#ifndef _my_asv_c_
#define _my_asv_c_

/////////////////////////
// PID Controller Code //

void pid_init(pid_t *pid, float p, float i, float d, float _min, float _max) {
  pid->last_error = 0.0f;
  pid->cumulative_error = 0.0f;
  pid->current_error = 0.0f;
  pid->kp = p; pid->ki = i; pid->kd = d;
  pid->output_min = _min;
  pid->output_max = _max;
}

float pid_get_signal_unclamped(pid_t *pid) {
  float result = pid->kp * pid->current_error; // Proportional branch
  result += pid->cumulative_error; // Integral branch (pre-multiplied by pid->ki in update)
  result += pid->kd * (pid->current_error - pid->last_error); // Derivative branch
  return result;
}

float pid_get_signal(pid_t *pid) {
  return clamp(pid_get_signal_unclamped(pid), pid->output_min, pid->output_max);
}

void pid_update(pid_t *pid, float current_error) {
  const float output = pid_get_signal_unclamped(pid);
  const float outputc = pid_get_signal(pid);
  pid->last_error = pid->current_error;
  pid->current_error = current_error;
  // Instead of a typical saturation check, just pre-multiply by ki and clamp to min/max
  pid->cumulative_error += pid->ki * pid->current_error;
  pid->cumulative_error = clamp(pid->cumulative_error, pid->output_min, pid->output_max);
}

////////////////////////
// ASV algorithm code //

asv_data_t *get_asv_data() {
  asv_data_t *asv = GET_PTR(PTR_ASV_DATA, asv_data_t, init_asv_data);
  const unsigned now = tim_read_tim5();
  // Initialize if it's the first time or more than 0.1s elapsed, suggesting that the therapy was stopped and re-started.
  if ((now - asv->last_time) > 100000) { init_asv_data(asv); }
  asv->last_time = now;

  return asv;
}

void init_asv_data(asv_data_t *data) {
  data->last_time = tim_read_tim5();
  data->breath_count = 0;

  data->ticks = -1; // Uninitialized

  pid_init(&data->pid, 0.6f, 0.075f, 0.2f, -0.05f, 2.5f);
  data->asv_factor = 1.0f;
  data->final_ips = 0.0f;

  init_breath(&data->recent);

  for(int i=0; i<ASV_STEP_COUNT+1; i++) {
    data->targets_current[i] = 0.0f;
    data->targets_recent[i] = 0.0f;
  }
}

void update_asv_data(asv_data_t* asv, tracking_t* tr) {
  // TODO: Expand checks for whether a breath was valid
  // TODO: Convert to using the 80th percentile of recent volume, multiplied by relative recent targets
  // TODO: Wait for average error to stabilize before engaging ASV

  breath_t *recent = &asv->recent;
  breath_t *current = &tr->current;
  breath_t *last = &tr->last;

  if (tr->st_inhaling && tr->st_just_started) { // New breath just started
    bool valid_breath = last->te > max(recent->te * 0.6f, 0.7f);
    if (valid_breath) {
      float coeff = 0.025f; // ~40% from last 20 breaths, ~66% from 40, ~77% from 60
      for(int i=0; i<ASV_STEP_COUNT+1; i++) {
        // If it has zero volume, it means the breath was past its peak already, ignore it.
        if (asv->targets_current[i] > 0.0f) {
          inplace(interp, &asv->targets_recent[i], asv->targets_current[i], coeff);
        }
        asv->targets_current[i] = 0.0f;
      }
      inplace(interp, &recent->volume_max, current->volume_max, coeff);
      inplace(interp, &recent->exh_maxflow, current->exh_maxflow, coeff);
      inplace(interp, &recent->inh_maxflow, current->inh_maxflow, coeff);
      inplace(interp, &recent->ti, current->ti, coeff);
      inplace(interp, &recent->te, current->te, coeff);

    }
    asv->final_ips = 0.0f;
    asv->breath_count += 1;
  }

  // FIXME: The padded step logic needs to calculate with STEP_SKIP better
  int i = current->t / ASV_STEP_LENGTH - ASV_STEP_SKIP + 1;
  if ((current->t%ASV_STEP_LENGTH == 0) && (i>=0) && (i<ASV_STEP_COUNT+1) && tr->st_inhaling) {
    asv->targets_current[i] = current->volume;
    if (i >= 1) { // Ignore the first, padded step 
      const float error_volume = asv->targets_current[i] / (asv->targets_recent[i] + 0.001f);
      const float recent_flow = asv->targets_recent[i] - asv->targets_recent[i-1];
      const float current_flow = asv->targets_current[i] - asv->targets_current[i-1];
      const float error_flow = current_flow / (recent_flow + 0.001f);

      float error = map01c(error_volume, 0.96f, 0.4f) - map01c(error_volume, 0.98f, 1.4f);
      // Speed up tiny adjustments slightly.
      if (error > 0.0f) { error = error * 0.975f - 0.025f; }
      if (error < 0.0f) { error = error * 0.95f - 0.05f; }
      if (error >= 0.99f) { error = 0.0f; } // If the error maxes out, it's probably not a real inhale

      pid_update(&asv->pid, error);
      asv->asv_factor = clamp(1.0f + pid_get_signal(&asv->pid), 1.0f, 3.5f);
    }
  }
}

#endif