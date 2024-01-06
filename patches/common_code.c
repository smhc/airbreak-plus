#include "common_code.h"

float map01(float s, float start, float end) {
   return (s - start)/(end-start);
}

// Version that clamps to 0-1
float map01c(float s, float start, float end) {
   return clamp( map01(s, start, end), 0.0f, 1.0f );
}

float interp(float from, float to, float coeff) {
   return from + (to - from) * coeff;
}