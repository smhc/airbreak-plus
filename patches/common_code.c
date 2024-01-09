#include "common_code.h"
#include "stubs.h"

float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

const int MAX_POINTERS = 8;

typedef struct {
  unsigned magic;
  void** pointers;
} magic_ptr_t;

magic_ptr_t * const magic_ptr = (void*) (0x20000be0);
const unsigned MAGIC = 0x07E49001;

void *get_pointer(int index, int size) {
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->pointers = malloc(sizeof(void*) * MAX_POINTERS);
    magic_ptr->magic = MAGIC;
    for(int i=0; i<MAX_POINTERS; i++) {
      magic_ptr->pointers[i] = 0;
    }
  }

  if (magic_ptr->pointers[index] == 0) {
    magic_ptr->pointers[index] = malloc(size);
  }

  return magic_ptr->pointers[index];
}

// 0be0, 2978, 2980, 4560
