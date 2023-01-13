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

#define STRUCT_TEST 1
#define HALFP_TEST 0
#define ASV_TEST 0
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

typedef struct {
  float last_progress;
  #if HALFP_TEST == 1
    __fp16 volume;
    __fp16 peak_volume;
  #else
    float volume;
    float peak_volume;
  #endif
  unsigned last_time;
  #if ASV_TEST == 1
    // float 
  #endif
} my_data_t;

typedef struct {
  unsigned magic;
  my_data_t * data_ptr;
} magic_ptr_t;

static magic_ptr_t * const magic_ptr = (void*) (0x2000e948 + 0x11*4);

const unsigned MAGIC = 0x07E49001;
static inline my_data_t * get_data() {
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->magic = MAGIC;
    magic_ptr->data_ptr = malloc(sizeof(my_data_t));
    magic_ptr->data_ptr->volume = 0.0f;
    magic_ptr->data_ptr->peak_volume = 0.0f;
    magic_ptr->data_ptr->last_progress = 0.0f;
    magic_ptr->data_ptr->last_time = tim_read_tim5();
  }
  return magic_ptr->data_ptr;
}



// TODO: Don't run ASV before stable TV has been achieved once: recent_tv_error = interp(recent_tv_error, abs(recent_tv - tv), xxx)
// TODO: Safeguard against breath stacking: if ((te < avg_te * 0.7) && (residual_volume > peak_volume * 0.1f) ) { /* deliver no PS */ }
//       residual_volume is set at breath start
/*
void asv_operation(float delta) {
  float target_volume[20];
  float current_volume[20];
  int breath_count = 0;
  float current_ips = *cmd_ps;

  if (volume > peak_volume) {
    if (timesteps % checkpoint == 0) {
      unsigned i = timesteps/checkpoint;
      current_volume[i] = volume;

      float gain_mult = 1.0f;
      if (breath_time <= 0.15f || breath_time >= 0.7f) gain_mult = 0.5f;
      if (breath_time <= 0.05f) gain_mult = 0.125f;

      #if 1 // Option A: Adjust IPS to meet target (similar to Resmed algo)
        float delta_ips = delta * 100.0f * (0.9f * target_volume[i] - volume) / target_volume[i]; // Volume-independent version
          // With this, 50% drop = -0.5f, 50% excess = +0.5f;
        // float delta_ips = delta * 0.2f * (0.9f * target_volume[i] - volume); // Volume-dependent version
        float error = volume / target_volume[i];
        
        // if (errror <= 1.4f)
        current_ips = clamp(current_ips + delta_ips, ips, ips+asv_ips);

      #else // Option B: Scale IPS relative to error (bad)
        float error = volume / target_volume[i];
        if error >= 1.30f { // IPS: hyperpnea damping 
          current_ips = ips * map01c(error, 2.0f, 1.0f);
        } else if error <= 0.90f { // IPS: hypopnea boosting
          // FIXME: This approach is likely to over- or under- respond
          //        It should inst
          current_ips = ips + asv_ips * map01c(error, 1.0f, 0.5f); 
        } else { } // IPS: Return to baseline
      #endif
    }
  }

  if (breath_finished) {
    if duration > 0.7 {
      for(unsigned i=0; i<20; i++) {
        target_volume[i] = interp(target_volume[i], current_volume[i], ASV_TARGET_INTERP);
      }
    }
  }
}
// */

//Agenda:
//    1. Graph deltatime, confirm if it's constant. If yes, dispense with calculating it.

//ASV: 25ms steps. Grace(0-50ms), Early ramp(50-350ms), late ramp(350-700ms), stable(700ms+)
//    Grace: Slower ramp, no adjustment
//    Early ramp: Dampen hyperpnea, compensate hypopnea
//    Late ramp: Compensate hypopnea
//    Stable: Slower compensate hypopnea

// float target_volumes[40]

// TODO: Start using deltatime


// void motor_command_shutdown(int param_1) { fvars[0x2d] = 0.0f; }
// void motor_command_write__0x7ffffd4(void) { fvars[0x2d] = *cmd_ipap - *cmd_ps; }

const float RISE_TIME=0.2f;  // 1.6 * 0.4 = ~0.65s ramp
const float PS_INTERP=0.07f; // 0.1f is ~0.25s and sharp but comfy, 0.05f feels a bit slow
const float ASV_TARGET_INTERP=0.025; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45

