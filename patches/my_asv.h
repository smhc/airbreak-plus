#ifndef _my_asv_h_
#define _my_asv_h_

/////////////////////////
// PID controller code //

// #define ASV_STEP_COUNT 20

#define ASV_STEP_LENGTH 5 // (10ms ticks)
#define ASV_STEP_COUNT 20 // (steps)
#define ASV_STEP_SKIP 1 // (steps)

typedef struct {
  float last_error;
  float cumulative_error;
  float current_error;
  float kp;
  float ki;
  float kd;
  float output_min;
  float output_max;
} pid_t;

void pid_init(pid_t *pid, float p, float i, float d, float _min, float _max);
float pid_get_signal_unclamped(pid_t *pid);
float pid_get_signal(pid_t *pid);
void pid_update(pid_t *pid, float current_error);

////////////////////////
// ASV algorithm code //

typedef struct {
  uint32 last_time;
  uint32 breath_count;

  int16 ticks; // Starts at 0, +1 each call
  bool asv_disable;

  pid_t pid;
  float asv_factor;
  float final_ips; // Final max IPS value, used to maintain correct downslope

  breath_t recent;

  // float16 volumes[60];
  float16 targets_recent[ASV_STEP_COUNT+1]; // +1 for flow calculations
  float16 targets_current[ASV_STEP_COUNT+1];
} asv_data_t;

asv_data_t *get_asv_data();
void init_asv_data(asv_data_t *data);
void update_asv_data(asv_data_t* asv, tracking_t* tr);

/////////////////////////
// Oh god, what a mess //

/*
  // Initialize new breath
  if (d->stage == S_START_INHALE) {
    d->stage = S_INHALE;
    if (((d->current.volume_max / d->recent.volume_max) <= 1.35f) || d->breath_count < 90) {
    if ((d->current.te > d->recent.te * 0.60f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        asv_interp_all(d);
    }}

    // 0-25% asvIPS -> go down; 25-100% asvIPS -> go up 
    const float rel_ips = max(d->current.ips - s_ips, 0.0f) / ASV_MAX_IPS;
    if (ASV_MAX_EPAP > 0.0f) {
      float rel_epap = max(d->asv_target_epap - s_epap, 0.0f) / ASV_MAX_EPAP;
      d->asv_target_epap_target = d->asv_target_epap + (map01c(rel_ips, 0.25f, 1.0f) * (1.0f - rel_epap) - map01c(rel_ips, 0.20f, 0.0f) * rel_epap) * ASV_MAX_EPAP;
      d->asv_target_epap_target = clamp(d->asv_target_epap_target, s_epap, s_epap + ASV_MAX_EPAP);
      // d->asv_target_epap_target = s_epap + map01c(d->current.ips, s_ips, s_ips + ASV_MAX_IPS) * ASV_MAX_EPAP;
    }
    d->asv_target_epap = max(d->asv_target_epap, s_epap);

    d->asv_target_ips = ASV_IPS_TARGET_ADJUSTMENT * d->current.ips * 0.95f + (1.0f - ASV_IPS_TARGET_ADJUSTMENT) * s_ips;

    if ((ASV_DISABLER == 1) && (d->dont_support == 0)) {
      // For every 10% volume excess above 120%, or for every 1s excess above recent average te, disable ASV for 1 breath, for up to 6 at once
      int te_excess = (int)(clamp(d->current.te - max(1.8f, d->recent.te), 0.0f, 6.0f));
      int vol_excess = (int)(clamp((d->current.volume_max / d->recent.volume_max - 1.1f) * 10.0f, 0.0f, 6.0f));
      d->asv_disable = d->asv_disable + te_excess + vol_excess - 1; // Reduce by 1 per breath
      d->asv_disable = clamp(d->asv_disable, -2, 9);
    }

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.20f) && (d->current.te > 0.300f) && (d->current.te <= 1.2f);

    d->final_ips = 0.0f;

    init_breath(&d->current);
    d->current.ips = max(d->asv_target_ips, s_ips);
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }

  // Keep track of ongoing values
  {
    d->current.volume += flow * delta;
    d->current.volume_max = max(d->current.volume_max, d->current.volume);
    d->current.exh_maxflow = min(d->current.exh_maxflow, flow); 
    d->current.inh_maxflow = max(d->current.inh_maxflow, flow); 
    if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
      d->current.ti += delta; 
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) {
      d->current.te += delta;
      if (d->final_ips == 0.0f) { d->final_ips = *cmd_ps; }
    }
  };


  int i = d->ticks/ASV_STEP_LENGTH;

  if ((d->ticks % ASV_STEP_LENGTH == 0) && (i>=ASV_STEP_SKIP) && (i<ASV_STEP_COUNT) && (d->stage == S_INHALE)) {
    d->current.targets[i] = d->current.volume;
    const float error_volume = d->current.targets[i] / (d->recent.targets[i] + 0.001f);
    // float recent_flow = d->recent.targets[i] - d->recent.targets[i-1];
    // float current_flow = d->current.targets[i] - d->current.targets[i-1];
    // float error_flow = current_flow / (recent_flow + 0.001f);

    #if ASV_DYNAMIC_GAIN == 1
      d->current.ips = max(s_ips, d->current.ips);
    #else
      d->current.ips = s_ips;
    #endif

    // This way:  95-140% => 0 to -1;  95-50% => 0 to 1
    const float ips_adjustment = map01c(error_volume, 0.94f, 0.5f) - map01c(error_volume, 0.96f, 1.4f);
    const float dt_asv = delta * ASV_STEP_LENGTH;

    // FIXME: Change the gain to something sane, probably delta-based
    // The 0.05f values ensure the adjustment is not super tiny.
    if (ips_adjustment > 0.01f) {
      d->current.ips += min(ips_adjustment + 0.05f, 1.0f) * ASV_GAIN * dt_asv;
    } else if (ips_adjustment < -0.01f) {
      d->current.ips += max(ips_adjustment - 0.05f, -1.0f) * ASV_GAIN * dt_asv; // (d->current.ips - s_ips) * 3.0f 
    }
    d->current.ips = s_ips + get_disabler_mult(d->asv_disable) * (d->current.ips - s_ips);
    d->current.ips = clamp(d->current.ips, s_ips, s_ips + ASV_MAX_IPS);
  }
  // eps += (d->asv_target_epap - s_epap) * 0.2f; // 0.2cmH2O extra EPS for every 1cmH2O of EPAP
  ips = max(d->current.ips, s_ips);
  if (EPS_REDUCE_WHEN_ASV > 0.0f) {
    // (don't- I think it just contributes to expiratory intolerance) Reduce EPS proportionally to extra IPS
    if (d->current.ips > s_ips) {
       eps = max(0.0f, eps - (d->current.ips - s_ips) * EPS_REDUCE_WHEN_ASV);
    }
  }

  // Handle EPAP adjustment
  {
    if (d->asv_target_epap < d->asv_target_epap_target) {
      d->asv_target_epap += 0.025f * delta; // 40s to change by 1cmH2O
    } else {
      d->asv_target_epap -= 0.0125f * delta; // 1m20s to drop by 1cmH2O
    }
    d->asv_target_epap = clamp(d->asv_target_epap, s_epap, s_epap + ASV_MAX_EPAP);
    epap = d->asv_target_epap;
  }
  if (toggle) {
    ips = s_ips;
    eps = 0.6f;
  }

*/

#endif