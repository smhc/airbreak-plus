#include "stubs.h"

#define INLINE inline __attribute__((always_inline))
#define MAIN __attribute__((section(".text.0.main")))
#define STATIC static __attribute__((section(".text.x.nonmain")))

#define COUNT_PRETRIGGER_FLOW 0 // Include pre-breath-start positive flow values into cumulative volume of current breath
                                // (maybe important for early calculations, but I think it's likely to underestimate limitations in )
const float EPS_REDUCE_WHEN_ASV = 0.5f; // % of extra IPS to reduce EPS by

#define ASV 1
#define ASV_SLOPE 0

#define ASV_DISABLER 1 // Disable ASV after hyperpneas and/or apneas (because all of mine are central, todo: differentiate the two)
#define ASV_IPS_OVERSHOOT 0
#define ASV_IPS_DYNAMIC_GAIN 0

const float EPS_FLOWBASED_DOWNSLOPE = 0.75f; // Maximum flowbased %
const float EPS_FIXED_TIME = 1.1f; 

// 20*5*10ms = 1s
#define ASV_STEP_COUNT 20
#define ASV_STEP_LENGTH 5
#define ASV_STEP_SKIP 1 // Amount of steps before first doing ASV adjustments. MUST be at least 1 or code will crash due to out of bounds target array read
const float ASV_MAX_IPS = 2.0f;
const float ASV_GAIN = 3.0f; // 4.0f seems fine but maybe a bit aggressive?
const float ASV_MAX_EPAP = 0.0f;
const float ASV_EPAP_EEPAP_ONLY = 0.0f;
const float ASV_EPAP_EXTRA_EPS = 0.2f; // How much extra EPS per EPAP


const char S_UNINITIALIZED = 0;
const char S_START_INHALE = 1;
const char S_INHALE = 2;
const char S_INHALE_LATE  = 3; // After peak flow has been reached
const char S_EXHALE = 4; // Active exhale
const char S_EXHALE_LATE  = 5; // Expiratory pause


float * const fvars = (void*) 0x2000e948;
int * const ivars = (void*) 0x2000e750;



typedef unsigned int uint32;
typedef signed short int16;
typedef signed char int8;
typedef __fp16 float16;

/////// Utility functions ///////
#define min(a,b) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a < _b ? _a : _b; \
})

#define max(a,b) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a > _b ? _a : _b; \
})

#define clamp(a, _min, _max) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (_min) __min = (_min); \
  __typeof__ (_max) __max = (_max); \
  _a > __max ? __max : (_a < __min ? __min : a); \
})

INLINE float clamp01(float a) { return clamp(a, 0.0f, 1.0f); }

INLINE float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
INLINE float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

INLINE float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

INLINE float interpmin(float from, float to, float coeff, float min_speed) {
   float rate = (to - from) * coeff;
   if (rate >= 0.0f) {
     return from + clamp(rate, min_speed, to-from);
   } else {
     return from + clamp(rate, to-from, -min_speed);
   }
}

