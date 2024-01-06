#include "stubs.h"
#include "common_code.h"

// Ctrl+F 'FIXME'
// TODO: Rework ASV IPS gain towards something more rational
// TODO: Add second ASV IPS limit, for total maximum
// TODO: Early part of perc_rise should be faster

#define HISTORY_LENGTH 15

#define CUSTOM_TRIGGER 0 // 0=stock, 1=hybrid flow+pres
#define CUSTOM_CYCLE 1 // 0=stock, 1=based on mean, not max flow (compensates for non-easybreathe spikes

#define JITTER 0 // Necessary for graphing code to run properly. Shouldn't affect performance.

#define ASV 0
#define ASV_DYNAMIC_GAIN 1 // Keep ramping IPS up within-breath until target is met
const float ASV_IPS_TARGET_ADJUSTMENT = 0.5f; // (%) how much baseline target IPS adjusts towards last breath's final IPS value

#define ASV_DISABLER 1 // Disable ASV after hyperpneas and/or apneas (because all of mine are central, todo: differentiate the two)

const float IPS_PARTIAL_EASYBREATHE = 0.6f; // (% of IPS)

const float EPS_FLOWBASED_DOWNSLOPE = 0.25f; // % of downslope governed by negative flow instead of time
const float EPS_FIXED_TIME = 1.2f;
const float EPS_REDUCE_WHEN_ASV = 0.5f; // % of extra IPS to reduce EPS by

// 20*5*10ms = 1s
#define ASV_STEP_COUNT 20
#define ASV_STEP_LENGTH 5
#define ASV_STEP_SKIP 1 // Amount of steps before first doing ASV adjustments. MUST be at least 1 or code will crash due to out of bounds target array read
const float ASV_MAX_IPS = 2.0f; // within one breath
const float ASV_GAIN = 40.0f; // (cmH2O/s at 50% flow deficit)
const float ASV_MAX_EPAP = 1.0f;
const float ASV_EPAP_EXTRA_EPS = 0.2f; // How much extra EPS per EPAP


const char S_UNINITIALIZED = 0;
const char S_START_INHALE = 1;
const char S_INHALE = 2;
const char S_INHALE_LATE  = 3; // After peak flow has been reached
const char S_EXHALE = 4; // Active exhale
const char S_EXHALE_LATE  = 5; // Expiratory pause

// Setup storage for important data
typedef struct {
  float volume;
  float volume_max;

  // New stuff
  float exh_maxflow;
  float inh_maxflow;
  float ti;
  float te;

  float ips;
  #if ASV == 1
    float16 targets[ASV_STEP_COUNT];
  #endif 
} breath_t;

STATIC void init_breath(breath_t *breath) {
  breath->volume = 0.0f;
  breath->volume_max = 0.0f;
  breath->exh_maxflow = 0.0f;
  breath->inh_maxflow = 0.0f;
  breath->ti = 0.0f;
  breath->te = 0.0f;
  breath->ips = 0.0f;
  #if ASV == 1
    for(int i=0; i<ASV_STEP_COUNT; i++) {
      breath->targets[i] = 0.0f;
    }
  #endif
}

typedef struct {
  float last_progress;
  uint32 last_time;
  uint32 breath_count;

  int16 ticks; // Starts at 0, +1 each call
  int8 last_jitter;
  int8 dont_support;
  int8 asv_disable;
  int8 stage;

  float asv_target_ips;
  float asv_target_epap;
  float asv_target_epap_target;

  float final_ips;

  breath_t current;
  breath_t recent;
} my_data_t;

STATIC void init_my_data(my_data_t *data) {
  data->last_progress = 0.0f;
  data->last_time = tim_read_tim5();
  data->breath_count = 0;

  data->ticks = -1; // Uninitialized
  data->last_jitter = 0;
  data->dont_support = 0;
  data->asv_disable = 0;
  data->stage = S_UNINITIALIZED;

  data->asv_target_ips = 0.0f;
  data->asv_target_epap = 0.0f;
  data->asv_target_epap_target = 0.0f;

  data->final_ips = 0.0f;

  init_breath(&data->current);
  init_breath(&data->recent);
}

