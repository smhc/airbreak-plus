#include "stubs.h"
#include "common_code.h"

#define JITTER 1
const float INSTANT_PS = 0.4f;
const float IPS_FA = 0.0f; // Set to 0 to disable
const float EPS_FA = 0.0f; // Set to 0 to disable

// +1 pointer address: 0x000f93d0. Original function address: 0x080bc992
extern void pressure_limit_max_difference();

/*
STATIC float reshape_vauto_ps(float ps, float mult) {
  float a = ps;
  float b = a*a*a*a; b *= b; // (a**8), 1.59x the AUC
  if (mult <= 1.0) { return a; }
  else if ((mult > 1.0) && (mult <= 2.0)) {
    float a_vs_b = map(mult, 1.0f, 2.0f, 0.0f, 1.0f);
    float mult_by = map(mult, 1.0f, 2.0f, 1.0f, 1.25f);
    return (a*(1.0f-a_vs_b) + b*a_vs_b) * mult_by;
  } else if ((mult >= 2.0) && (mult <= 3.0)) {
    a = b; b*=b; b*=b; // (a**16), 1.87x the AUC
    float a_vs_b = map(mult, 2.0f, 3.0f, 0.0f, 1.0f);
    float mult_by = map(mult, 2.0f, 3.0f, 1.25f, 1.6);
    return (a*(1.0f-a_vs_b) + b*a_vs_b) * mult_by;
  } else {
    a = b*b;
  }
}
*/


void MAIN start() {
  const float flow = *flow_compensated / 60.0f; // (L/s)
  history_t *hist = get_history();
  update_history(hist);

  #if JITTER == 1
    apply_jitter(true);
  #endif

  float dps = 0.0f;
  if (*therapy_mode == 3) {
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

    const float ps = *cmd_ps + vauto_ps/2.0f;
    const float ps1 = (ps/vauto_ps); // 0.0 to 1.0

    if ((breath_progress <= 0.5f) && (breath_progress >= 0.01f)) {
      dps += INSTANT_PS - ps*(INSTANT_PS/vauto_ps);
      dps += (predicted_flow-0.05f) * IPS_FA;
    } 
    if (breath_progress > 0.5f) { // Exhaling
      dps += (ps1*ps1 - ps1) * vauto_ps * 0.75f; // 75% of the way from base downslope to squared downslope
      if (flow < -0.1f) dps += (flow+0.1f) * EPS_FA;
    }

    dps = clamp(dps, -2.0f, 2.0f); // Just in case I screw up somehow
  }

  const float orig_ps = *cmd_ps;
  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;

  #if JITTER == 1
    apply_jitter(false);
  #endif
}
