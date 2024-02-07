#include "stubs.h"
#include "common_code.h"

#define JITTER 1
const float INSTANT_PS = 0.4f;
// const float IPS_FA = 0.0f; // Set to 0 to disable
// const float EPS_FA = 0.0f; // Set to 0 to disable

// +1 pointer address: 0x000f93d0. Original function address: 0x080bc992
extern void pressure_limit_max_difference();


STATIC float reshape_vauto_ps(float ps, float mult) {
  float a = 1.0f - ps;
  float b = pow(a, 8); // 1.59x the AUC
  if (mult <= 1.0) { 
    return ps; 
  } else if ((mult > 1.0) && (mult <= 2.0)) {
    return map(mult, 1.0f, 2.0f, (1.0f-a) * 1.0f, (1.0f-b) * 1.25f);
  } else if (mult <= 3.0) {
    b = pow(a, 16); // 1.87x the AUC
    a = pow(a, 8);
    return map(mult, 2.0f, 3.0f, (1.0f-a) * 1.25f, (1.0f-b) * 1.6f);
  } else {
    return (1.0f - pow(a, 16)) * (mult / 1.87f);
  }

  return ps;
}



void MAIN start() {
  const float flow = *flow_compensated / 60.0f; // (L/s)
  history_t *hist = get_history();
  update_history(hist);
  tracking_t *tr = get_tracking();
  update_tracking(tr);
  asv_data_t *asv = get_asv_data();
  update_asv_data(asv, tr);

  #if JITTER == 1
    apply_jitter(true);
  #endif

  float dps = 0.0f;
  bool toggle = (ti_min <= 150);
  if (*therapy_mode == 3) {
    int t = hist->tick;
    /*
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
    */

    const float ps = *cmd_ps + vauto_ps/2.0f;
    const float ps1 = (ps/vauto_ps); // 0.0 to 1.0

    if ( tr->st_inhaling && (ps >= 0.01f) ) {
      dps += INSTANT_PS;
      // dps += (predicted_flow-0.05f) * IPS_FA;

      if (toggle) { // Disable if Ti min is set to above 0.1s
        float new_ps1 = reshape_vauto_ps(ps1, asv->asv_factor);
        dps += (new_ps1 - ps1) * vauto_ps;
      }
      asv->final_ips = max(asv->final_ips, ps + dps);
    } 
    if (!tr->st_inhaling) { // Exhaling
      if (toggle) {
        const float new_ps = (ps1*ps1) * asv->final_ips * 0.75f + ps1 * asv->final_ips * 0.25f;
        dps = (new_ps - ps);
      } else {
        dps += (ps1*ps1 - ps1) * vauto_ps * 0.75f;
      }

      // if (flow < -0.1f) dps += (flow+0.1f) * EPS_FA;
    }

    // dps = clamp(dps, -vauto_ps, vauto_ps); // Just in case I screw up somehow
  }

  const float orig_ps = *cmd_ps;
  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;

  #if JITTER == 1
    apply_jitter(false);
  #endif
}
