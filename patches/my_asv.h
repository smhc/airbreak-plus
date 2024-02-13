#ifndef _my_asv_h_
#define _my_asv_h_

/////////////////////////
// PID controller code //

// #define ASV_STEP_COUNT 20

#define ASV_STEP_LENGTH 5 // (10ms ticks)
#define ASV_STEP_COUNT 20 // (steps)
#define ASV_STEP_SKIP 2 // (steps)

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
  pid_t pid;
  float asv_factor;
  float final_ips; // Final max IPS value, used to maintain correct downslope

  breath_t recent;

  float16 targets_recent[ASV_STEP_COUNT+1]; // +1 for flow calculations
  float16 targets_current[ASV_STEP_COUNT+1];
} asv_data_t;

asv_data_t *get_asv_data();
void init_asv_data(asv_data_t *data);
void update_asv_data(asv_data_t* asv, tracking_t* tr);

#endif