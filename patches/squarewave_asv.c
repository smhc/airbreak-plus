#include "stubs.h"

#define INLINE inline __attribute__((always_inline))

#define COUNT_PRETRIGGER_FLOW 0 // Include pre-breath-start positive flow values into cumulative volume of current breath
                                // (maybe important for early calculations, but I think it's likely to underestimate limitations in )
#define REDUCE_EPS_WHEN_ASV 1 // I think it just causes expiratory intolerance

#define ASV_EARLY 0
#define ASV_EARLY_SLOPE 0
#define ASV_EARLY_IPS 1
#define ASV_EARLY_IPS_EPAP 0
// #define ASV_EARLY_TARGET_FLOW 0 // Target flow instead of volume

#define IPS_FAST_SLOPE 1

#define EPS_ENABLE 1
#define EPS_VOLUMEBASED 1
const float EPS_FIXED_TIME = 1.1f; 

// 20*5*10ms = 1s
#define ASV_STEP_COUNT 20
#define ASV_STEP_LENGTH 5
#define ASV_STEP_SKIP 1 // Amount of steps before first doing ASV adjustments. MUST be at least 1 or code will crash due to out of bounds target array read
const float ASV_MAX_IPS = 2.0f;
const float ASV_MAX_EPAP = 2.0f;


static float * const fvars = (void*) 0x2000e948;
static int * const ivars = (void*) 0x2000e750;

typedef signed short int16;
typedef signed char int8;
typedef __fp16 float16;

static INLINE float maxf(float a, float b) {
  if (a >= b) { return a; }
  else { return b; }
}

static INLINE float minf(float a, float b) {
  if (a < b) { return a; }
  else { return b; }
}

static INLINE float clamp(float a, float _min, float _max) {
  return maxf(minf(a, _max), _min);
}
static INLINE float clamp01(float a) { return clamp(a, 0.0f, 1.0f); }

static INLINE float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
static INLINE float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

static INLINE float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

static INLINE float interpmin(float from, float to, float coeff, float min_speed) {
   float rate = (to - from) * coeff;
   if (rate >= 0.0f) {
     return from + clamp(rate, min_speed, to-from);
   } else {
     return from + clamp(rate, to-from, -min_speed);
   }
}

static INLINE void interp_inplace(float *value, float towards, float coeff) {
  *value = interp(*value, towards, coeff);
}


// Setup storage for important data
typedef struct {
  float volume;
  float volume_max;

  float duration;

  // New stuff
  float exh_maxflow;
  float inh_maxflow;
  float ti;
  float te;

  float ips;
  float slope;
  float targets[ASV_STEP_COUNT];
} breath_t;

static INLINE void init_breath(breath_t *breath) {
  breath->volume = 0.0f;
  breath->volume_max = 0.0f;
  breath->duration = 0.0f;
  breath->exh_maxflow = 0.0f;
  breath->inh_maxflow = 0.0f;
  breath->ti = 0.0f;
  breath->te = 0.0f;
  breath->ips = 0.0f;
  breath->slope = 0.0f;
  for(int i=0; i<ASV_STEP_COUNT; i++) {
    breath->targets[i] = 0.0f;
  }
}

typedef struct {
  float last_progress;
  unsigned last_time;
  unsigned breath_count;
  int16 ticks; // Starts at 0, +1 each call

  int dont_support;
  float asv_target_slope;
  float asv_target_ips;
  float asv_target_epap;
  float asv_target_epap_adjustment;

  float final_ips;

  #if COUNT_PRETRIGGER_FLOW == 1
    float pretrigger_flow[10]; // 10t=100ms 
  #endif


  breath_t current;
  breath_t recent;
} my_data_t;

static INLINE void init_my_data(my_data_t *data) {
  data->last_progress = 0.0f;
  data->last_time = tim_read_tim5();
  data->breath_count = 0;
  data->ticks = -1; // Uninitialized
  data->dont_support = 0;
  data->asv_target_slope = 0.0f;
  data->asv_target_ips = 0.0f;
  data->asv_target_epap = 0.0f;
  data->asv_target_epap_adjustment = 0.0f;
  data->final_ips = 0.0f;
  #if COUNT_PRETRIGGER_FLOW == 1
    for(int i=0; i<10; i++) { data->pretrigger_flow[i] = 0.0f; }
  #endif
  init_breath(&data->current);
  init_breath(&data->recent);
}


