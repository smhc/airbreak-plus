#include "stubs.h"
#include "common_code.h"

const float INSTANT_PS = 0.4f;
const float EPS = 1.2f;

typedef struct {
  float eps; // EPS (cmH2O) - used to prevent instant jumps in pressure in case of autotriggering
  float ips_fa; // Flow-Assist IPS (cmH2O) - currently used to augment pretrigger effort
} features_t;

STATIC void init_features(features_t *feat) {
  feat->eps = 0.0f;
  feat->ips_fa = 0.0f;
}

STATIC float get_delta_flow(history_t *hist, int bin_size) {
  const int t = hist->tick;
  if (t < 2*bin_size) { return 0.0f; }
  float avgf[3] = {0.0f, 0.0f, 0.0f}; // I don't think it overflows, but just in case it does, let's have padding.
  for (int i=0; i<2*bin_size; i++) {
    avgf[i/bin_size] += hist->flow[(t-i) % HISTORY_LENGTH];
  }
  return (avgf[0] - avgf[1]) / (float)(bin_size*bin_size);
}


// +1 pointer address: 0x000f93d0. Original function address: 0x080bc992
extern void pressure_limit_max_difference();

STATIC float reshape_ps1(float ps1, int exp, float perc) {
  return perc * (1.0f - pow(1.0f-ps1, exp)) + (1.0f-perc) * ps1;
}

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
  history_t *hist = get_history();
  update_history(hist);
  tracking_t *tr = get_tracking();
  update_tracking(tr);
  asv_data_t *asv = get_asv_data();
  update_asv_data(asv, tr);

  const float flow = *flow_compensated; // (L/min)
  const float dflow = get_delta_flow(hist, 4);
  const float flow2 = max(flow, flow + dflow*8.0f); // Flow extrapolated 80ms into the future

  features_t *feat = GET_PTR(PTR_FEATURES, features_t, init_features);

  apply_jitter(true);

  float dps = 0.0f;
  bool toggle = (ti_min <= 150);
  if (*therapy_mode == 3) {
    int t = hist->tick;
    const float ps = *cmd_ps + vauto_ps/2.0f;
    const float ps1 = (ps/vauto_ps); // 0.0 to 1.0

    if (tr->st_inhaling) {
      if (ps >= 0.01f) {
        float new_ps = ps;
        if (toggle) { // Disable if Ti min is set to above 0.1s
          float new_ps1 = reshape_vauto_ps(ps1, asv->asv_factor);
          new_ps = map(new_ps1, 0.0f, 1.0f, feat->eps, vauto_ps) - INSTANT_PS*ps1;
        } else {
          new_ps = map(reshape_ps1(ps1, 4, 0.2f), 0.0f, 1.0f, feat->eps, vauto_ps-INSTANT_PS) + INSTANT_PS;
          // new_ps = map(ps1, 0.0f, 1.0f, feat->eps, vauto_ps-INSTANT_PS) + INSTANT_PS;
        }
        dps = (new_ps - ps);
      }
      feat->ips_fa = 0.0f;
      feat->eps = min(feat->eps + 0.01f * EPS, 0.0f);

      asv->final_ips = max(asv->final_ips, ps + dps);
    } else { // Exhaling
      if (tr->current.ti >= 0.7f) {
        if (tr->st_just_started) { feat->eps = -EPS; }
        else {
          float new_eps = mapc(tr->current.volume / tr->current.volume_max, 0.6f, 0.15f, -EPS, 0.0f);
          feat->eps = max(feat->eps, new_eps);
        }
        // else { feat->eps += max(( (flow * 0.01f) / tr->current.volume_max), 0.01f / 2.5f) * EPS; feat->eps = max(feat->eps, 0.0f); } 
        // feat->eps = mapc(tr->current.volume / tr->current.volume_max, 0.6f, 0.15f, -EPS, 0.0f);
      }
      float new_ps1 = ps1*ps1 * 0.75f + 0.25f * ps1;
      float new_ps = map(new_ps1, 0.0f, 1.0f, feat->eps, vauto_ps);
      dps = (new_ps - ps);

      feat->ips_fa = mapc(flow2, 0.0f, 8.0f, 0.0f, 0.3f);
      dps += feat->ips_fa;
    }
  }

  const float orig_ps = *cmd_ps;
  *cmd_ps += dps;
  pressure_limit_max_difference(); // Execute the original function
  *cmd_ps = orig_ps;

  apply_jitter(false);
}