void start(int param_1) {
  float * const fvars = (void*) 0x2000e948;
  int * const ivars = (void*) 0x2000e750;

  const float progress = fvars[0x20]; // 1.6s to 0.5, 4.5s to 1.0 
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];


  const float epap = s_epap;
  const float ips = s_ipap - s_epap;
  const float eps = 1.5f;

  char ramping = (fvars[0x2a] >= s_epap); // Might or might not work

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2a]; // This is probably set to epap+ps elsewhere, and likely does nothing here

  // float *v_peak_exhale = &fvars[0x11];
  // float *v_ps = &fvars[0x10];
  my_data_t * data = get_data();
  #if HALFP_TEST == 1
    __fp16 *v_peak_volume = &data->peak_volume;
    __fp16 *v_volume = &data->volume;
  #else
    float *v_peak_volume = &data->peak_volume;
    float *v_volume = &data->volume;
  #endif
  unsigned *last = &data->last_time;
  float *v_last_progress = &data->last_progress;

  // Calculate deltatime in seconds
  #if CONSTANT_DELTA == 1
    float delta = 0.010f; // It's 10ms +- 0.010ms, basically constant
  #else
    unsigned now = tim_read_tim5();
    float delta = (now - *last) / 1000000.0f; // In seconds
    delta = clamp(delta, 0.0f, 0.05f); // Clamp it in case *last is uninitialized (disgusting hack tbh)
    *last = now;
  #endif

  fvars[0x16] = delta; // Write delta so I can graph it

  const float flow = fvars[0x25] * delta; // Leak-compensated patient flow

  if (*v_last_progress > progress + 0.25f) {
    // *v_dont_support = (*v_volume / *v_peak_volume) > 0.15f; // If there's too much residual volume, do not support this "breath"
    *v_volume = 0.0f; // We just started a new breath, set volume to 0
    *v_peak_volume = 0.0f;
    // *cmd_ps = 0.0f;
  } else {
    *v_volume += flow;
  }
  *v_peak_volume = maxf(*v_peak_volume, *v_volume);

  *cmd_epap = epap;
  if (progress <= 0.5f) { // Inhale
    *cmd_ps = minf(ips, *cmd_ps + ips * delta / 0.65f );

    // *cmd_ps = interpmin(*cmd_ps, ips, 0.03f, 0.01f); // Gradual interp. Too fast early, too slow late :-/
    // if (progress <= RISE_TIME) {
    //   *cmd_ps = progress * (ips / RISE_TIME);
    // } else { *cmd_ps = ips; }
  } else { // Exhale
    // EPS from 80% volume to 15% volume 
    float volumebased_mult = map01c(*v_volume, *v_peak_volume * 0.15f, *v_peak_volume * 0.80f); // At >80% residual volume == 1, below 15% == 0
    // 0.075, 0.02 is ~300ms downslope. Marginally punchy
    *cmd_ps = interpmin(*cmd_ps, -eps * volumebased_mult, 0.06f, 0.02f); // Convert to use dt: delta * 1cmH2O/s minimum
  }
  *v_last_progress = progress;

  *cmd_ps = clamp(*cmd_ps, -eps, +ips); // Just in case
  if (*cmd_epap < 0.0f) { *cmd_epap = 0.0f; }

  // Could probably get away with much less of this(ps-=0.2 isn't enough tho), but...
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;

  return;
}


/* Retired, non-struct code
  #else
    const float ips = s_ipap - s_epap;
    float *v_peak_volume = &fvars[0x11];
    float *v_volume = &fvars[0x12];
    float *v_last_progress = &fvars[0x13];
    unsigned *last = (void*) ( 0x2000e948 + 0x14*4);
  #endif
*/


// typedef struct {
//   unsigned int is_keyword : 1;
//   unsigned int is_extern : 1;
//   unsigned int is_static : 1;
//   __fp16 test;
// } bitfield_t;

// my_fvars_t * get_stuff() {
  
//   return stuff;
// }


// heapstate_t * ptr = get_heapstate();
// if (ptr->test == 0.0f) {
//   ptr->test = s_ipap - s_epap;
// }
// const float s_ps = ptr->test;

// if (fvars[0x18] > 0.0f) {
//   fvars[0x19] = fvars[0x18];
//   fvars[0x18] = 0.0f;
// }