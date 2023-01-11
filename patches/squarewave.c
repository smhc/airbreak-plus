#include "stubs.h"
// #include "heapstate.h"

#define EPS_TEST 1

// 0: early inhale (pressure up slope) ps->ips
//   -   0% of slope: Set IPS to max(ips, asv_ips)
//   -  50% of slope: Check volume1, adjust ips target (down if hyperpnea, up if >10% drop)
//   - 100% of slope: Check volume2, adjust ips target (up if >10% drop)
// 1: late inhale (pressure constant) ps==ips
// 2: early exhale (flow still dropping) - ps->eps
//   - end: Record peak eflow
// 3: late exhale (flow returning) - ps->0 (based on max exhflow->0 curve)
// 4: exhalatory pause - ps==0

inline float max(float a, float b) {
  if (a >= b) { return a; }
  else { return b; }
}

inline float min(float a, float b) {
  if (a < b) { return a; }
  else { return b; }
}

inline float clamp(float a, float _min, float _max) {
  return max(min(a, _max), _min);
}

inline float interp(float from, float to, float coeff) {
   return (1.0f-coeff)*from + coeff*to;
}

const float RISE_TIME=0.2f;  // 1.6 * 0.4 = ~0.65s ramp
const float PS_INTERP=0.075f; // 0.1f is ~0.25s and sharp but comfy, 0.05f feels a bit slow

void start(int param_1) {
  float * const fvars = (void*) 0x2000e948;
  int * const ivars = (void*) 0x2000e750;

  const float progress = fvars[0x20]; // 1.6s to 0.5, 4.5s to 1.0 
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];

  const float flow = fvars[0x25]; // Leak-compensated patient flow

  const float epap = s_epap;
  const float ips = s_ipap - s_epap;
  const float eps = 1.2f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2d]; // This is probably set to epap+ps elsewhere, and likely does nothing here

  float *v_ps = &fvars[0x10];
  float *v_peak_exhale = &fvars[0x11];
  float *v_exhale_done = &fvars[0x12];

  if (progress <= 0.5f) { // Inhale
    *cmd_epap = epap;
    if (progress <= RISE_TIME) {
      *cmd_ps = progress * (ips / RISE_TIME);
    } else {
      *cmd_ps = ips;
    }
    #if EPS_TEST == 1
      *v_peak_exhale = 0.0f;
      *v_ps = *cmd_ps;
    #endif
  } else { // Exhale
    #if EPS_TEST == 1
      *v_ps = interp(*v_ps, 0.0f, 0.075f);
      *v_peak_exhale = min(*v_peak_exhale, flow);

      // Crude hack for now. Minimum of constant slope and the thing
      float target_eps_a = 0.0f;
      float target_eps_b = clamp((flow / *v_peak_exhale - 0.10f), 0.0f, 1.0f) / 0.9f; 
      if (progress <= 0.600f) {
        target_eps_a = (progress - 0.5f) / 0.1f;
      } else if (progress <= 0.750f) {
        target_eps_a = (0.750f - progress) / 0.15f;
      }

      *cmd_epap = epap;
      *cmd_ps = *v_ps - eps * min(target_eps_a, target_eps_b);
    #else
      *cmd_ps = interp(*cmd_ps, 0, 0.075f);
      *cmd_epap = epap;
      if (progress <= 0.625f) {
        *cmd_epap -= (progress - 0.5f) * 8.0f * eps;
      } else if (progress <= 0.750f) {
        *cmd_epap -= (0.75f - progress) * 8.0f * eps;
      }
    #endif
  }

  // Could probably get away with much less of this(ps-=0.2 isn't enough tho), but...
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;
  
  *cmd_epap = max(*cmd_epap, 0.0f);

  return;
}




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