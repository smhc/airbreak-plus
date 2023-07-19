#include "stubs.h"
#include "common_code.h"

#define HISTORY_LENGTH 15
#define COUNT_PRETRIGGER_FLOW 0 // Include pre-breath-start positive flow values into cumulative volume of current breath
                                // (maybe important for early calculations, but I think it's likely to underestimate limitations in )

#define CUSTOM_TRIGGER 0 // 0=stock, 1=hybrid flow+pres
#define CUSTOM_CYCLE 0 // 0=stock, 1=-5% or +100ms, 2= stock-15%
const float CUSTOM_CYCLE_SENS = -0.05f;
#define JITTER 0

#define ASV 0
#define ASV_SLOPE 2

#define ASV_DISABLER 1 // Disable ASV after hyperpneas and/or apneas (because all of mine are central, todo: differentiate the two)

const float IPS_EARLY_DOWNSLOPE = 0.0f; // (% of IPS)
const float IPS_EARLY_DOWNSLOPE_START = 0.0f; // (% of max flow)

const float IPS_PARTIAL_EASYBREATHE = 0.0f; // (% of IPS)

const float EPS_FLOWBASED_DOWNSLOPE = 0.66f; // Maximum flowbased %
const float EPS_FIXED_TIME = 1.1f;
const float EPS_REDUCE_WHEN_ASV = 0.5f; // % of extra IPS to reduce EPS by

#define FOT 0
const   int FOT_HALF_WAVELENGTH = 3*4; // In ticks, must be a multiple of 4 to save into EDF files correctly.
const float FOT_AMPLITUDE = 0.15f;

// 20*5*10ms = 1s
#define ASV_STEP_COUNT 15
#define ASV_STEP_LENGTH 5
#define ASV_STEP_SKIP 1 // Amount of steps before first doing ASV adjustments. MUST be at least 1 or code will crash due to out of bounds target array read
const float ASV_MAX_IPS = 2.0f;
const float ASV_GAIN = 3.0f; // 4.0f seems fine but maybe a bit aggressive?
const float ASV_MAX_EPAP = 0.0f;
const float ASV_EPAP_EXTRA_EPS = 0.2f; // How much extra EPS per EPAP


const char S_UNINITIALIZED = 0;
const char S_START_INHALE = 1;
const char S_INHALE = 2;
const char S_INHALE_LATE  = 3; // After peak flow has been reached
const char S_EXHALE = 4; // Active exhale
const char S_EXHALE_LATE  = 5; // Expiratory pause


STATIC float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

// Setup storage for important data
typedef struct {
  float volume;
  float volume_max;

  float duration;

  // New stuff
  float exh_maxflow;
  float inh_maxflow;
  float ti;
  float te;

  float ips;
  float slope;
  #if ASV == 1
    float targets[ASV_STEP_COUNT];
  #endif 
} breath_t;

STATIC void init_breath(breath_t *breath) {
  breath->volume = 0.0f;
  breath->volume_max = 0.0f;
  breath->duration = 0.0f;
  breath->exh_maxflow = 0.0f;
  breath->inh_maxflow = 0.0f;
  breath->ti = 0.0f;
  breath->te = 0.0f;
  breath->ips = 0.0f;
  breath->slope = 0.0f;
  #if ASV == 1
    for(int i=0; i<ASV_STEP_COUNT; i++) {
      breath->targets[i] = 0.0f;
    }
  #endif
}

typedef struct {
  float16 flow[HISTORY_LENGTH];
} history_t;

