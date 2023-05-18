#include "stubs.h"

// 0: early inhale (pressure up slope) ps->ips
//   -   0% of slope: Set IPS to max(ips, asv_ips)
//   -  50% of slope: Check volume1, adjust ips target (down if hyperpnea, up if >10% drop)
//   - 100% of slope: Check volume2, adjust ips target (up if >10% drop)
// 1: late inhale (pressure constant) ps==ips
// 2: early exhale (flow still dropping) - ps->eps
//   - end: Record peak eflow
// 3: late exhale (flow returning) - ps->0 (based on max exhflow->0 curve)
// 4: exhalatory pause - ps==0

#define CONSTANT_DELTA 0

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
const int ASV_STEPS = 20; // 20*5=1000ms window of adjustment
const float ASV_MAX_IPS = 2.0f;
// const char S_INHALE = 0;
// const char S_EXHALE = 1;
// const char S_PAUSE = 2;

static inline void asv_interp(float *value, float towards) {
  *value = interp(*value, towards, ASV_INTERP);
}

typedef struct {
  float last_progress;
  float volume;
  float peak_volume;
  unsigned last_time;

  float recent_duration;
  float duration;
  unsigned breath_count;
  int ticks; // Starts at 0, +1 each call

  // float ti; float te;
  // float recent_ti; recent_te;

  float recent_volume[20];
  float current_volume[20];
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
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was re-started.
  if ((magic_ptr->magic != MAGIC) || (now - magic_ptr->data->last_time) > 100000) {
    magic_ptr->data->last_progress = 0.0f;
    magic_ptr->data->volume = 0.0f;
    magic_ptr->data->peak_volume = 0.0f;
    magic_ptr->data->last_time = now;
    magic_ptr->data->recent_duration = 0.0f;
    magic_ptr->data->duration = 0.0f;
    magic_ptr->data->breath_count = 0;
    magic_ptr->data->ticks = -1; // Uninitialized
    magic_ptr->data->dont_support = 0;
    for(int i=0; i<20; i++) {
      magic_ptr->data->recent_volume[i] = 0.0f;
      magic_ptr->data->current_volume[i] = 0.0f;
    }
    magic_ptr->data->recent_ips = 0.0f;
    magic_ptr->data->current_ips = 0.0f;
  }
  magic_ptr->magic = MAGIC;
  return magic_ptr->data;
}



// TODO: Don't run ASV before stable TV has been achieved once: recent_tv_error = interp(recent_tv_error, abs(recent_tv - tv), xxx)
// TODO: Safeguard against breath stacking: if ((te < avg_te * 0.7) && (residual_volume > peak_volume * 0.1f) ) { /* deliver no PS */ }
//       residual_volume is set at breath start

//Agenda:
//    1. Graph deltatime, confirm if it's constant. If yes, dispense with calculating it.

//ASV: 25ms steps. Grace(0-50ms), Early ramp(50-350ms), late ramp(350-700ms), stable(700ms+)
//    Grace: Slower ramp, no adjustment
//    Early ramp: Dampen hyperpnea, compensate hypopnea
//    Late ramp: Compensate hypopnea
//    Stable: Slower compensate hypopnea


const float RISE_TIME=0.2f;  // 1.6 * 0.4 = ~0.65s ramp
const float PS_INTERP=0.07f; // 0.1f is ~0.25s and sharp but comfy, 0.05f feels a bit slow

static float * const fvars = (void*) 0x2000e948;
static int * const ivars = (void*) 0x2000e750;

