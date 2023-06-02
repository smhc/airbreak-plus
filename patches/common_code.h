#ifndef _common_code_h_
#define _common_code_h_


// Mandatory definitions
#define INLINE inline __attribute__((always_inline))
#define MAIN __attribute__((section(".text.0.main")))
#define STATIC static __attribute__((section(".text.x.nonmain")))


// Types
typedef unsigned int uint32;
typedef signed short int16;
typedef signed char int8;
typedef __fp16 float16;


// PAP memory addresses
float * const fvars = (void*) 0x2000e948;
int * const ivars = (void*) 0x2000e750;

float *cmd_ps = &fvars[0x29];
float *cmd_epap = &fvars[0x28];
float *cmd_ipap = &fvars[0x2a]; // This is set to epap+ps somewhere else, no point in writing directly to it

const float *flow_raw = &fvars[0x3];
const float *flow_patient = &fvars[0x0];
const float *flow_compensated = &fvars[0x25];
// const float *flow_delayed = &fvars[0x26]; // Slightly delayed 0x25 ?

const float *actual_pressure = &fvars[1]; // Actual current pressure in the circuit
const   int *therapy_mode = &ivars[0x6f]; // It's 0 when device is inactive
const   int *pap_timer = &ivars[0];


// Utility functions
#define min(a,b) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a < _b ? _a : _b; \
})

#define max(a,b) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a > _b ? _a : _b; \
})

#define clamp(a, _min, _max) ({ \
  __typeof__ (a) _a = (a); \
  __typeof__ (_min) __min = (_min); \
  __typeof__ (_max) __max = (_max); \
  _a > __max ? __max : (_a < __min ? __min : a); \
})

INLINE float clamp01(float a) { return clamp(a, 0.0f, 1.0f); }

INLINE float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
INLINE float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

#endif