#include "stubs.h"
#include "common_code.h"

#define HISTORY_LENGTH 15
#define COUNT_PRETRIGGER_FLOW 1 // Include pre-breath-start positive flow values into cumulative volume of current breath
                                // (maybe important for early calculations, but I think it's likely to underestimate limitations in )

#define CUSTOM_CYCLE 1
#define JITTER 0

const float EPS_FLOWBASED_DOWNSLOPE = 0.6f; // Maximum flowbased %
const float EPS_FIXED_TIME = 1.1f;

const   int FOT_HALF_WAVELENGTH = 3*4; // In ticks, must be a multiple of 4 to save into EDF files correctly.
const float FOT_AMPLITUDE = 0.2f;

// int a = error_implement_fot();

const char S_UNINITIALIZED = 0;
const char S_START_INHALE = 1;
const char S_INHALE = 2;
const char S_INHALE_LATE  = 3; // After peak flow has been reached
const char S_EXHALE = 4; // Active exhale
const char S_EXHALE_LATE  = 5; // Expiratory pause


INLINE float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}

INLINE float interpmin(float from, float to, float coeff, float min_speed) {
   float rate = (to - from) * coeff;
   if (rate >= 0.0f) {
     return from + clamp(rate, min_speed, to-from);
   } else {
     return from + clamp(rate, to-from, -min_speed);
   }
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
} breath_t;

INLINE void init_breath(breath_t *breath) {
  breath->volume = 0.0f;
  breath->volume_max = 0.0f;
  breath->duration = 0.0f;
  breath->exh_maxflow = 0.0f;
  breath->inh_maxflow = 0.0f;
  breath->ti = 0.0f;
  breath->te = 0.0f;
  breath->ips = 0.0f;
  breath->slope = 0.0f;
}

typedef struct {
  float16 flow[HISTORY_LENGTH];
} history_t;

INLINE void init_history(history_t *hist) {
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
  int8 stage;

  float cycle_off_timer;
  float final_ips;

  breath_t current;
  breath_t recent;

  history_t history;
} my_data_t;

INLINE void init_my_data(my_data_t *data) {
  data->last_progress = 0.0f;
  data->last_time = tim_read_tim5();
  data->breath_count = 0;

  data->ticks = -1; // Uninitialized
  data->last_jitter = 0;
  data->dont_support = 0;
  data->stage = S_UNINITIALIZED;

  data->cycle_off_timer = 0.0f;
  data->final_ips = 0.0f;

  init_breath(&data->current);
  init_breath(&data->recent);

  init_history(&data->history);
}