void start(int param_1) {
  const float progress = fvars[0x20]; // 1.6s to 0.5, 4.5s to 1.0 
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];

  float epap = s_epap;
  float ips = s_ipap - s_epap;
  float eps = 1.0f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2a]; // This is probably set to epap+ps elsewhere, and likely does nothing here
  
  // int ramping = (fvars[0x2a] >= s_epap); // Might or might not work

  my_data_t * d = get_data();

  // Calculate deltatime in seconds
  // #if CONSTANT_DELTA == 1
    float delta = 0.010f; // It's 10ms +- 0.010ms, basically constant
    d->last_time = tim_read_tim5();
  // #else
  //   unsigned now = 
  //   float delta = (now - d->last_time) / 1000000.0f; // In seconds
  //   delta = clamp(delta, 0.0f, 0.05f); // Clamp it in case *last is uninitialized (disgusting hack tbh)
  //   d->last_time = now;
  // #endif

  const float flow = fvars[0x25] * delta; // Leak-compensated patient flow

  // Initialize new breath
  if (d->last_progress > progress + 0.25f) {
    if ((d->duration > 2.0f) && (d->ticks != -1) && (d->dont_support == 0) ) {
      for(int i=0; i<20; i++) {
        asv_interp(&d->recent_volume[i], d->current_volume[i]);
      }
    }
    asv_interp(&d->recent_duration, d->duration);
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
    d->peak_volume = 0.0f;
    d->ticks = 0;
    d->breath_count += 1;
    d->duration = 0.0f;
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }
  d->volume += flow;
  d->peak_volume = maxf(d->peak_volume, d->volume);
  d->duration += delta;

  // ASV operation
  if (d->volume >= d->peak_volume) {
    int i = d->ticks/ASV_ADJUST_EVERY - ASV_GRACE_PERIOD;
    
    if ((d->ticks % ASV_ADJUST_EVERY == 0) && i>=0 && i<ASV_STEPS) {
      d->current_volume[i] = d->volume;

      float gain = 40.0f;
      if (d->ticks <= 15 || d->ticks >= 75 ) gain *= 0.5f;
      if (d->ticks <= 5) gain *= 0.125f;

      // recent = 500, current = 400; (500-400)/500=0.2
      float delta_ips = (d->recent_volume[i] - d->volume) / (d->recent_volume[i] +0.00001f); // Volume-independent version
      if (delta_ips >= 0.1f) {
        delta_ips = delta * gain * (delta_ips-0.1f);
      } else if (delta_ips <= 0.0f) {
        delta_ips = 0.5f * delta * gain * delta_ips; // Reducing it should be slower
      } else {
        delta_ips = 0.0f;
      }
      d->current_ips = clamp(d->current_ips + delta_ips, ips, ips+ASV_MAX_IPS);

    }
  }
  ips = d->current_ips;

  // Allow myself to disable ASV during the night, if it disrupts my sleep after all
  if ((s_ipap - s_epap) > 2.9f) {
    ips = s_ipap - s_epap;
  }

  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 0.1f);
  } else {
    // Interpolate PS and EPAP
    if (progress <= 0.5f) { // Inhale
      *cmd_ps = minf(ips, *cmd_ps + ips * delta / 0.7f );
      // *cmd_ps = clamp(*cmd_ps + ips * delta / 0.65f, *cmd_ps, ips); // Hacky way to prevent mid-slope PS drops
    } else { // Exhale
      float volumebased_mult = map01c(d->volume, d->peak_volume * 0.15f, d->peak_volume * 0.80f); // At >80% residual volume == 1, below 15% == 0
      *cmd_ps = interpmin(*cmd_ps, -eps * volumebased_mult, 0.06f, 0.02f); // Convert to use dt: delta * 1cmH2O/s minimum // 0.075, 0.02 is ~300ms downslope. Marginally punchy
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, +ips + ASV_MAX_IPS); // Just in case
  *cmd_epap = clamp(*cmd_epap, s_epap - 2, s_epap + 2);

  // Could probably get away with much less of this(ps-=0.2 isn't enough tho), but...
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;

  return;
}

/* // TODO ASV code and comments
    // With this, 50% drop = +0.5f, 50% excess = -0.5f;
  // float error = volume / d->recent_volume[i];
  // float delta_ips = delta * 0.2f * (0.9f * d->recent_volume[i] - volume); // Volume-dependent version
  
  // TODO: Implement hyperpnea compensation
  // if (volume > d->recent_volume[i] * 1.3f) {
  //   d->current_ips = clamp(d->current_ips + delta_ips, ips * 0.666f, ips+ASV_MAX_IPS);
  // } else {
*/

/* // Retired alternative ASV operation code
  // Option B: Scale IPS relative to error (bad)
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