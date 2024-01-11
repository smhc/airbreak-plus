#include "common_code.h"
#include "stubs.h"

float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

typedef struct {
  unsigned magic;
  void** pointers;
} magic_ptr_t;

// At the very start of a segment of memory addresses not used by the device.
// Other candidates include: 0be0, 2978, 2980, 4560
magic_ptr_t * const magic_ptr = (void*) (0x20000be0); 
const unsigned MAGIC = 0x07E49001;

void *get_pointer(int index, int size) {
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->pointers = malloc(sizeof(void*) * (POINTERS_MAX+POINTERS_SPECIAL));
    magic_ptr->magic = MAGIC;
    for(int i=0; i<POINTERS_MAX+POINTERS_SPECIAL; i++) {
      magic_ptr->pointers[i] = 0;
    }
  }
  index += POINTERS_SPECIAL; // Special pointers have negative indices
  if (magic_ptr->pointers[index] == 0) {
    magic_ptr->pointers[index] = malloc(size);
  }
  return magic_ptr->pointers[index];
}


void init_history(history_t *hist) {
  for(int i=0; i<HISTORY_LENGTH; i++) {
    hist->flow[i] = 0.0f;
  }
  hist->tick = -1;
}

void update_history(history_t *hist) {
  const unsigned now = tim_read_tim5();
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was stopped and re-started.
  if ((now - hist->last_time) > 100000) { init_history(hist); }
  
  hist->last_time = now; // Keep it updated so we don't reset the struct
  hist->tick += 1;
  hist->flow[hist->tick % HISTORY_LENGTH] = *flow_compensated;
}

history_t *get_history() { 
  return get_pointer(-1, sizeof(history_t));

}

