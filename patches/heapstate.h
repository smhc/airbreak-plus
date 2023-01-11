#ifndef _heapstate_h_
#define _heapstate_h_

#include "stubs.h"

typedef struct {
  float test;
  // float peak_inhale;
  // float peak_exhale;
  // float breath_slope;
  // float breath_volume;
  // float recent_peak_inhale;
  // float recent_peak_exhale;
  // float recent_breath_slope;
  // float recent_breath_volume;
} heapstate_t;

// Will try using 0x20000BE8 (near end of first free area) and hope nothing overwrites it. Or 66C0

heapstate_t * get_heapstate() {
  union { heapstate_t *ptr; float num; } * my_thing;
  my_thing = (void*) ( 0x2000e948 + 4 * 0x21);
  if (my_thing->num == 0.0f) {
    my_thing->ptr = malloc(sizeof(heapstate_t));
    my_thing->ptr->test = 0.0f;
  }
  return my_thing->ptr;
}


// heapstate_t * get_heapstate() {
//   void * fvars_0x21 = (void*) ( 0x2000e948 + 4 * 0x21);
//   float * f_fvars_0x21 = (float*) ( 0x2000e948 + 4 * 0x21);
//   // float * const fvars = (void*) 0x2000e948;
//   if (*(float *)&fvars_0x21 == 0.0f) {
//   // if (*f_fvars_0x21 == 0.0f) {
//     fvars_0x21 = malloc(sizeof(heapstate_t));
//   }
//   return (heapstate_t*)(fvars_0x21);
// }

#endif