const float ASV_INTERP = 0.025f; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
static INLINE void asv_interp_all(my_data_t* data) {
  breath_t *recent = &data->recent;
  breath_t *current = &data->current;
  float coeff = ASV_INTERP;
  float coeff_v = coeff; // Update volumes a bit differently
  if (current->volume_max < recent->volume_max) { // Update downwards a bit slower.
    coeff_v *= 0.4f;
  }
  // Don't adjust targets if it's a hyperpnea. Breath count hardcoded for ASV_INTERP=0.025f constant.
  // TODO: Instead of counting breaths, wait for average error to stabilize
  for(int i=0; i<ASV_STEP_COUNT; i++) {
    // If it has zero volume, it means the breath was past its peak already, ignore it.
    if (current->targets[i] > 0.0f) {
      interp_inplace(&recent->targets[i], current->targets[i], coeff_v);
    }
    current->targets[i] = 0.0f;
  }
  interp_inplace(&recent->ips, current->ips, 0.5f);
  interp_inplace(&recent->slope, current->slope, 0.5f);
  interp_inplace(&recent->volume_max, current->volume_max, coeff_v);
  interp_inplace(&recent->duration, current->duration, coeff);
  interp_inplace(&recent->exh_maxflow, current->exh_maxflow, coeff);
  interp_inplace(&recent->inh_maxflow, current->inh_maxflow, coeff);
  interp_inplace(&recent->ti, current->ti, coeff);
  interp_inplace(&recent->te, current->te, coeff);
  // interp_inplace(&recent->ips, current->ips, 0.5f);
}


// Awful hacky code for storing arbitrary data
typedef struct {
  unsigned magic;
  my_data_t * data;
} magic_ptr_t;

static magic_ptr_t * const magic_ptr = (void*) (0x2000e948 + 0x11*4);
const unsigned MAGIC = 0x07E49001;
static INLINE my_data_t * get_data() {
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->data = malloc(sizeof(my_data_t));
  }
  unsigned now = tim_read_tim5();
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was stopped and re-started.
  if ((magic_ptr->magic != MAGIC) || (now - magic_ptr->data->last_time) > 100000) {
    init_my_data(magic_ptr->data);
  }
  magic_ptr->data->last_time = now; // Keep it updated so we don't reset the struct
  magic_ptr->magic = MAGIC;
  return magic_ptr->data;
}

// asv_epap_min = fvars[0x11];
// asv_epap_max = fvars[0x10];
// asv_ips_min = fvars[0x14];
// asv_ips_max = fvars[0x13];

