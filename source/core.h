#pragma once

#include "engine.h"

// REF: http://www.isthe.com/chongo/tech/comp/fnv/index.html [FNV Hash]

// REF: https://prng.di.unimi.it/ [xoshiro / xoroshiro generators]

union Random_Data{
	struct{
		u64 seed_low;
		u64 seed_high;
	};
	u64 seed[2];
};

extern thread_local Random_Data g_default_random;

void create_default_random();

char random_char(Random_Data& = g_default_random);
short random_short(Random_Data& = g_default_random);
int random_int(Random_Data& = g_default_random);
int64 random_int64(Random_Data& = g_default_random);

// REF: https://medium.com/@tglaiel/how-to-make-your-game-run-at-60fps-24c61210fe75 [How to make your game run at 60 FPS]

/*
	---- About history smoothing and residual ----

	* History smoothing reduces the impact of lag spikes by spreading the lag delay over the duration of the history

	* The tick_residual counts the number of (1 / history_size)-th of time unit to keep history smoothing accurate

	EXAMPLE:
	* -4,-3,-2 are perfect frames, taking 10ms
	* -1 is a slow frame, taking 40 ms instead of 10 ms 

	 i ; time : prev. hist  -> new hist    = avg -> total (+acc.) -> updt. ; acc. ; residual
	---------------------------------------------------------------------------
	 0 ; 40ms : 10 10 10 10 -> 40 10 10 10 = 17.5 -> 17.5         -> 1     ; +7   ; 2/4
	 1 ; 10ms : 40 10 10 10 -> 40 10 10 10 = 17.5 -> 24.5         -> 2     ; +4   ; 4/4
	RESIDUAL INJECTION								              -> 2     ; +5   ; 0/4
	 2 ; 10ms : 40 10 10 10 -> 40 10 10 10 = 17.5 -> 22.5         -> 2     ; +2   ; 2/4
	 3 ; 10ms : 40 10 10 10 -> 40 10 10 10 = 17.5 -> 19.5         -> 1     ; +9   ; 4/4
	RESIDUAL INJECTION								              -> 2     ; +0   ; 0/4
	 4 ; 10ms : 40 10 10 10 -> 10 10 10 10 = 10  -> 10            -> 1     ; +0   ; 0/4
	-----------------------------------------------------------------------------------

	updates without smoothing					: 4 ; 1 ; 1 ; 1 ; 1 = 8
	updates with	smoothing					: 1 ; 2 ; 2 ; 1 ; 1 = 7 [residual lost in the integer division history_sum / history_size]
	updates with	smoothing + tick_residual	: 1 ; 2 ; 2 ; 2 ; 1 = 8

	----------------------------------------------------------
*/

struct Frame_Controller{
	void create();
	void destroy();

	void add_snapping_frequency( u64 frequency );

	int update_time(u64 new_time);
	void resync_next_step();

	u64 time;
	u64 step_count;

	u64 step_multiplicity;
	u64 step_maximum;

	u64 tick_accumulator;
	u64 tick_residual;
	u64 tick_per_step;

	int snapping_count;
	u64 tick_snapping[8];
	double tick_snapping_error;

	u64 history_index;
	u64 history[4];
};

struct Pixel_Canvas{
	void create();
	void destroy();

	// Right-Handed Y-up ; X-right
	// Bottom-Left origin
	// Row Major layout

	void set_resolution(int width, int height);
	void set_pixel(int x, int y, RGBA color);
	void clear(RGBA color);

	int width;
	int height;
	RGBA* canvas;
};
