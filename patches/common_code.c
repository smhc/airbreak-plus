#include "common_code.h"
#include "stubs.h"

float map(float s, float start, float end, float new_start, float new_end) {
  return new_start + map01(s, start, end) * (new_end - new_start);
}

float mapc(float s, float start, float end, float new_start, float new_end) {
  return clamp( map(s, start, end, new_start, new_end), new_start, new_end);
}

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

void *get_pointer(ptr_index index, int size, void (*init_fn)(void*)) {
  const int max_pointers = __PTR_LAST;
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->pointers = malloc(sizeof(void*) * max_pointers);
    magic_ptr->magic = MAGIC;
    for(int i=0; i<max_pointers; i++) {
      magic_ptr->pointers[i] = 0;
    }
  }
  if (magic_ptr->pointers[index] == 0) {
    magic_ptr->pointers[index] = malloc(size);
    init_fn(magic_ptr->pointers[index]);
  }
  return magic_ptr->pointers[index];
}


void init_history(history_t *hist) {
  for(int i=0; i<HISTORY_LENGTH; i++) {
    hist->flow[i] = 0.0f;
  }
  hist->tick = -1;
  hist->last_jitter = 0;
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
  return GET_PTR(PTR_HISTORY, history_t, init_history);
}


void apply_jitter(bool undo) {
  history_t *hist = get_history();
  if (undo) { // Get new jitter value
    hist->last_jitter *= -1;
  } else { // Undo the previous
    hist->last_jitter = 2 - (tim_read_tim5() % 5);
  }
  const float amtf = 0.005f * hist->last_jitter;
  *cmd_ps += amtf; *cmd_epap_ramp -= amtf;
}