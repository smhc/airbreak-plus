#include "stubs.h"
// #include "heapstate.h"
// #include "fstate.h"

// inline float map01(float s, float start, float end) {
//   return (s - start)/(end-start);
// }

// I can have up to 16 floats, they are not written/used in S mode.
// typedef struct {
//   float recent_slopevol_1;
//   float recent_slopevol_2;
//   float current_slopevol;
//   float asv_ps;
//   float last_0x20;
//   byte stage; // Inhaling/exhaling/pause
//   unsigned magic;
// } my_fvars_t;
// const unsigned MAGIC = 0x07E49001;

inline float max(float a, float b) {
  if (a >= b) { return a; }
  else { return b; }
}

inline float interp(float from, float to, float coeff) {
   return (1.0f-coeff)*from + coeff*to;
}

const float RISE_TIME=0.2f;  // 1.6 * 0.4 = ~0.65s ramp
const float PS_DECAY=0.925f; // 0.9f is 0.25s and sharp but comfy, 0.95f feels a bit slow
const float EPR=1.0f;        // 1.0cmH2O (lasts ~1.5s)

void start(int param_1) {
  float * const fvars = (void*) 0x2000e948;
  int * const ivars = (void*) 0x2000e750;

  const float stage = fvars[0x20]; // 1.6s to 0.5, 4.5s to 1.0 
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];

  const float epap = fvars[0x2a]; // Current EPAP including ramp, etc.
  const float ips = s_ipap - s_epap;
  const float eps = 1.0f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];

  // my_fvars_t * stuff = (void*) (0x2000e948 + 4*0x10);
  // if (stuff->magic != MAGIC) {
  //   stuff->recent_slopevol = 0.0f;
  //   stuff->current_slopevol = 0.0f;
  //   stuff->asv_ps = s_ps;
  //   stuff->last_0x20 = 0.0f;
  //   stuff->magic = MAGIC;
  // }


  if (stage <= 0.5f) { // Inhale
    if (stage <= RISE_TIME) {
      *cmd_epap = epap;
      *cmd_ps = stage * (ips / RISE_TIME);
    } else {
      *cmd_ps = ips;
    } 
  } else { // Exhale
    // *cmd_ps = interp(*cmd_ps, 0, 0.075f);
    *cmd_ps = (*cmd_ps) * PS_DECAY;
    if (stage <= 0.625f) {
      *cmd_epap = epap - (stage - 0.5f) * 8.0f * ips;
    } else if (stage <= 0.750f) {
      *cmd_epap = epap - (0.75f - stage) * 8.0f * ips;
    } else {
      *cmd_epap = epap;
    }
  }


  // Could probably get away with much less of this(ps-=0.2 isn't enough tho), but...
  const float jitter = 0.02f;
  if (tim_read_tim5() & 1) {
      *cmd_ps -= jitter; *cmd_epap += jitter; fvars[0x2d] += jitter*2;
  } else {
      *cmd_ps -= jitter; *cmd_epap -= jitter; fvars[0x2d] -= jitter*2;
  }
  
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