// This is where the real magic starts
// The entry point. All other functions MUST be inline
void start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent. Only in S mode
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];
  const float s_ips = s_ipap - s_epap;

  float epap = s_epap;
  float ips = s_ips;
  float eps = 0.4f;
  float rise_time = 0.65f;
  float slope = ips / rise_time; // Slope to meet IPS in rise_time milliseconds 
  float fall_time = 0.65f;

  const float SLOPE_MIN = slope;
  const float SLOPE_MAX = 10.0f;
  const float SLOPE_EPAP = 0.5f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2a]; // This is set to epap+ps elsewhere, and likely does nothing here

  my_data_t *d = get_data();

  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = fvars[0x25]; // Leak-compensated patient flow

  // Initialize new breath
  if (d->last_progress > progress + 0.25f) {
    if (((d->current.volume_max / d->recent.volume_max) <= 1.35f) || d->breath_count < 90) {
    if ((d->current.te > d->recent.te * 0.60f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        asv_interp_all(d);
    }}

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.20f) && (d->current.te <= d->recent.te * 0.50f);

    d->final_ips = 0.0f;

    #if (ASV_EARLY == 1) && (COUNT_PRETRIGGER_FLOW == 1)
      for(int i=0; i<10; i++) { 
        if (d->pretrigger_flow[i] > 0.0f) {
          d->current.volume += d->pretrigger_flow[i];
        }
        d->pretrigger_flow[i] = 0.0f; 
      }
    #endif
    init_breath(&d->current);
    // TODO: Replace with d->asv_target_ ips/slope
    // d->current.ips   = maxf(d->current.ips, s_ips);
    // d->current.slope = maxf(d->current.slope, SLOPE_MIN);
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }
  #if COUNT_PRETRIGGER_FLOW == 1
    if (d->ticks != -1) {
      d->pretrigger_flow[d->ticks % 10] = flow;
    }
  #endif


  // Keep track of ongoing values
  d->current.volume += flow;
  d->current.volume_max = maxf(d->current.volume_max, d->current.volume);
  d->current.duration += delta;
  d->current.exh_maxflow = minf(d->current.exh_maxflow, flow); 
  d->current.inh_maxflow = maxf(d->current.inh_maxflow, flow); 
  if (progress <= 0.5f) { d->current.ti += delta; }
  else { 
    d->current.te += delta;
    if (d->final_ips == 0.0f) { 
      d->final_ips = *cmd_ps; 
      // #if ASV_EARLY_IPS_EPAP == 1
      //   d->asv_target_epap = (d->asv_target_epap - s_epap) * 0.8f + map01c(d->final_ips, s_ips, s_ips + ASV_MAX_IPS) * 0.4f;
      // #endif
    }
  }

  // #if ASV_EARLY_IPS_EPAP == 1
  //  d->asv_target_epap = clamp(d->asv_target_epap, s_epap, s_epap + ASV_MAX_EPAP);
  // #endif

  #if ASV_EARLY == 1
    int i = d->ticks/ASV_STEP_LENGTH;

    if ((d->ticks % ASV_STEP_LENGTH == 0) && i>=ASV_STEP_SKIP && i<ASV_STEP_COUNT) {
      d->current.targets[i] = d->current.volume;
      float error_volume = (d->recent.targets[i] - d->current.volume) / (d->recent.targets[i] +0.001f);

      float recent_flow = d->recent.targets[i] - d->recent.targets[i-1];
      float current_flow = d->current.targets[i] - d->current.targets[i-1];
      float error_flow = (recent_flow - current_flow) / (recent_flow + 0.001f); // No need to divide by step length

      // // Right now, it targets 90-95% of recent TV.
      // float delta_ips = error_volume;
      // delta_ips = clamp(delta_ips, -0.5f, +0.5f); // Prevent anomalous values
      // if (delta_ips >= 0.10f) {
      //   delta_ips = gain * (delta_ips-0.10f);
      // } else if (delta_ips <= 0.05f) {
      //   delta_ips = gain * (delta_ips-0.05f);
      // } else {
      //   delta_ips = 0.0f;
      // }
      // d->current.ips = clamp(d->current.ips + delta_ips, maxf(s_ips, *cmd_ps), s_ips+ASV_MAX_IPS);

      // TODO: use d->asv_target_ ips/slope as baseline, adjust from that.

      // Map 95-50% volume to 0-1, base extra IPS on that.
      float temp = map01c(error_flow, 0.05f, 0.4f);
      float extra_ips = (map01c(error_volume, 0.05f, 0.5f) + temp*0.125f) * 4.0f;
      d->current.ips = clamp(s_ips + extra_ips, maxf(s_ips, *cmd_ps), s_ips + ASV_MAX_IPS);

      // Set slope based on current flow deficit
      d->current.slope = SLOPE_MIN + (SLOPE_MAX - SLOPE_MIN) * temp;
    }
    ips = maxf(d->current.ips, s_ips);
    slope = maxf(d->current.slope, SLOPE_MIN);
    #if REDUCE_EPS_WHEN_ASV == 1
      // (don't- I think it just contributes to expiratory intolerance) Reduce EPS proportionally to extra IPS
      if (d->current.ips > s_ips) {
         eps = maxf(0.0f, eps - (d->current.ips - s_ips)*0.5f );
      }
    #endif

    // Allow myself to disable ASV during the night, if it disrupts my sleep after all
    float a = (s_ipap - (int)s_ipap);
    if (((a >= 0.1f) && (a <= 0.3f)) || ((a >= 0.7f) && (a <= 0.9f))) {
      ips = s_ips;
      slope = SLOPE_MIN;
    }
  #endif

  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 3.0f * delta);
  } else {
    if (progress <= 0.5f) { // Inhale
      float t = d->current.ti;
      if (ips - *cmd_ps <= 0.4f) { slope *= 0.5f; }
      if (ips - *cmd_ps <= 0.2f) { slope *= 0.5f; }

      // Slows slope by 66ms
      if (t <= 0.050f) { slope *= 0.707f; }
      if (t <= 0.100f) { slope *= 0.707f; }

      *cmd_ps = minf(ips, *cmd_ps + slope * delta ); // Avoid mid-slope PS drops.
    } else { // Exhale
      float t = d->current.te;
      #if EPS_ENABLE == 1
        #if EPS_VOLUMEBASED == 1
          float eps_mult = map01c(d->current.volume / d->current.volume_max, 0.05f, 0.6f);
          eps_mult = minf(eps_mult, map01c(t, EPS_FIXED_TIME, 0.4f));
        #else
          float eps_mult = 0.0f;
          float t_midpoint = maxf(t * 0.65f, EPS_FIXED_TIME) / 2.0f;
          float t_endpoint = maxf(t * 0.65f, EPS_FIXED_TIME);
          if (t <= t_midpoint) {
            eps_mult = map01(t, 0.0f, t_midpoint);
          } else if (t <= t_endpoint ) {
            eps_mult = map01(t, t_endpoint, t_midpoint);
          }
        #endif
      #else
          float eps_mult = 0.0f;
      #endif

      float a = map01c(t, fall_time, 0.0f); a = a * a * 0.95f;
      *cmd_ps = a * d->final_ips - (1.0f - a) * eps_mult * eps;
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, s_ips + ASV_MAX_IPS);
  *cmd_epap = clamp(*cmd_epap, s_epap - 1, s_epap + 1);
  *cmd_ipap = *cmd_epap + *cmd_ps;

  // Necessary to keep graphing code called(I think based on PS change, who cares)
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;

  return;
}
