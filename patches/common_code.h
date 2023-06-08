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

float *cmd_ps = &fvars[0x29]; // (cmH2O)
float *cmd_epap = &fvars[0x28]; // (cmH2O)
float *cmd_ipap = &fvars[0x2a]; // (cmH2O) This is set to epap+ps somewhere else, no point in writing directly to it

const float *leak_basal = &fvars[0x22]; // I believe this to be basal unintentional leak (L/min)
const float *leak = &fvars[0x24]; // Unintentional leak (L/min) - this is what flow_compensated incorporates
// const float *leak_b = &fvars[0x23]; // Also unintentional leak, but smaller amplitude, unsure of significance

const float *flow_raw = &fvars[0x3]; // (L/min)
const float *flow_patient = &fvars[0x0]; // (L/min)
const float *flow_compensated = &fvars[0x25]; // (L/min)
// const float *flow_delayed = &fvars[0x26]; // Slightly delayed 0x25 ?

const float *actual_pressure = &fvars[1]; // (cmH2O) Actual current pressure in the circuit
const   int *therapy_mode = &ivars[0x6f]; // It's 0 when device is inactive
const   int *pap_timer = &ivars[0];

#define p_error (*flow_compensated - *cmd_ipap)

// I believe these are the reported EPAP and IPAP written to EDF files.
// However, they're probably written somewhere else(before end of inspiration), which needs to be debugged to write them
// Or used to figure out how to add extra signals..?
// float *report_epap = &fvars[0xC5];
// float *report_ipap = &fvars[0xC4];
// b9, be might be TV, ba, bb might be MV 


// const float *asv_epap_min = &fvars[0x11];
// const float *asv_epap_max = &fvars[0x10];
// const float *asv_ips_min  = &fvars[0x14];
// const float *asv_ips_max  = &fvars[0x13];

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

STATIC float clamp01(float a) { return clamp(a, 0.0f, 1.0f); }

STATIC float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
STATIC float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}


// Usage example: `inplace(max, &a, b)`
#define inplace(fn, ptr, args...) ({ \
  __typeof__ (ptr) _ptr = (ptr); \
  *_ptr = fn(*_ptr, args); \
})


#endif