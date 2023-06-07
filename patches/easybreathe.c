#include "stubs.h"
#include "common_code.h"

void MAIN start(int param_1) {
  float delta = 0.010f; // It's 10+-0.01ms, basically constant
  const float flow = *flow_compensated / 60.0f; // (L/s)
  const float progress = fvars[0x20];

  // if (*cmd_ps > 0.8f) {
  //   *cmd_ps -= 0.8f;
  // }


  pressure_work(param_1); // Call the original EasyBreathe function

  if (progress <= 0.5f) {
  // if (*cmd_ps > 0.01f) {
    *cmd_ps += 0.6f;
  } else if (flow < -0.08f) {
    *cmd_ps += (flow+0.08f) * 1.0f;
  }
 *cmd_ipap = *cmd_epap + *cmd_ps;

  // if (flow < -0.08f) {
  //   *cmd_ps += (flow+0.08f) * 2.0f;
  // }


  // if (progress < 0.5f) {
  //   *cmd_ps = fvars[0x20] * 3.0f;
  // } else {
  //   *cmd_ps = (progress-1.0f) * 1.0f;
  // }

}