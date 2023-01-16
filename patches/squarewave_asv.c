#include "stubs.h"

#define COUNT_PRETRIGGER_FLOW 1 // Include pre-breath-start positive flow values into cumulative volume of current breath

static inline float maxf(float a, float b) {
  if (a >= b) { return a; }
  else { return b; }
}

static inline float minf(float a, float b) {
  if (a < b) { return a; }
  else { return b; }
}

static inline float clamp(float a, float _min, float _max) {
  return maxf(minf(a, _max), _min);
}

static inline float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
static inline float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

static inline float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

static inline float interpmin(float from, float to, float coeff, float min_speed) {
   float rate = (to - from) * coeff;
   if (rate >= 0.0f) {
     return from + clamp(rate, min_speed, to-from);
   } else {
     return from + clamp(rate, to-from, -min_speed);
   }
}

static inline float cinterp(float from, float to, float speed) {
  if (to >= from) {
    return from + minf(to - from, speed);
  } else {
    return from - minf(from - to, speed);
  }
}

const int ASV_ADJUST_EVERY = 5; // 5 ticks (50ms) 
const float ASV_INTERP = 0.025; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
const int ASV_GRACE_PERIOD = 2; // Amount of ticks before first running ASV
#define ASV_STEPS 20 // 20*5=1000ms window of adjustment
const float ASV_MAX_IPS = 2.0f;

static inline void asv_interp(float *value, float towards) {
  *value = interp(*value, towards, ASV_INTERP);
}

typedef struct {
  float last_progress;
  float volume;
  float recent_peak_volume;
  float peak_volume;
  unsigned last_time;

  float recent_duration;
  float duration;
  unsigned breath_count;
  int ticks; // Starts at 0, +1 each call
  // float ti; float te;
  // float recent_ti; recent_te;

  float recent_volume[ASV_STEPS];
  float current_volume[ASV_STEPS];
  #if COUNT_PRETRIGGER_FLOW == 1
    float last_flow[10]; // 10t=100ms 
  #endif
  float recent_ips;
  float current_ips;
  int dont_support;
} my_data_t;

// Awful hacky code for storing arbitrary data
typedef struct {
  unsigned magic;
  my_data_t * data;
} magic_ptr_t;
static magic_ptr_t * const magic_ptr = (void*) (0x2000e948 + 0x11*4);
const unsigned MAGIC = 0x07E49001;

static inline my_data_t * get_data() {
  unsigned now = tim_read_tim5();
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->data = malloc(sizeof(my_data_t));
  }
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was stopped and re-started.
  if ((magic_ptr->magic != MAGIC) || (now - magic_ptr->data->last_time) > 100000) {
    magic_ptr->data->last_progress = 0.0f;
    magic_ptr->data->volume = 0.0f;
    magic_ptr->data->recent_peak_volume = 0.0f;
    magic_ptr->data->peak_volume = 0.0f;
    magic_ptr->data->recent_duration = 0.0f;
    magic_ptr->data->duration = 0.0f;
    magic_ptr->data->breath_count = 0;
    magic_ptr->data->ticks = -1; // Uninitialized
    magic_ptr->data->dont_support = 0;
    #if COUNT_PRETRIGGER_FLOW == 1
      for(int i=0; i<10; i++) { magic_ptr->data->last_flow[i] = 0.0f; }
    #endif
    for(int i=0; i<ASV_STEPS; i++) {
      magic_ptr->data->recent_volume[i] = 0.0f;
      magic_ptr->data->current_volume[i] = 0.0f;
    }
    magic_ptr->data->recent_ips = 0.0f;
    magic_ptr->data->current_ips = 0.0f;
  }
  magic_ptr->data->last_time = now; // Keep it updated so we don't reset the struct
  magic_ptr->magic = MAGIC;
  return magic_ptr->data;
}

// TODO: Don't run ASV before stable TV has been achieved once: recent_tv_error = interp(recent_tv_error, abs(recent_tv - tv), xxx)
// TODO: Safeguard against breath stacking: if ((te < avg_te * 0.7) && (residual_volume > peak_volume * 0.1f) ) { /* deliver no PS */ }
//       residual_volume is set at breath start

static float * const fvars = (void*) 0x2000e948;
static int * const ivars = (void*) 0x2000e750;