INLINE void interp_inplace(float *value, float towards, float coeff) {
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

INLINE void init_breath(breath_t *breath) {
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
  uint32 last_time;
  uint32 breath_count;

  int16 ticks; // Starts at 0, +1 each call
  int8 last_jitter;
  int8 dont_support;
  int8 asv_disable;
  int8 stage;

  float asv_target_slope;
  float asv_target_ips;
  float asv_target_epap;
  float asv_target_epap_target;
  float asv_target_adjustment;

  float target_ti;
  float cycle_off_timer;
  float final_ips;

  #if COUNT_PRETRIGGER_FLOW == 1
    float pretrigger_flow[10]; // 10t=100ms 
  #endif

  breath_t current;
  breath_t recent;
} my_data_t;

INLINE void init_my_data(my_data_t *data) {
  data->last_progress = 0.0f;
  data->last_time = tim_read_tim5();
  data->breath_count = 0;

  data->ticks = -1; // Uninitialized
  data->last_jitter = 0;
  data->dont_support = 0;
  data->asv_disable = 0;
  data->stage = S_UNINITIALIZED;

  data->asv_target_slope = 0.0f;
  data->asv_target_ips = 0.0f;
  data->asv_target_epap = 0.0f;
  data->asv_target_epap_target = 0.0f;
  data->asv_target_adjustment = 0.0f;

  data->target_ti = 1.0f;
  data->cycle_off_timer = 0.0f;
  data->final_ips = 0.0f;

  #if COUNT_PRETRIGGER_FLOW == 1
    for(int i=0; i<10; i++) { data->pretrigger_flow[i] = 0.0f; }
  #endif

  init_breath(&data->current);
  init_breath(&data->recent);
}

const float ASV_INTERP = 0.025f; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
INLINE void asv_interp_all(my_data_t* data) {
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
}


// Awful hacky code for storing arbitrary data
typedef struct {
  unsigned magic;
  my_data_t * data;
} magic_ptr_t;

magic_ptr_t * const magic_ptr = (void*) (0x2000e948 + 0x11*4);
const unsigned MAGIC = 0x07E49001;
INLINE my_data_t * get_data() {
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

INLINE float get_disabler_mult(int n) {
  if (n <= 0) { return 1.00f; }
  if (n == 1) { return 0.5f; }
  if (n == 2) { return 0.25f; }
  if (n == 3) { return 0.125f; }
  return 0.0f;
}


float *cmd_ps = &fvars[0x29];
float *cmd_epap = &fvars[0x28];
float *cmd_ipap = &fvars[0x2a]; // This is set to epap+ps elsewhere, and likely does nothing here

INLINE void apply_jitter(int8 amt) {
  float amtf = 0.01f * amt;
  *cmd_ps += amtf; *cmd_epap += amtf; *cmd_ipap += amtf;
}

// This is where the real magic starts
// The entry point. All other functions MUST be inline
void MAIN start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent. Only in S mode
  float s_ipap = fvars[0xe];
  float s_epap = fvars[0xf];
  float s_eps = 0.6f;
  float s_ips = s_ipap - s_epap;

  const float actual_pressure = fvars[1]; // Actual current pressure in the circuit

  float epap = s_epap; // (cmH2O)
  float ips = s_ips;   // (cmH2O)
  float eps = s_eps;    // (cmH2O)
  float rise_time = 0.70f;       // (s)
  float fall_time = 0.65f;       // (s)

  my_data_t *d = get_data();

  apply_jitter(-d->last_jitter); // Undo last jitter, to prevent small errors from it from accumulating.

  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = fvars[0x25]; // Leak-compensated patient flow

  if (ASV_EPAP_EEPAP_ONLY > 0.0f) {
    float a = max(d->asv_target_epap - s_epap, 0.0f) * ASV_EPAP_EEPAP_ONLY;
    s_ips -= a;
    ips = s_ips;
    eps += a;
  }

  eps += max(d->asv_target_epap - s_epap, 0.0f) * ASV_EPAP_EXTRA_EPS;

  float slope = ips / rise_time; // (cmH2O/s) Slope to meet IPS in rise_time milliseconds 
  float SLOPE_MIN = slope; // (cmH2O/s)
  float SLOPE_MAX = 10.0f; // (cmH2O/s)
  float SLOPE_EPAP = 0.5f; // (cmH2O/s)

  ips   = max(d->current.ips, s_ips);
  slope = max(d->current.slope, SLOPE_MIN);

  // Process breath stage logic
  if (d->last_progress > progress + 0.25f) { // Stock inhale trigger
    d->stage = S_START_INHALE;
  } else if ((d->stage == S_INHALE) && (*cmd_ps >= ips*0.99f)) {
    d->stage = S_INHALE_LATE;
  } else if ((d->stage == S_INHALE) || (d->stage == S_INHALE_LATE)) {
    // Cycle off below 0 flow, or after 100ms past configured cycle sensitivity.
    if (flow < -0.01f * d->current.inh_maxflow) { d->stage = S_EXHALE; }
    if (progress > 0.5f) { 
      d->cycle_off_timer += delta;
      if (d->cycle_off_timer >= 0.95f) { d->stage = S_EXHALE; }
    }
  } else if ((d->stage == S_EXHALE) && ((d->current.volume / d->current.volume_max) <= 0.1f)) {
    d->stage = S_EXHALE_LATE;
  }

  // Initialize new breath
  if (d->stage == S_START_INHALE) {
    d->stage = S_INHALE;
    if (((d->current.volume_max / d->recent.volume_max) <= 1.35f) || d->breath_count < 90) {
    if ((d->current.te > d->recent.te * 0.60f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        asv_interp_all(d);
    }}

    if ((d->current.ti > d->target_ti - 0.4f)
     && (d->current.ti < d->target_ti + 0.4f)) {
      d->target_ti = d->current.ti;
    } else {
      interp_inplace(&d->target_ti, d->current.ti, 0.3f);
    }
    d->target_ti = clamp(d->target_ti, 1.0f, 1.8f);

    // 0-25% asvIPS -> go down; 25-100% asvIPS -> go up 
    float rel_ips = max(d->current.ips - s_ips, 0.0f) / ASV_MAX_IPS;
    if (ASV_MAX_EPAP > 0.0f) {
      float rel_epap = max(d->asv_target_epap - s_epap, 0.0f) / ASV_MAX_EPAP;
      d->asv_target_epap_target = d->asv_target_epap + (map01c(rel_ips, 0.25f, 1.0f) * (1.0f - rel_epap) - map01c(rel_ips, 0.25f, 0.0f) * rel_epap) * ASV_MAX_EPAP;
      d->asv_target_epap_target = clamp(d->asv_target_epap_target, s_epap, s_epap + ASV_MAX_EPAP);
      // d->asv_target_epap_target = s_epap + map01c(d->current.ips, s_ips, s_ips + ASV_MAX_IPS) * ASV_MAX_EPAP;
    }
    d->asv_target_epap = max(d->asv_target_epap, s_epap);

    d->asv_target_ips = 0.9f * d->current.ips + 0.1f * s_ips;

    if ((ASV_DISABLER == 1) && (d->dont_support == 0)) {
      // For every 10% volume excess above 120%, or for every 1s excess above recent average te, disable ASV for 1 breath, for up to 6 at once
      int te_excess = (int)(clamp(d->current.te - max(1.8f, d->recent.te), 0.0f, 6.0f));
      int vol_excess = (int)(clamp((d->current.volume_max / d->recent.volume_max - 1.1f - d->asv_target_adjustment) * 10.0f, 0.0f, 6.0f));
      d->asv_disable = d->asv_disable + te_excess + vol_excess - 1; // Reduce by 1 per breath
      d->asv_disable = clamp(d->asv_disable, -2, 9);
    }

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.20f) && (d->current.te <= d->recent.te * 0.50f);

    d->asv_target_adjustment = 0.0f;

    d->cycle_off_timer = 0.0f;

    d->final_ips = 0.0f;

    #if (ASV == 1) && (COUNT_PRETRIGGER_FLOW == 1)
      for(int i=0; i<10; i++) { 
        if (d->pretrigger_flow[i] > 0.0f) {
          d->current.volume += d->pretrigger_flow[i];
        }
        d->pretrigger_flow[i] = 0.0f; 
      }
    #endif
    init_breath(&d->current);
    d->current.ips = max(d->asv_target_ips, s_ips);
    d->current.slope = max(d->asv_target_slope, SLOPE_MIN);
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
  d->current.volume_max = max(d->current.volume_max, d->current.volume);
  d->current.duration += delta;
  d->current.exh_maxflow = min(d->current.exh_maxflow, flow); 
  d->current.inh_maxflow = max(d->current.inh_maxflow, flow); 
  if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
    d->current.ti += delta; 
  } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) {
    d->current.te += delta;
    if (d->final_ips == 0.0f) { d->final_ips = *cmd_ps; }
  }

  #if ASV == 1
    int i = d->ticks/ASV_STEP_LENGTH;

    if ((d->ticks % ASV_STEP_LENGTH == 0) && (i>=ASV_STEP_SKIP) && (i<ASV_STEP_COUNT) && (d->stage == S_INHALE)) {
      d->current.targets[i] = d->current.volume;
      float error_volume = d->current.targets[i] / (d->recent.targets[i] + 0.001f);

      float recent_flow = d->recent.targets[i] - d->recent.targets[i-1];
      float current_flow = d->current.targets[i] - d->current.targets[i-1];
      float error_flow = current_flow / (recent_flow + 0.001f);

      #if ASV_IPS_OVERSHOOT == 1
        if (i >= 5) {
          // If the error is large enough, shift the target for current breath higher
          float adjustment = ((0.85f - error_volume) / 0.15f) * 0.025f;
          d->asv_target_adjustment = max(d->asv_target_adjustment, adjustment);
        }
      #endif

      // This way:  95-130% => 0 to -1;  95-50% => 0 to 1
      float ev2 = error_volume - d->asv_target_adjustment;
      float ips_adjustment = map01c(ev2, 0.95f, 0.5f) - map01c(ev2, 0.98f, 1.3f);
      #if ASV_IPS_DYNAMIC_GAIN == 1
        float base_ips = max(d->current.ips, s_ips);
      #else
        float base_ips = max(d->asv_target_ips, s_ips);
      #endif
      // The 0.05f values ensure the adjustment is not super tiny.
      if (ips_adjustment > 0.01f) {
        d->current.ips = base_ips + min(ips_adjustment + 0.05f, 1.0f) * ASV_GAIN;
      } else if (ips_adjustment < -0.01f) {
        d->current.ips = base_ips + min(ips_adjustment - 0.05f, 1.0f) * (base_ips - s_ips);
      } else {
        d->current.ips = base_ips;
      }
      d->current.ips = clamp(d->current.ips, max(s_ips, *cmd_ps), s_ips + ASV_MAX_IPS);
      d->current.ips = s_ips + get_disabler_mult(d->asv_disable) * (d->current.ips - s_ips);

      #if ASV_SLOPE == 1
        // Set slope based on current flow deficit
        d->current.slope = SLOPE_MIN + (SLOPE_MAX - SLOPE_MIN) * temp;
      #else
        d->current.slope = d->current.ips / rise_time;
      #endif
    }
    // eps += (d->asv_target_epap - s_epap) * 0.2f; // 0.2cmH2O extra EPS for every 1cmH2O of EPAP
    ips = max(d->current.ips, s_ips);
    slope = max(d->current.slope, SLOPE_MIN);
    if (EPS_REDUCE_WHEN_ASV > 0.0f) {
      // (don't- I think it just contributes to expiratory intolerance) Reduce EPS proportionally to extra IPS
      if (d->current.ips > s_ips) {
         eps = max(0.0f, eps - (d->current.ips - s_ips) * EPS_REDUCE_WHEN_ASV);
      }
    }
  #endif

  // Allow myself to disable ASV during the night, if it disrupts my sleep after all
  float a = (s_ipap - (int)s_ipap);
  if (((a >= 0.1f) && (a <= 0.3f)) || ((a >= 0.7f) && (a <= 0.9f))) {
    ips = s_ips;
    slope = SLOPE_MIN;
    eps = 0.6f;
  }

  if (d->asv_target_epap < d->asv_target_epap_target) {
    d->asv_target_epap += 0.025f * delta; // 40s to change by 1cmH2O
  } else {
    d->asv_target_epap -= 0.0125f * delta; // 1m20s to drop by 1cmH2O
  }
  d->asv_target_epap = clamp(d->asv_target_epap, s_epap, s_epap + ASV_MAX_EPAP);
  epap = d->asv_target_epap;

  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 3.0f * delta);
  } else {
    if (d->stage == S_INHALE) {
      float t = d->current.ti;
      if (ips - *cmd_ps <= 0.4f) { slope *= 0.5f; }
      if (ips - *cmd_ps <= 0.2f) { slope *= 0.5f; }
      if (ips - *cmd_ps <= 0.0f) { slope  = 0.0f; }

      *cmd_ps = min(ips, *cmd_ps + slope * delta ); // Avoid mid-slope PS drops.
    } if (d->stage == S_INHALE_LATE) { 
      float t = d->current.ti;
      *cmd_ps = ips;
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) { // Exhale
      float t = d->current.te;
      float eps_mult = map01c(d->current.volume / d->current.volume_max, 0.05f, 0.6f);
      eps_mult = min(eps_mult, map01c(t, EPS_FIXED_TIME, 0.4f));

      float ips_mult = map01c(t, fall_time, 0.0f); ips_mult = ips_mult * ips_mult * 0.95f;
      if (EPS_FLOWBASED_DOWNSLOPE > 0.0f) {
        float temp = EPS_FLOWBASED_DOWNSLOPE;
        ips_mult = min(ips_mult, ips_mult * (1.0f-temp) + temp * map01c(flow, -d->current.inh_maxflow * 0.9f, 0.0f) );
      }
      *cmd_ps = ips_mult * d->final_ips - (1.0f - ips_mult) * eps_mult * eps;
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, s_ips + ASV_MAX_IPS);
  *cmd_epap = clamp(*cmd_epap, s_epap - 1, s_epap + ASV_MAX_EPAP);
  *cmd_ipap = *cmd_epap + *cmd_ps;

  // Necessary to keep graphing code called(I think based on PS change, who cares)
  d->last_jitter ^= 2 - (tim_read_tim5() & 4);
  apply_jitter(d->last_jitter);
  return;
}