const float ASV_INTERP = 0.025f; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
STATIC void asv_interp_all(my_data_t* data) {
  breath_t *recent = &data->recent;
  breath_t *current = &data->current;
  float coeff = ASV_INTERP;
  float coeff_v = coeff; // Update volumes a bit differently
  if (current->volume_max < recent->volume_max) { // Update downwards a bit slower.
    coeff_v *= 0.4f;
  }
  // Don't adjust targets if it's a hyperpnea. Breath count hardcoded for ASV_INTERP=0.025f constant.
  // TODO: Instead of counting breaths, wait for average error to stabilize
  #if ASV == 1
    for(int i=0; i<ASV_STEP_COUNT; i++) {
      // If it has zero volume, it means the breath was past its peak already, ignore it.
      if (current->targets[i] > 0.0f) {
        inplace(interp, &recent->targets[i], current->targets[i], coeff_v);
      }
      current->targets[i] = 0.0f;
    }
  #endif
  inplace(interp, &recent->ips, current->ips, 0.5f);
  inplace(interp, &recent->volume_max, current->volume_max, coeff_v);
  inplace(interp, &recent->exh_maxflow, current->exh_maxflow, coeff);
  inplace(interp, &recent->inh_maxflow, current->inh_maxflow, coeff);
  inplace(interp, &recent->ti, current->ti, coeff);
  inplace(interp, &recent->te, current->te, coeff);
}


// Awful hacky code for storing arbitrary data
typedef struct {
  unsigned magic;
  my_data_t * data;
} magic_ptr_t;

magic_ptr_t * const magic_ptr = (void*) (0x2000e948 + 0x11*4);
const unsigned MAGIC = 0x07E49001;
STATIC my_data_t * get_data() {
  if (magic_ptr->magic != MAGIC) {
    magic_ptr->data = malloc(sizeof(my_data_t));
  }
  const unsigned now = tim_read_tim5();
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was stopped and re-started.
  if ((magic_ptr->magic != MAGIC) || (now - magic_ptr->data->last_time) > 100000) {
    init_my_data(magic_ptr->data);
  }
  magic_ptr->data->last_time = now; // Keep it updated so we don't reset the struct
  magic_ptr->magic = MAGIC;
  return magic_ptr->data;
}

#if ASV == 1
STATIC float get_disabler_mult(int n) {
  if (n <= 0) { return 1.00f; }
  if (n == 1) { return 0.5f; }
  if (n == 2) { return 0.25f; }
  if (n == 3) { return 0.125f; }
  return 0.0f;
}
#endif

#if JITTER == 1
STATIC void apply_jitter(int8 amt) {
  const float amtf = 0.005f * amt;
  *cmd_ps += amtf; *cmd_epap -= amtf;
}
#endif

