#include "stubs.h"
#include "common_code.h"

// +1 pointer address: 0x000f93d0
// Original function address: 0x080bc992

extern void pressure_limit_max_difference();

void start() {
  const float flow = *flow_compensated / 60.0f; // (L/s)
  float dps = 0.0f;

  if (*therapy_mode == 3) {
    if (flow < -0.04f) {
      dps = -0.2f * (*cmd_ps) + (flow+0.04f)*3.0f;
    }
    if ((flow > 0.02f) && (*cmd_ps > 0.005f)) {
      dps = 0.1f + flow * 0.75f;
    }
  }

  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps -= dps;
}

// TODO: Consider using history and setting IPS FA to flow extrapolated 120ms into the future.
/*
typedef struct {
  float16 flow[HISTORY_LENGTH];
} history_t;

INLINE void init_history(history_t *hist) {
  for(int i=0; i<HISTORY_LENGTH; i++) {
    hist->flow[i] = 0.0f;
  }
}

if (d->ticks != -1) {
  d->history.flow[d->ticks % 10] = flow;
}
*/