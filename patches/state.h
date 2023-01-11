#ifndef _state_h_
#define _state_h_

// WARNING: Do not use this. It corrupts memory that code related to the SD card uses.

typedef struct {
	float high_pressure; // in mm H20
	float low_pressure; //  in mm H20
	unsigned high_time; // in microseconds
	unsigned low_time; // in microseconds
	float smoothing;

	unsigned magic;
	unsigned last_change;
	unsigned state;
	unsigned sample;
	float target;
	int last_value;
} breath_state_t;

// our state is is at the top of SRAM
static breath_state_t * const state = (void*) 0x20001f00;

#endif
