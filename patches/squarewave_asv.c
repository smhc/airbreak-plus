#include "stubs.h"

#define INLINE inline __attribute__((always_inline))

#define COUNT_PRETRIGGER_FLOW 0 // Include pre-breath-start positive flow values into cumulative volume of current breath
                                // (maybe important for early calculations, but I think it's likely to underestimate limitations in )
#define REDUCE_EPS_WHEN_ASV 0 // I think it just causes expiratory intolerance

#define ASV_ONE 0 // Initial, ongoing TV targeting ASV algo

#define ASV_EARLY 0 // TODO: Early ASV that increases slope when flow doesn't meet expectation
#define ASV_LATE 1 // WIP: Standard ASV algo that targets peak flow

typedef signed short int16;
typedef signed char int8;

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

const int ASV_ADJUST_EVERY = 5; // 5 ticks (50ms) 
const float ASV_INTERP = 0.025; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
const int ASV_GRACE_PERIOD = 2; // Amount of ticks before first running ASV
#define ASV_STEPS 20 // 20*5=1000ms window of adjustment
const float ASV_MAX_IPS = 2.0f;

static INLINE void asv_interp(float *value, float towards) {
  *value = interp(*value, towards, ASV_INTERP);
}



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
  float targets[ASV_STEPS];
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
  for(int i=0; i<ASV_STEPS; i++) {
    breath->targets[i] = 0.0f;
  }
}

static INLINE void asv_interp_all(breath_t *recent, breath_t *current) {
  // Don't adjust targets if it's a hyperpnea. Breath count hardcoded for ASV_INTERP=0.025f constant.
  // TODO: Instead of counting breaths, wait for average error to stabilize
  for(int i=0; i<ASV_STEPS; i++) {
    // If it has zero volume, it means the breath was past its peak already, ignore it.
    if (current->targets[i] > 0.0f) {
      asv_interp(&recent->targets[i], current->targets[i]);
    }
    current->targets[i] = 0.0f;
  }
  asv_interp(&recent->volume_max, current->volume_max);
  asv_interp(&recent->duration, current->duration);
  asv_interp(&recent->exh_maxflow, current->exh_maxflow);
  asv_interp(&recent->inh_maxflow, current->inh_maxflow);
  asv_interp(&recent->ti, current->ti);
  asv_interp(&recent->te, current->te);
  recent->ips = interp(recent->ips, current->ips, 0.5f);
}

