#include "stubs.h"
#include "common_code.h"

const float INSTANT_PS = 0.4f;

// +1 pointer address: 0x000f93d0. Original function address: 0x080bc992
extern void pressure_limit_max_difference();

// Reshapes PS in 0.0-1.0 format to differently shaped slopes with `mult` times the AUC, first increasing slope before magnitude
// Only using ^4 shape, because going to ^8 and above is very jarring and results in bad premature cycling
STATIC float reshape_vauto_ps(float ps1, float mult) {
  // ^2 - 1.330, ^6 - 1.707, ^8 - 1.770
  float ps4 = 1.0f - pow(1.0f - ps1, 4);  // ~1.594x the AUC
  if (mult <= 1.0) { 
    return ps1; 
  } else if ((mult > 1.0) && (mult <= 2.5)) {
    return map(mult, 1.0f, 2.5f, ps1 * 1.0f, ps4 * 1.594f);
  } else {
    return ps4 * (mult / 1.594f);
  }

  return ps1;
}

void MAIN start() {
  const float flow = *flow_compensated / 60.0f; // (L/s)
  history_t *hist = get_history();
  update_history(hist);
  tracking_t *tr = get_tracking();
  update_tracking(tr);
  asv_data_t *asv = get_asv_data();
  update_asv_data(asv, tr);

  apply_jitter(true);

  float dps = 0.0f;
  bool toggle = (ti_min <= 150);
  if (*therapy_mode == 3) {
    int t = hist->tick;
    const float ps = *cmd_ps + vauto_ps/2.0f;
    const float ps1 = (ps/vauto_ps); // 0.0 to 1.0

    if ( tr->st_inhaling && (ps >= 0.01f) ) {
      dps += INSTANT_PS;

      if (toggle) { // Disable if Ti min is set to above 0.1s
        float new_ps1 = reshape_vauto_ps(ps1, asv->asv_factor);
        dps += (new_ps1 - ps1) * (vauto_ps - INSTANT_PS);
      } else {
        dps -= ps1 * INSTANT_PS;
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
    }
  }

  const float orig_ps = *cmd_ps;
  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;

  apply_jitter(false);
}