const float ASV_INTERP = 0.025f; // ~45% from last 15 breaths, ~70% from 30, ~88% from 45
INLINE void asv_interp_all(my_data_t* data) {
  breath_t *recent = &data->recent;
  breath_t *current = &data->current;
  float coeff = ASV_INTERP;
  float coeff_v = coeff; // Update volumes a bit differently
  if (current->volume_max < recent->volume_max) { // Update downwards a bit slower.
    coeff_v *= 0.4f;
  }

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
INLINE my_data_t * get_data() {
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

INLINE void apply_jitter(int8 amt) {
  float amtf = 0.005f * amt;
  *cmd_ps += amtf; *cmd_epap += amtf; *cmd_ipap += amtf;
}

// This is where the real magic starts
// The entry point. All other functions MUST be inline
void MAIN start(int param_1) {
  const float progress = fvars[0x20]; // Inhale(1.6s to 0.5), Exhale(4.5s from 0.5 to 1.0). Seems breath-duration-dependent. Only in S mode
  const float s_ipap = fvars[0xe];
  const float s_epap = fvars[0xf];
  const float s_eps = 0.6f;
  const float s_ips = s_ipap - s_epap;

  float epap = s_epap; // (cmH2O)
  float ips = s_ips;   // (cmH2O)
  float eps = s_eps;    // (cmH2O)
  float rise_time = 0.70f;       // (s)
  float fall_time = 0.75f;       // (s)
  float slope = ips / rise_time; // (cmH2O/s) Slope to meet IPS in rise_time milliseconds

  float fot = 0.0f;

  float assist_scale = 1.0f;

  int8 toggle = 0;

  // Binary toggle if IPAP ends in 0.2 or 0.8
  float a = (s_ipap - (int)s_ipap);
  if (((a >= 0.1f) && (a <= 0.3f)) || ((a >= 0.7f) && (a <= 0.9f))) {
    toggle = 1;
  }

  my_data_t *d = get_data();

  #if JITTER == 1
    apply_jitter(-d->last_jitter); // Undo last jitter, to prevent small errors from it from accumulating.
  #endif

  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = *flow_compensated / 60.0f; // (L/s)


  eps = max((s_epap - 5) * 0.3f, 0.4f);

  // Gradually ramp the assist scale up from 40% to 100%
  assist_scale = min(0.4f + max((int)d->breath_count - 2, 0) * 0.1f, 1.0f);

  const int fhw = FOT_HALF_WAVELENGTH;
  if (((*pap_timer * 10) % (fhw*2)) < fhw) {
    // ((float)(d->ticks % fhw) / (float)fhw) 
    fot = FOT_AMPLITUDE;
  } else {
    fot = -FOT_AMPLITUDE;
  }

  // Process breath stage logic
  if (d->last_progress > progress + 0.25f) { // Stock inhale trigger
    d->stage = S_START_INHALE;
  } else if ((d->stage == S_INHALE) && (*cmd_ps >= ips*0.99f)) {
    d->stage = S_INHALE_LATE;
  } else if ((d->stage == S_INHALE) || (d->stage == S_INHALE_LATE)) {
    #if CUSTOM_CYCLE == 1
      // Cycle off below 0 flow, or after 100ms past configured cycle sensitivity.
      if (flow < -0.01f * d->current.inh_maxflow) { d->stage = S_EXHALE; }
      if (progress > 0.5f) { 
        d->cycle_off_timer += delta;
        if (d->cycle_off_timer >= 0.95f) { d->stage = S_EXHALE; }
      }
    #else
      if (progress > 0.5f) { d->stage = S_EXHALE; }
    #endif
  } else if ((d->stage == S_EXHALE) && ((d->current.volume / d->current.volume_max) <= 0.1f)) {
    d->stage = S_EXHALE_LATE;
  }

  // Initialize new breath
  if (d->stage == S_START_INHALE) {
    d->stage = S_INHALE;
    if (((d->current.volume_max / d->recent.volume_max) <= 1.35f) || d->breath_count < 90) {
    if ((d->current.te > d->recent.te * 0.60f) && (d->ticks != -1) && (d->dont_support == 0) ) {
        asv_interp_all(d);
    }}

    d->ticks = 0;
    d->breath_count += 1;
    d->dont_support = ((d->current.volume / d->current.volume_max) > 0.20f) && (d->current.te <= 1.2f);

    d->cycle_off_timer = 0.0f;

    d->final_ips = 0.0f;

    #if (COUNT_PRETRIGGER_FLOW == 1)
      float recent_volume = 0.0f;
      for(int i=0; i<HISTORY_LENGTH; i++) {
        if (d->history.flow[i] > 0.0f) {
          recent_volume += d->history.flow[i] * delta;
        }
        d->history.flow[i] = 0.0f; 
      }
      d->current.volume = recent_volume;
    #endif
    init_breath(&d->current);
  } else {
    if (d->ticks != -1) { d->ticks += 1; }
  }
  #if COUNT_PRETRIGGER_FLOW == 1
    if (d->ticks != -1) {
      d->history.flow[d->ticks % HISTORY_LENGTH] = flow;
    }
  #endif

  // Keep track of ongoing values
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

  // Set the commanded PS and EPAP values based on our target
  *cmd_epap = epap;
  if (d->dont_support) {
    *cmd_ps = interp(*cmd_ps, 0.0f, 3.0f * delta);
  } else {

    float IMOD = s_ips / 4.0f;
    float IPS_FIX = 1.2f * IMOD;
    float IPS_FLO = 2.0f * IMOD; // Expect under half of it 
    float IPS_VOL = 5.2f * IMOD; // Expect half of it

    if (d->stage == S_INHALE || d->stage == S_INHALE_LATE) {
      float t = d->current.ti;
      float fixed_assist = 0.0f; float volume_assist = 0.0f; float flow_assist = 0.0f;

      fixed_assist = IPS_FIX * map01c(t, 0.0f, IPS_FIX / 4.5f);

      float vol2 = d->current.volume;
      if (vol2 >= 0.5f) { vol2 = 0.5f + (vol2-0.5f) * 0.66f; }
      volume_assist = vol2 * IPS_VOL; // 2.0f * s_ips

      float flow2 = flow;
      if (flow2 > 0.6f) { flow2 = 0.6f + (flow2-0.6f) * 0.66f; }
      if (flow2 > 0.3f) { flow2 = 0.3f + (flow2-0.3f) * 0.66f; }
      flow_assist = flow2 * IPS_FLO; // 0.5f * s_ips;

      *cmd_ps = clamp(fixed_assist + assist_scale * (volume_assist + flow_assist), 0.0f, s_ips * 1.5f);
    } else if (d->stage == S_EXHALE || d->stage == S_EXHALE_LATE) { // Exhale
      float t = d->current.te;
      float eps_mult = map01c(d->current.volume / d->current.volume_max, 0.0f, 0.6f);
      eps_mult = min(eps_mult, map01c(t, EPS_FIXED_TIME, 0.4f));

      float ips_mult = map01c(t, fall_time, 0.0f); ips_mult = ips_mult * ips_mult * 0.95f;
      if (EPS_FLOWBASED_DOWNSLOPE > 0.0f) {
        float temp = EPS_FLOWBASED_DOWNSLOPE;
        ips_mult = min(ips_mult, ips_mult * (1.0f-temp) + temp * map01c(flow, -d->current.inh_maxflow * 0.9f, 0.0f) );
      }
      *cmd_ps = ips_mult * d->final_ips - (1.0f - ips_mult) * eps_mult * eps;

      if (d->stage == S_EXHALE_LATE) {

        *cmd_ps += fot * map01c(d->current.volume / d->current.volume_max, 0.4f, 0.2f);
      }
    }
  }
  d->last_progress = progress;

  // Safeguards against going cray cray
  *cmd_ps = clamp(*cmd_ps, -eps - 1.0f, s_ips*1.5f+0.2f);
  *cmd_epap = clamp(*cmd_epap, s_epap - 1.0f, s_epap + 1.0f);
  *cmd_ipap = *cmd_epap + *cmd_ps;

  #if JITTER == 1
    // Necessary to keep graphing code called(I think based on PS change, who cares)
    d->last_jitter ^= 2 - (tim_read_tim5() & 4);
    apply_jitter(d->last_jitter);
  #endif
  return;
}
