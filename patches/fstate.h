#ifndef _fstate_h_
#define _fstate_h_

#include "stubs.h"

// I can have up to 16 floats, they are not written/used in S mode.
typedef struct {
  float recent_slopevol;
  float current_slopevol;
  float asv_ps;
  float last_0x20;
  unsigned magic;
} my_fvars_t;

const unsigned MAGIC = 0x07E49001;
my_fvars_t * get_stuff() {
  my_fvars_t * stuff = (void*) (0x2000e948 + 4*0x10);
  if (stuff->magic != MAGIC) {
    stuff->recent_slopevol = 0.0f;
    stuff->current_slopevol = 0.0f;
    stuff->asv_ps = 0.0f;
    stuff->last_0x20 = 0.0f;
    stuff->magic = MAGIC;
  }
  return stuff;
}

#endif