STATIC void init_history(history_t *hist) {
  for(int i=0; i<HISTORY_LENGTH; i++) { 
    hist->flow[i] = 0.0f; 
  }
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

  float asv_target_slope;
  float asv_target_ips;
  float asv_target_epap;
  float asv_target_epap_target;

  float cycle_off_timer;
  float final_ips;

  float hack_earlyvol;
  float hack_minflow;
  float hack_maxslope;
  float hack_pretrigger_ips;
  float ips_fa;

  breath_t current;
  breath_t recent;

  history_t history;
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

  data->asv_target_slope = 0.0f;
  data->asv_target_ips = 0.0f;
  data->asv_target_epap = 0.0f;
  data->asv_target_epap_target = 0.0f;

  data->cycle_off_timer = 0.0f;
  data->final_ips = 0.0f;

  data->hack_earlyvol = 0.0f;
  data->hack_minflow = 0.0f;
  data->hack_maxslope = 0.0f;
  data->hack_pretrigger_ips = 0.0f;
  data->ips_fa = 0.0f;

  init_breath(&data->current);
  init_breath(&data->recent);

  init_history(&data->history);
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
  inplace(interp, &recent->slope, current->slope, 0.5f);
  inplace(interp, &recent->volume_max, current->volume_max, coeff_v);
  inplace(interp, &recent->duration, current->duration, coeff);
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
  unsigned now = tim_read_tim5();
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
  float amtf = 0.005f * amt;
  *cmd_ps += amtf; *cmd_epap -= amtf;
}
#endif

