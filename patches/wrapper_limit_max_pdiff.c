#include "stubs.h"
#include "common_code.h"

// +1 pointer address: 0x000f93d0
// Original function address: 0x080bc992

extern void pressure_limit_max_difference();

void MAIN start() {
  const float flow = *flow_compensated / 60.0f; // (L/s)
  float dps = 0.0f;

  history_t *hist = get_history();
  update_history(hist);

  int t = hist->tick;
  float delta_flow = 0.0f;
  // Calculate the delta-flow between flow[-0 to -40ms] and flow[-50 to -80ms]
  const int BIN_S = 4;
  if (t >= 2*BIN_S) {
    float avgf[2] = {0.0f, 0.0f};
    for (int i=0; i<2*BIN_S; i++) {
      avgf[i/BIN_S] += hist->flow[(t-i) % HISTORY_LENGTH];
    }
    delta_flow = (avgf[0] - avgf[1]) / (BIN_S * 1.0f);
  }

  // TODO: It might be better to do max() during positive flow, and min() during negative, maybe?
  float predicted_flow = flow + 4.0f * (delta_flow/60.0f); // 4 * 40ms bins = 160ms into the future
  predicted_flow = max(predicted_flow, flow); // Don't overzealously predict drops in flow, since this is often collapse and not expiration.

  if (*therapy_mode == 3) {
    if (flow < -0.04f) {
      dps = -0.2f * (*cmd_ps) + (flow+0.04f)*3.0f;
    }
    if (flow > 0.02f) {
      dps = predicted_flow * 1.0f;
      if (*cmd_ps > 0.005f) { dps += 0.15f; }
    }

    dps = clamp(dps, -2.0f, 2.0f); // Just in case I screw up somehow
  }

  const float orig_ps = *cmd_ps;
  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;
}
