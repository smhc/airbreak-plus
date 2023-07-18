#include "stubs.h"
#include "common_code.h"


// Takes place of `compute_pressure_stuff_replaced_with_breath`
// It appears to replace all other modes...??

// Not an option:
//  - compute_pressure_stuff_replaced_with_breath - runs in multiple modes, breaks S when replaced.
//  - pressure_work - breaks S when replaced
//  - pressure_timed_ramp - doesn't seem to do anything
//  - pressure_target_normal_operation - doesn't seem to do anything
// Try:
// - pressure_check_something - writes maybe-PS to fvars[0x90]
void MAIN start(int param_1) {
  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = *flow_compensated / 60.0f; // (L/s)
  const float progress = fvars[0x20];

  // if (*cmd_ps > 0.8f) {
  //   *cmd_ps -= 0.8f;
  // }


  // return 0;
  // int retval = pressure_timed_ramp(param_1,param_2,param_3);
  return pressure_work(param_1);
  // int retval = pressure_timed_ramp(param_1,param_2,param_3);

  // // Early IPS
  // if (flow > 0.08f) {
  //   if ((*cmd_ps > 0.05f) && (*cmd_ps < 1.0f)) {
  //     *cmd_ps += 1.0f;
  //   }
  // }

  // if (flow < -0.08f) {
  //   *cmd_ps += (flow+0.08f) * 2.0f;
  // }

  // *cmd_ipap = *cmd_epap + *cmd_ps;

  // return retval;
}