// This is where the real magic starts
// The entry point. All other functions MUST be inline
void MAIN start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent. Only in S mode
  const float s_eps = 0.6f;
  const float s_rise_time = s_rise_time_f;       // (s)
  const float s_fall_time = 0.9f;       // (s)
  const float s_slope = s_ips / s_rise_time;

  float epap = s_epap; // (cmH2O)
  float ips = s_ips;   // (cmH2O)
  float eps = s_eps;    // (cmH2O)

  my_data_t *d = get_data();

  // Binary toggle if IPAP ends in 0.2 or 0.8
  int8 toggle = 0;
  float a = (s_ipap - (int)s_ipap);
  if (((a >= 0.1f) && (a <= 0.3f)) || ((a >= 0.7f) && (a <= 0.9f))) {
    toggle = 1;
  }


  #if JITTER == 1
    apply_jitter(-d->last_jitter); // Undo last jitter, to prevent small errors from it from accumulating.
  #endif

  const float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = *flow_compensated / 60.0f; // (L/s)
  // const float flow2 = f_unfucked / 60.0f; // (L/s)

  // eps += max(d->asv_target_epap - s_epap, 0.0f) * ASV_EPAP_EXTRA_EPS;
  // eps = 0.2f + max((max(d->asv_target_epap, s_epap) - 5.0f) * 0.2f, 0.4f);
  eps = 0.6f;

  float slope = ips / s_rise_time; // (cmH2O/s) Slope to meet IPS in s_rise_time milliseconds 
  float SLOPE_MIN = slope; // (cmH2O/s)
  float SLOPE_MAX = 15.0f; // (cmH2O/s)

  ips   = max(d->current.ips, s_ips);
  slope = max(d->current.slope, SLOPE_MIN);

  // Process breath stage logic
  {
    #if CUSTOM_TRIGGER == 1 
      float sens = 0.5f + 0.5f * map01c(d->current.duration, max(1.0f, d->recent.te * 0.5f), max(1.6f, d->recent.te * 0.85f));
      int8 start_inhale = ( (flow>-0.04f) * clamp(-p_error / 3.0f, 0.0f, 0.04f) + flow) * sens >= (sens_trigger / 60.0f);
      // int8 exhale_done = (d->current.duration > 0.9f);
      // } else if (((d->stage == S_EXHALE) || (d->stage == S_EXHALE_LATE)) && (exhale_done && start_inhale)) {
      if (((d->stage == S_UNINITIALIZED) || (d->stage == S_EXHALE_LATE)) && start_inhale)
    #else
      if (d->last_progress > progress + 0.25f) // Stock inhale trigger
    #endif
    {
        d->stage = S_START_INHALE;
    } else if ((d->stage == S_INHALE) && (d->ips_fa >= ips*0.98f)) {
      d->stage = S_INHALE_LATE;
      d->hack_earlyvol = d->current.volume;
    } else if ((d->stage == S_INHALE) || (d->stage == S_INHALE_LATE)) {
      #if CUSTOM_CYCLE == 1
        const int8 do_cycle = (flow / d->current.inh_maxflow) <= CUSTOM_CYCLE_SENS;
        // Cycle off below 0 flow, or after 100ms past configured cycle sensitivity.
        if (do_cycle) { d->stage = S_EXHALE; }
        if (progress > 0.5f) { 
          d->cycle_off_timer += delta;
          if (d->cycle_off_timer >= 0.95f) { d->stage = S_EXHALE; }
        }
      #elif CUSTOM_CYCLE == 2
        if ( (flow / d->current.inh_maxflow) <= (sens_cycle-0.15f)) { 
          d->stage = S_EXHALE;
        }
      #else
        if (progress > 0.5f) { d->stage = S_EXHALE; }
      #endif
    } else if ((d->stage == S_EXHALE) && ((d->current.volume / d->current.volume_max) <= 0.1f)) {
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
    float rel_ips = max(d->current.ips - s_ips, 0.0f) / ASV_MAX_IPS;
    if (ASV_MAX_EPAP > 0.0f) {
      float rel_epap = max(d->asv_target_epap - s_epap, 0.0f) / ASV_MAX_EPAP;
      d->asv_target_epap_target = d->asv_target_epap + (map01c(rel_ips, 0.25f, 1.0f) * (1.0f - rel_epap) - map01c(rel_ips, 0.25f, 0.0f) * rel_epap) * ASV_MAX_EPAP;
      d->asv_target_epap_target = clamp(d->asv_target_epap_target, s_epap, s_epap + ASV_MAX_EPAP);
      // d->asv_target_epap_target = s_epap + map01c(d->current.ips, s_ips, s_ips + ASV_MAX_IPS) * ASV_MAX_EPAP;
    }
    d->asv_target_epap = max(d->asv_target_epap, s_epap);

    d->asv_target_ips = 0.9f * d->current.ips + 0.1f * s_ips;
    // d->asv_target_slope = 0.9f * d->hack_maxslope + 0.1f * SLOPE_MIN;

    if ((ASV_DISABLER == 1) && (d->dont_support == 0)) {
      // For every 10% volume excess above 120%, or for every 1s excess above recent average te, disable ASV for 1 breath, for up to 6 at once
      int te_excess = (int)(clamp(d->current.te - max(1.8f, d->recent.te), 0.0f, 6.0f));
      int vol_excess = (int)(clamp((d->current.volume_max / d->recent.volume_max - 1.1f) * 10.0f, 0.0f, 6.0f));
      d->asv_disable = d->asv_disable + te_excess + vol_excess - 1; // Reduce by 1 per breath
      d->asv_disable = clamp(d->asv_disable, -2, 9);
    }

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.20f) && (d->current.te > 0.055f) && (d->current.te <= 1.2f);

    d->cycle_off_timer = 0.0f;
    d->final_ips = 0.0f;
    d->hack_earlyvol = 0.0f;
    d->hack_minflow = 0.0f;
    d->hack_maxslope = 0.0f;
    d->ips_fa = 0.0f;

    #if (COUNT_PRETRIGGER_FLOW == 1)
      float recent_volume = 0.0f;
      for(int i=0; i<HISTORY_LENGTH; i++) {
        if (d->history.flow[i] > 0.0f) {
          recent_volume += d->history.flow[i] * delta;
        }
      }
      d->current.volume = recent_volume;
    #endif
    init_breath(&d->current);
    d->current.ips = max(d->asv_target_ips, s_ips);
    d->current.slope = max(d->asv_target_slope, SLOPE_MIN);
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }

  // Keep track of ongoing values
  {
    d->current.volume += flow * delta;
    d->current.volume_max = max(d->current.volume_max, d->current.volume);
    d->current.duration += delta;
    d->current.exh_maxflow = min(d->current.exh_maxflow, flow); 
    d->current.inh_maxflow = max(d->current.inh_maxflow, flow); 
    if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
      d->current.ti += delta; 
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) {
      d->current.te += delta;
      if (d->final_ips == 0.0f) { d->final_ips = *cmd_ps; }
    }
    if (d->ticks != -1) {
      d->history.flow[d->ticks % HISTORY_LENGTH] = flow;
    }
  };

  #if ASV == 1
    int i = d->ticks/ASV_STEP_LENGTH;

    if ((d->ticks % ASV_STEP_LENGTH == 0) && (i>=ASV_STEP_SKIP) && (i<ASV_STEP_COUNT) && (d->stage == S_INHALE)) {
      d->current.targets[i] = d->current.volume;
      float error_volume = d->current.targets[i] / (d->recent.targets[i] + 0.001f);

      float recent_flow = d->recent.targets[i] - d->recent.targets[i-1];
      float current_flow = d->current.targets[i] - d->current.targets[i-1];
      float error_flow = current_flow / (recent_flow + 0.001f);

      float base_ips = max(d->asv_target_ips, s_ips);
      float base_slope = max(d->asv_target_slope, s_slope);
      
      // This way:  95-130% => 0 to -1;  95-50% => 0 to 1
      float ips_adjustment = map01c(error_volume, 0.90f, 0.5f) - map01c(error_volume, 0.95f, 1.3f);
      // The 0.05f values ensure the adjustment is not super tiny.
      d->current.ips = s_ips;
      if (ips_adjustment > 0.01f) {
        d->current.ips += min(ips_adjustment + 0.05f, 1.0f) * ASV_GAIN;
      } else if (ips_adjustment < -0.01f) {
        d->current.ips += max(ips_adjustment - 0.05f, -1.0f) * (base_ips - s_ips);
      }
      d->current.ips = clamp(d->current.ips, max(s_ips, d->ips_fa), s_ips + ASV_MAX_IPS);
      d->current.ips = s_ips + get_disabler_mult(d->asv_disable) * (d->current.ips - s_ips);

      #if ASV_SLOPE == 1
        float slope_adjustment = map01c(error_flow, 0.90f, 0.5f) - map01c(error_flow, 0.95f, 1.2f);
        d->current.slope = base_slope;
        if (ips_adjustment > 0.01f) {
          d->current.slope += min(slope_adjustment + 0.05f, 1.0f) * (SLOPE_MAX - base_slope);
        } else if (ips_adjustment < -0.01f) {
          d->current.slope += max(slope_adjustment - 0.05f, -1.0f) * (base_slope - SLOPE_MIN);
        }
      #elif ASV_SLOPE == 2
        const float GAIN = 0.3f * 60.0f; // 18.0f. Convert the ResMed gain factor from cmH2O per (L/min) to (L/s)
        // const float error_term = (current_flow - recent_flow);
        const float error_term = (d->current.targets[i] - d->recent.targets[i] * 0.9f);
        // No need to multiply by ASV_STEP_LENGTH, as the values already are a sum of ASV_STEP_LENGTH steps.
        d->current.slope += GAIN * error_term * (delta);
        d->current.slope = clamp(d->current.slope, SLOPE_MIN, SLOPE_MAX);
      #else
        d->current.slope = d->current.ips / s_rise_time;
      #endif

      // d->hack_maxslope = 

    }
    // eps += (d->asv_target_epap - s_epap) * 0.2f; // 0.2cmH2O extra EPS for every 1cmH2O of EPAP
    ips = max(d->current.ips, s_ips);
    slope = max(d->current.slope, SLOPE_MIN);
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
      slope = SLOPE_MIN;
      // eps = 0.6f;
    }
  #endif

  const float IPS_PT_FA = 2.0f;
  #if PRETRIGGER_IPS == 1
    if (d->stage == S_EXHALE_LATE) { 
      float dflow = flow - d->history.flow[(d->ticks - 1) % HISTORY_LENGTH];
      if ((dflow > 0.0f) && (flow < 0.0f)) { dflow *= 0.5f; }
      if ((dflow > 0.0f) && (flow < -0.08f)) { dflow = 0.0f; }
      if (dflow < 0.0f) { dflow *= 2.0f; }
      inplace(max, d->hack_pretrigger_ips + dflow * IPS_PT_FA, 0.0f);
    } else if (d->stage == S_EXHALE) {
      d->hack_pretrigger_ips = 0.0f;
    }
  #endif

  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    inplace(interp, cmd_ps, 0.0f, 3.0f * delta);
  } else {
    if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
      float t = d->current.ti;
      if (d->stage == S_INHALE) {
        if (t <= 0.050f) { slope *= 0.707f; }
        if (t <= 0.100f) { slope *= 0.707f; }

        if (IPS_PARTIAL_EASYBREATHE > 0.0f) {
          if (d->ips_fa >= ips) {
            slope  = 0.0f;
          } else if (d->ips_fa >= (1-IPS_PARTIAL_EASYBREATHE) * ips) {
            slope += (ips*IPS_PARTIAL_EASYBREATHE) / (1.2f - s_rise_time) - SLOPE_MIN;
          } else {
            slope -= SLOPE_MIN * IPS_PARTIAL_EASYBREATHE;
          }
        } else {
          if (d->ips_fa >= ips - 0.4f) { slope *= 0.66f; }
          if (d->ips_fa >= ips - 0.2f) { slope *= 0.66f; }
          if (d->ips_fa >= ips) { slope  = 0.0f; }
        }

        d->ips_fa = min(ips, d->ips_fa + slope * delta);
        d->hack_minflow = d->current.inh_maxflow;
      } else {
        // This mimics Flow Assist
        if ((IPS_EARLY_DOWNSLOPE > 0.0f)
            && (t >= max(d->recent.ti * 0.6f, 0.6f))
            && (d->current.volume >= d->recent.volume * 0.6f)
        ) {
          float ips_drop = IPS_EARLY_DOWNSLOPE * ips;
          float drop = map01c(min(flow, d->hack_minflow) / d->current.inh_maxflow, IPS_EARLY_DOWNSLOPE_START, 0.0f) * ips_drop;
          float drop_max = ips_drop / 0.3f * delta; // Assume the fastest we want is reaching the drop in 300ms
          // d->ips_fa = ips - drop;
          if (flow >= d->current.inh_maxflow * 0.05f) { // Don't drop when we're already close to 0
            d->ips_fa = clamp(ips - drop, d->ips_fa - drop_max, ips);
            d->hack_minflow = min(d->hack_minflow, flow);
          }
        } else {
          d->ips_fa = ips;
        }
      }
      *cmd_ps = d->ips_fa;
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) { // Exhale
      float t = d->current.te;
      float eps_mult = map01c(d->current.volume / d->current.volume_max, 0.05f, 0.6f);
      eps_mult = min(eps_mult, map01c(t, EPS_FIXED_TIME, 0.4f));

      float ips_mult = map01c(t, s_fall_time, 0.0f); ips_mult = ips_mult * ips_mult * 0.95f;
      if (EPS_FLOWBASED_DOWNSLOPE > 0.0f) {
        float temp = EPS_FLOWBASED_DOWNSLOPE;
        ips_mult = min(ips_mult, ips_mult * (1.0f-temp) + temp * map01c(flow, -d->current.inh_maxflow * 0.9f, 0.0f) );
      }
      *cmd_ps = ips_mult * d->final_ips - (1.0f - ips_mult) * eps_mult * eps;
    }
  }
  d->last_progress = progress;

  // if ( (d->stage == S_EXHALE_LATE) || (d->stage == S_INHALE) ) {
  //   *cmd_ps = max(*cmd_ps, d->hack_pretrigger_ips);
  // }

  #if FOT == 1
    const int fhw = FOT_HALF_WAVELENGTH;
    if (((*pap_timer * 10) % (fhw*2)) < fhw) {
      *cmd_ps += FOT_AMPLITUDE;
    } else {
      *cmd_ps -= FOT_AMPLITUDE;
    }
  #endif

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps-FOT_AMPLITUDE, s_ips + ASV_MAX_IPS + FOT_AMPLITUDE);
  *cmd_epap = clamp(*cmd_epap, s_epap - 1, s_epap + ASV_MAX_EPAP);
  *cmd_ipap = *cmd_epap + *cmd_ps;

  #if JITTER == 1
    // Necessary to keep graphing code called(I think based on PS change, who cares)
    d->last_jitter ^= 2 - (tim_read_tim5() & 4);
    apply_jitter(d->last_jitter);
  #endif

  return;
}