// This is where the real magic starts
// The entry point. All other functions **must** be marked INLINE or STATIC
void MAIN start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent. Only in S mode
  const float s_eps = 0.6f;
  const float s_rise_time = s_rise_time_f;       // (s)
  const float s_fall_time = 0.8f;       // (s)

  float epap = s_epap; // (cmH2O)
  float ips = s_ips;   // (cmH2O)
  float eps = s_eps;   // (cmH2O)

  my_data_t *d = get_data();

  // Binary toggle if IPAP ends in 0.2 or 0.8
  const float a = (s_ipap - (int)s_ipap);
  const int8 toggle = (((a >= 0.1f) && (a <= 0.3f)) || ((a >= 0.7f) && (a <= 0.9f)));

  #if JITTER == 1
    apply_jitter(-d->last_jitter); // Undo last jitter, to prevent small errors from it from accumulating.
  #endif

  const float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = *flow_compensated / 60.0f; // (L/s)

  // eps += max(d->asv_target_epap - s_epap, 0.0f) * ASV_EPAP_EXTRA_EPS;
  // eps = 0.2f + max((max(d->asv_target_epap, s_epap) - 5.0f) * 0.2f, 0.4f);
  eps = max((max(d->asv_target_epap, s_epap) - 5.0f) * 0.3f, 0.6f);
  // eps = 0.6f;

  ips   = max(d->current.ips, s_ips);

  // Process breath stage logic
  float cycle_threshold = sens_cycle * d->current.inh_maxflow;
  {
    #if CUSTOM_CYCLE == 1
      if (d->current.ti > 0.0f) {
        cycle_threshold = sens_cycle * (d->current.volume_max / d->current.ti) * 1.15f;
      }
    #endif
  
    #if CUSTOM_TRIGGER == 1 
      // float tr_flow = map01c(flow, 0.0f, sens_trigger / 60.0f);
      // float tr_pres = map01c(p_error, -0.05f, -0.20f) * (tr_flow > 0.0f) * 0.5f;
      // TODO: Only do the pressure term if cmd_ps >= 0

      float sens = 0.5f + 0.5f * map01c(d->current.te, max(1.0f, d->recent.te * 0.5f), max(1.6f, d->recent.te * 0.85f));
      float pressure_term = (flow>-0.04f) * (*cmd_ps >= -0.02f) * clamp(-p_error-0.05f, 0.0f, 0.25f) * 0.2f; // neg 0.05-0.20 -> 0-0.066
      int8 inspiratory_trigger = ( pressure_term + flow) * sens >= (sens_trigger / 60.0f + 0.025f);
      inspiratory_trigger &&=  ((d->stage == S_UNINITIALIZED) || (d->stage == S_EXHALE_LATE));
    #else
      int8 inspiratory_trigger = (d->last_progress > progress + 0.25f); // Stock inhale trigger
    #endif
    if (inspiratory_trigger) {
        d->stage = S_START_INHALE;
    } else if ((d->stage == S_INHALE) && (*cmd_ps >= ips*0.98f)) {
      d->stage = S_INHALE_LATE;
    } else if ((d->stage == S_INHALE) || (d->stage == S_INHALE_LATE)) {
      #if CUSTOM_CYCLE == 1
        if (flow <= cycle_threshold) { d->stage = S_EXHALE; }
      #else
        if (progress > 0.5f) { d->stage = S_EXHALE; }
      #endif
    } else if ((d->stage == S_EXHALE) && ((d->current.volume / d->current.volume_max) <= 0.2f)) {
      d->stage = S_EXHALE_LATE;
    }
  };

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


  #if ASV == 1
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
  #endif

  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    inplace(interp, cmd_ps, 0.0f, 3.0f * delta);
  } else {
    if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
      const float t = d->current.ti;
      if (d->stage == S_INHALE) {

        // FIXME: A sawtooth shape, not very useful, also deformed by the ASV function
        // float perc = map01c(t, 0.100f, 0.5f * s_rise_time + 0.100f) * (1.0f-IPS_PARTIAL_EASYBREATHE) * 0.6f;
        // perc += map01c(t, 0.100f + 0.5f * s_rise_time, s_rise_time + 0.100f) * (1.0f-IPS_PARTIAL_EASYBREATHE) * 0.4f;
        // perc += map01c(t, 0.0f, 1.2f) * IPS_PARTIAL_EASYBREATHE;
        float perc = map01c(t, 0.0f, s_rise_time);

        *cmd_ps = ips * perc;
      } else {
        *cmd_ps = ips;
      }
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) { // Exhale
      const float t = d->current.te;
      float eps_mult = map01c(d->current.volume / d->current.volume_max, 0.05f, 0.6f);
      eps_mult = min(eps_mult, map01c(t, EPS_FIXED_TIME, 0.4f));

      float ips_mult = map01c(t, s_fall_time, 0.0f); ips_mult = ips_mult * ips_mult * 0.95f;
      if (EPS_FLOWBASED_DOWNSLOPE > 0.0f) {
        float temp = EPS_FLOWBASED_DOWNSLOPE;
        ips_mult = min(ips_mult, ips_mult * (1.0f-temp) + temp * map01c(flow, -d->current.inh_maxflow * 0.9f, cycle_threshold) );
      }
      *cmd_ps = ips_mult * d->final_ips - (1.0f - ips_mult) * eps_mult * eps;
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps, s_ips + ASV_MAX_IPS);
  *cmd_epap = clamp(*cmd_epap, s_epap, s_epap + ASV_MAX_EPAP);
  *cmd_ipap = *cmd_epap + *cmd_ps;

  #if JITTER == 1
    // Necessary to keep graphing code called(I think based on PS change, who cares)
    d->last_jitter ^= 2 - (tim_read_tim5() & 4);
    apply_jitter(d->last_jitter);
  #endif

  return;
}
