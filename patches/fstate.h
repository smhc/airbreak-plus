#ifndef _fstate_h_
#define _fstate_h_

// #include "stubs.h"

// I can have up to 16 floats, they are not written/used in S mode.
typedef struct {
  float peak_exhale_current;
  float peak_exhale_recent;
  // float recent_slopevol;
  // float current_volume;
  // float asv_ps;
  float last_0x20;
  unsigned magic;
  char bstage 
} fstate_t;

const unsigned MAGIC = 0x07E49001;
fstate_t * get_fstate() {
  fstate_t * fstate = (void*) (0x2000e948 + 4*0x10);
  if (fstate->magic != MAGIC) {
    fstate->peak_exhale_recent  = 0.0f;
    fstate->peak_exhale_current = 0.0f;
    // fstate->recent_slopevol = 0.0f;
    // fstate->current_volume = 0.0f;
    // fstate->asv_ps = 0.0f;
    fstate->last_0x20 = 0.0f;
    fstate->magic = MAGIC;
  }
  return fstate;
}

#endif