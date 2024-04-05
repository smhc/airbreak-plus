#include "stubs.h"

const float RISE_TIME=0.2f;  // 1.6 * 0.4 = ~0.65s ramp
const float PS_lerp=0.07f; // 0.1f is ~0.25s and sharp but comfy, 0.05f feels a bit slow

void start(int param_1) {
  float * const fvars = (void*) 0x2000e948;
  int * const ivars = (void*) 0x2000e750;

  const float progress = fvars[0x20]; // 1.6s to 0.5, 4.5s to 1.0 
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];

  const float flow = fvars[0x25]; // Leak-compensated patient flow

  const float epap = s_epap;
  const float ips = s_ipap - s_epap;
  const float eps = 1.0f;

  float *cmd_ps = &fvars[0x29];
  float *cmd_epap = &fvars[0x28];
  float *cmd_ipap = &fvars[0x2d]; // This is probably set to epap+ps elsewhere, and likely does nothing here

  if (progress <= 0.5f) { // Inhale
    *cmd_epap = epap;
    if (progress <= RISE_TIME) {
      *cmd_ps = progress * (ips / RISE_TIME);
    } else {
      *cmd_ps = ips;
    }
  } else { // Exhale
    *cmd_ps = *cmd_ps * (1.0f-PS_lerp);
    *cmd_epap = epap;
    if (progress <= 0.600f) {
      *cmd_epap -= (progress - 0.5f) * 10.0f * eps;
    } else if (progress <= 0.700f) {
      *cmd_epap -= (0.70f - progress) * 10.0f * eps;
    }
  }

  // Could probably get away with much less of this(ps-=0.2 isn't enough tho), but...
  const float jitter = 0.02f - 0.04f * (tim_read_tim5() & 1);
  *cmd_ps += jitter; *cmd_epap += jitter * 2; *cmd_ipap += jitter*2;
  
  if (*cmd_epap < 0.0f) { *cmd_epap = 0.0f; }

  return;
}