typedef struct {
  float last_progress;
  unsigned last_time;
  unsigned breath_count;
  int16 ticks; // Starts at 0, +1 each call

  int dont_support;
  float asv_late_target;

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
  data->asv_late_target = 0.0f;
  #if COUNT_PRETRIGGER_FLOW == 1
    for(int i=0; i<10; i++) { data->pretrigger_flow[i] = 0.0f; }
  #endif
  init_breath(&data->current);
  init_breath(&data->recent);
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


static float * const fvars = (void*) 0x2000e948;
static int * const ivars = (void*) 0x2000e750;


// The entry point. All other functions MUST be inline
void start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];
  const float s_ips = s_ipap - s_epap;

  float epap = s_epap;
  float ips = s_ips;
  float eps = 1.5f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2a]; // This is set to epap+ps elsewhere, and likely does nothing here

  my_data_t *d = get_data();

  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = fvars[0x25]; // Leak-compensated patient flow
  // Don't multiply by delta since it doesn't matter for calc and loses a little precision


  // Initialize new breath
  if (d->last_progress > progress + 0.25f) {

    #if ASV_LATE == 1
      float peak_flow_error = (d->current.inh_maxflow - d->recent.inh_maxflow) / maxf(d->recent.inh_maxflow, 10.0f);
      if (peak_flow_error < -0.05f) {
        d->asv_late_target += map01c(peak_flow_error, 0.0f, -0.40f) * 1.0f + 0.2f;
      } else if (peak_flow_error > 0.45f ){
        d->asv_late_target = d->asv_late_target * 0.5f - 0.1f;
      } else if (peak_flow_error >= 0.0f) {
        d->asv_late_target *= 0.9f - map01c(peak_flow_error, 0.0f, 0.45f);
      }
      d->asv_late_target = clamp(d->asv_late_target, 0.0f, ASV_MAX_IPS);
    #endif

    d->recent.ips = maxf(d->recent.ips, s_ips);
    d->current.ips = d->recent.ips;
    // TODO: Replace d->duration with d->te
    if (((d->current.volume_max / d->recent.volume_max) <= 1.35f) || d->breath_count < 90) {
    if ((d->current.duration > 2.0f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        asv_interp_all(&d->recent, &d->current);
    }}

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.15f) && (d->current.duration <= 2.0f); // TODO: TE-based calculation

    #if COUNT_PRETRIGGER_FLOW == 1
      for(int i=0; i<10; i++) { 
        if (d->pretrigger_flow[i] > 0.0f) {
          d->current.volume += d->pretrigger_flow[i];
        }
        d->pretrigger_flow[i] = 0.0f; 
      }
    #endif

    init_breath(&d->current);
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
  else { d->current.te += delta; }


  #if ASV_LATE == 1
    ips = ips + d->asv_late_target;
  #endif

  #if ASV_ONE == 1
    // ASV operation
    if (d->current.volume >= d->current.volume_max) {
      int i = d->ticks/ASV_ADJUST_EVERY - ASV_GRACE_PERIOD;
      
      if ((d->ticks % ASV_ADJUST_EVERY == 0) && i>=0 && i<ASV_STEPS) {
        d->current.targets[i] = d->current.volume;

        float gain = 10.0f * (ASV_ADJUST_EVERY * delta); // 8.0/s 
        if (i <= 3 || i >= 15 ) gain *= 0.5f;
        if (i <= 1) gain *= 0.25f;

        // Right now, it targets 90-95% of recent TV
        float delta_ips = (d->recent.targets[i] - d->current.volume) / (d->recent.targets[i] +0.00001f); // Volume-independent version
        delta_ips = clamp(delta_ips, -0.5f, +0.5f); // Prevent anomalous values
        if (delta_ips >= 0.10f) {
          delta_ips = gain * (delta_ips-0.10f);
        } else if (delta_ips <= 0.05f) {
          delta_ips = gain * (delta_ips-0.05f);
        } else {
          delta_ips = 0.0f;
        }
        d->current.ips = clamp(d->current.ips + delta_ips, ips, ips+ASV_MAX_IPS);
      }
    }
    // Allow myself to disable ASV during the night, if it disrupts my sleep after all
    if ((s_ipap - s_epap) > 3.1f) {
      ips = s_ipap - s_epap;
    } else {
      ips = d->current.ips;
      #if REDUCE_EPS_WHEN_ASV == 1
        // (don't- I think it just contributes to expiratory intolerance) Reduce EPS proportionally to extra IPS
        if (d->current.ips > (ips)) {
           eps = maxf(0, eps - (d->current.ips - ips)*0.5f );
        }
      #endif
    }
  #endif



  // Slope: 4-12cmH2O/s (about 0.75s to 0.25s to 3cmH2O pressure) - lower than this is acceptable when ahead of the minimum slope
  //   How to calculate if we're ahead of the original slope to IPS ?
  //     
  #if ASV_EARLY == 1
    /*

      float slope(float x1, float y1, float x2, float y2) {
        return (y1 - y2) / (x1 - x2)
      }

      // // Alternative ASV code 
      // float error = (d->recent.targets[i] - d->current.volume) / (d->recent.targets[i] +0.00001f)

  
    float error = (d->current.volume - d->recent.targets[i]) / (d->recent.targets[i] +0.00001f); // -0.5 is 50% of target volume, +0.5 is 150%
    if (error < -0.10f) {
      maximum_slope
    } else if (error > 0.0f) {
      lower slope
    } else {
      slope_to_minps
    }

    if (breath_time < (min_slope / ips) - 0.05) {
      const float PS_DAMPER = 0.65f; // During a hyperpnea, PS target can be reduced to this fraction.
      const float SLOPE_MIN = 4.0f; // 750ms to 3 IPS
      const float SLOPE_MAX = 12.0f; // 250ms to 3 IPS
      // A: 2->3, 0.25/0.75
      // B: 1->3, 0.50/0.75
      // C: 4->3, 0.50/0.75
      // D: 2->3, 1.00/0.75
      // D: 4->3, 1.00/0.75
      float damped_slope = minf(current.ips - ips*PS_DAMPER, 0.0f) / minf(breath_time - ips*PS_DAMPER / SLOPE_MIN, -0.01f) ;
      float damped_slope = minf(damped_slope, 0.0f);
      // when time is over or close to target, this shoots to stupid values, when it should be set to zero instead
      float base_slope = minf(current.ips - ips, 0.0f) / minf(breath_time - ips / SLOPE_MIN, -0.01f);
      // float base_slope = get_slope(current.ips, breath_time, ips, ips/SLOPE_MIN);
      // float base_slope = minf(SLOPE_MIN, base_slope); // Lower between original and current slope to meeting minimum PS on time.
      float base_slope = minf(base_slope, 0.0f); // Lower between original and current slope to meeting minimum PS on time.
    } else {
      damped_slope = 0.0f;
      base_slope = 0.0f;
    }
    float max_slope = SLOPE_MAX;

    float min_slope = 
    float base_slope = minf(SLOPE_MIN, slope2);
    float max_slope = minf(SLOPE_MAX); */
  #endif


  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 10.0f * delta); // Max of 10cmH2O/s change
  } else {
    // Interpolate PS and EPAP
    if (progress <= 0.5f) { // Inhale
      *cmd_ps = minf(ips, *cmd_ps + ips * delta / 0.65f ); // Avoid mid-slope PS drops.
    } else { // Exhale
      float volumebased_mult = map01c(d->current.volume, d->current.volume_max * 0.15f, d->current.volume_max * 0.70f); // At >70% residual volume == 1, below 15% == 0
      *cmd_ps = interpmin(*cmd_ps, -eps * volumebased_mult, 0.0625f, 3.0f * delta); // Convert to use dt: delta * 1cmH2O/s minimum // 0.075, 0.02 is ~300ms downslope. Marginally punchy
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, +ips + ASV_MAX_IPS);
  *cmd_epap = clamp(*cmd_epap, s_epap - 1, s_epap + 1);

  // Necessary to keep graphing code called(I think based on PS change, who cares)
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;

  return;
}