void start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];

  float epap = s_epap;
  float ips = s_ipap - s_epap;
  float eps = 1.5f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2a]; // This is set to epap+ps elsewhere, and likely does nothing here

  my_data_t * d = get_data();

  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = fvars[0x25] * delta; // Leak-compensated patient flow

  // Initialize new breath
  if (d->last_progress > progress + 0.25f) {
    // Don't adjust targets if it's a hyperpnea. Breath count hardcoded for ASV_INTERP=0.025f constant.
    // TODO: Instead of counting breaths, wait for average error to stabilize
    if (((d->peak_volume / d->recent_peak_volume) <= 1.25f) || d->breath_count < 60) {
      if ((d->duration > 2.0f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        for(int i=0; i<ASV_STEPS; i++) {
          // If it has zero volume, it means the breath was past its peak already, ignore it.
          if (d->current_volume[i] > 0.0f) {
            asv_interp(&d->recent_volume[i], d->current_volume[i]);
          }
          d->current_volume[i] = 0.0f;
        }
        asv_interp(&d->recent_peak_volume, d->peak_volume);
      }
    }
    asv_interp(&d->recent_duration, d->duration);
    if (d->recent_ips < ips) {
      d->recent_ips = ips;
    }
    d->recent_ips = interp(d->recent_ips, d->current_ips, 0.5f);
    d->current_ips = d->recent_ips;

    // TODO: Replace d->duration with d->te
    if ( ((d->volume / d->peak_volume) > 0.15f) && d->duration <= 2.0f ) {
      d->dont_support = 1;
    } else {
      d->dont_support = 0;
    }

    // TODO: d->dont_support = (d->volume / d->peak_volume) > 0.15f; // Also do TE-based calculation
    d->volume = 0.0f; // We just started a new breath, set volume to 0

    #if COUNT_PRETRIGGER_FLOW == 1
      for(int i=0; i<10; i++) { 
        if (d->last_flow[i] > 0.0f) {
          d->volume += d->last_flow[i];
        }
        d->last_flow[i] = 0.0f; 
      }
    #endif
    d->peak_volume = 0.0f;
    d->ticks = 0;
    d->breath_count += 1;
    d->duration = 0.0f;
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }
  d->volume += flow;
  #if COUNT_PRETRIGGER_FLOW == 1
    if (d->ticks != -1) {
      d->last_flow[d->ticks % 10] = flow;
    }
  #endif
  d->peak_volume = maxf(d->peak_volume, d->volume);
  d->duration += delta;

  // ASV operation
  if (d->volume >= d->peak_volume) {
    int i = d->ticks/ASV_ADJUST_EVERY - ASV_GRACE_PERIOD;
    
    if ((d->ticks % ASV_ADJUST_EVERY == 0) && i>=0 && i<ASV_STEPS) {
      d->current_volume[i] = d->volume;

      float gain = 8.0f * (ASV_ADJUST_EVERY * delta); // 8.0/s 
      if (i <= 3 || i >= 15 ) gain *= 0.5f;
      if (i <= 1) gain *= 0.25f;

      // recent = 500, current = 400; (500-400)/500=0.2
      float delta_ips = (d->recent_volume[i] - d->volume) / (d->recent_volume[i] +0.00001f); // Volume-independent version
      delta_ips = clamp(delta_ips, -0.5f, +0.5f); // Prevent anomalous values
      if (delta_ips >= 0.1f) {
        delta_ips = minf((ips + ASV_MAX_IPS) - d->current_ips, gain * (delta_ips-0.1f));
        // delta_ips = gain * (delta_ips-0.1f);
      } else if (delta_ips <= -0.3f) { // Hyperpnea damping
        delta_ips = maxf(0.75f * ips - d->current_ips, gain * delta_ips);
      } else if (delta_ips <= 0.06f) {
        // delta_ips = 0.5f * gain * (delta_ips-0.06f); // Reducing it should be slower
        delta_ips = maxf(minf(ips - d->current_ips, 0.0f), gain * (delta_ips-0.06f));
      } else {
        delta_ips = 0.0f;
      }
      // d->current_ips = clamp(d->current_ips + delta_ips, ips * 0.66f, ips+ASV_MAX_IPS+0.5f);
      d->current_ips = clamp(d->current_ips + delta_ips, ips, ips+ASV_MAX_IPS);
    }
  }


  // Allow myself to disable ASV during the night, if it disrupts my sleep after all
  if ((s_ipap - s_epap) > 2.9f) {
    ips = s_ipap - s_epap;
  } else {
    ips = d->current_ips;
    // (don't- I think it just contributes to expiratory intolerance) Reduce EPS proportionally to extra IPS
    // if (d->current_ips > (ips)) {
    //   eps = maxf(0, eps - (d->current_ips - ips)*0.5f );
    // }
  }

  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 10.0f * delta); // Max of 10cmH2O/s change
  } else {
    // Interpolate PS and EPAP
    if (progress <= 0.5f) { // Inhale
      *cmd_ps = minf(ips, *cmd_ps + ips * delta / 0.7f ); // Avoid mid-slope PS drops.
    } else { // Exhale
      float volumebased_mult = map01c(d->volume, d->peak_volume * 0.15f, d->peak_volume * 0.70f); // At >70% residual volume == 1, below 15% == 0
      *cmd_ps = interpmin(*cmd_ps, -eps * volumebased_mult, 0.06f, 3.0f * delta); // Convert to use dt: delta * 1cmH2O/s minimum // 0.075, 0.02 is ~300ms downslope. Marginally punchy
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, +ips + ASV_MAX_IPS);
  *cmd_epap = clamp(*cmd_epap, s_epap - 2, s_epap + 2);

  // Necessary to keep graphing code called(I think based on PS change, who cares)
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;

  return;
}

// int ramping = (fvars[0x2a] >= s_epap); // Might or might not work

/* // Retired alternative ASV operation code
  // Option B: Scale IPS relative to error
  float error = volume / recent_volume[i];
  if error >= 1.30f { // IPS: hyperpnea damping 
    current_ips = ips * map01c(error, 2.0f, 1.0f);
  } else if error <= 0.90f { // IPS: hypopnea boosting
    // FIXME: This approach is likely to over- or under- respond
    //        It should inst
    current_ips = ips + asv_ips * map01c(error, 1.0f, 0.5f); 
  } else { } // IPS: Return to baseline
// */

// typedef struct {
//   unsigned int is_keyword : 1;
//   unsigned int is_extern : 1;
//   unsigned int is_static : 1;
//   __fp16 test;
// } bitfield_t;