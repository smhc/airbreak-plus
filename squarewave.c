
#include "stubs.h"
#include "state.h"

// About 1.6s to reach 0.5 (once 1.6s ??)
// About 4.5s to reach 1.0

void start(int param_1) {

  float * const fvars = (void*) 0x2000e948;
  int * const ivars = (void*) 0x2000e750;

  float stage = fvars[0x20];
  // float target_ps = fvars[0x29];
  // float target_epap = fvars[0x28];
  
  // therapy_variable_get_float(&stage,0x20);
  // therapy_variable_get_float(&target_ps,0x29);
  // therapy_variable_get_float(&target_epap,0x28);

  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];
  const float s_ps = s_ipap - s_epap;
  // therapy_variable_get_float(&s_ipap,0xe);
  // therapy_variable_get_float(&s_epap,0xf);

  // if (stage > 0.5001) {

  // 1.6 * 0.4 = 0.65s ramp 
  if (stage <= 0.2f) {
    fvars[0x28] = s_epap;
    fvars[0x29] = stage * 5.0f * s_ps;
  } else if (stage <= 0.5f) {
    fvars[0x29] = s_ps;
  } else {
    fvars[0x29] = fvars[0x29] * 0.90f;
    // EPR that lasts ~1.5s
    if (stage <= 0.625f) {
      fvars[0x28] = s_epap - (stage - 0.5f) * 8.0f;
    } else if (stage <= 0.750f) {
      fvars[0x28] = s_epap - (0.75f - stage) * 8.0f;
    } else {
      fvars[0x28] = s_epap;
    }
  }
  
  // and because we need to keep the drawing routine constantly called,
  // add a small value periodically.
  if (now & 1)
    smooth_target += 0.01f;
  else
    smooth_target -= 0.01f;

  /* // Original test. Ramp up then down
  if (fvars[0x20] > 0.5001f) {
    // fvars[0x29] = 0.0f;
    fvars[0x29] = (1 - stage) * 2.0f * (s_ipap - s_epap);
  } else {
    fvars[0x29] = stage * 2.0f * (s_ipap - s_epap);
  }
  // */ 

  return;
}