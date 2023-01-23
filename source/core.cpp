#include "core.h"

static u64 ROTL(u64 v, int nbit){
    return (v << nbit) | (v >> (64 - nbit));
}

static u64 xoroshiro128P_NEXT(Random_Data& data){
    u64 seed_low = data.seed_low;
	u64 seed_high = data.seed_high;

    u64 rand = seed_low + seed_high;

    seed_high ^= seed_low;
    data.seed_low = ROTL(seed_low, 24) ^ seed_high ^ (seed_high << 16);
    data.seed_high = ROTL(seed_high, 37);

    return rand;
}

// JUMP equivalent to 2^64 NEXT ie 2^64 non-overlapping sequences 
static void xoroshiro128P_JUMP(Random_Data& data){
    static const uint64_t JUMP[] = { 0xdf900294d8f554a5, 0x170865df4b3201fc };

	uint64_t seed_low = 0;
	uint64_t seed_high = 0;

    for (int ibit = 0; ibit != 64; ++ibit){
        if (JUMP[0] & u64(1) << ibit){
            seed_low ^= data.seed_low;
            seed_high ^= data.seed_high;
        }
		xoroshiro128P_NEXT(data);
    }

    for (int ibit = 0; ibit != 64; ++ibit){
        if (JUMP[1] & u64(1) << ibit){
            seed_low ^= data.seed_low;
            seed_high ^= data.seed_high;
        }
		xoroshiro128P_NEXT(data);
    }

    data.seed_low = seed_low;
    data.seed_high = seed_high;
}

// JUMP equivalent to 2^96 NEXT ie 2^32 non-overlapping sequences 
static void xoroshiro128P_LONG_JUMP(Random_Data& data){
    static const uint64_t LONG_JUMP[] = { 0xd2a98b26625eee7b, 0xdddf9b1090aa7ac1 };

	uint64_t seed_low = 0;
	uint64_t seed_high = 0;

    for (int ibit = 0; ibit != 64; ++ibit){
        if (LONG_JUMP[0] & u64(1) << ibit){
            seed_low ^= data.seed_low;
            seed_high ^= data.seed_high;
        }
		xoroshiro128P_NEXT(data);
    }

    for (int ibit = 0; ibit != 64; ++ibit){
        if (LONG_JUMP[1] & u64(1) << ibit){
            seed_low ^= data.seed_low;
            seed_high ^= data.seed_high;
        }
		xoroshiro128P_NEXT(data);
    }

    data.seed_low = seed_low;
    data.seed_high = seed_high;
}

thread_local Random_Data g_default_random;

void create_default_random(){
    g_default_random.seed_low = 0x357638792F423F45ULL;
    g_default_random.seed_high = 0x635266556A586E32ULL;

    //int nLJMP = thread_id();
	//for (int iLJMP = 0; iLJMP != nLJMP; ++iLJMP) xoroshiro128P_LONG_JUMP(g_default_random);
}

char random_char(Random_Data& data){ return xoroshiro128P_NEXT(data) >> 56; }
short random_short(Random_Data& data){ return xoroshiro128P_NEXT(data) >> 48; }
int random_int(Random_Data& data){ return xoroshiro128P_NEXT(data) >> 32; }
int64 random_int64(Random_Data& data){ return xoroshiro128P_NEXT(data); }

void Frame_Controller::create() { memset( this, 0x00, sizeof( Frame_Controller ) ); }
void Frame_Controller::destroy() {}

void Frame_Controller::add_snapping_frequency( u64 frequency )
{
	ram_assert( snapping_count < carray_size( Frame_Controller::tick_snapping ) - 1 );
	u64 ticks_per_second = g_timer->ticks_per_second();
	u64 tick_period = ticks_per_second / frequency + (ticks_per_second % frequency ? 1 : 0);
	tick_snapping[snapping_count++] = tick_period;
}

int Frame_Controller::update_time(u64 new_time)
{
	ram_assert( tick_per_step );
	ram_assert( step_multiplicity );
	ram_assert( step_maximum > step_multiplicity );

	// resync
	if( time == 0u )
	{
		new_time = max( new_time, tick_per_step * step_multiplicity );
		time = new_time - tick_per_step * step_multiplicity;
	}

	// timer anomaly correction (rewind and overshoot)
	new_time = max( time, new_time );
	s64 time_delta = new_time - time;
	time_delta = min(time_delta, (s64)(tick_per_step * step_maximum));

	// frequency snapping
	for( int isnap = 0; isnap != snapping_count; ++isnap )
	{
		u64 snap_period = tick_snapping[isnap];
		u64 snap_error = (u64)abs((s64)snap_period - time_delta);

		double error_ratio = (double)snap_error / (double)tick_per_step;
		if (error_ratio < tick_snapping_error){
			time_delta = snap_period;
			break;
		}
	}

	constexpr u64 history_size = carray_size(Frame_Controller::history);

	// history smoothing
	history[history_index] = time_delta;
	history_index = (history_index + 1u) % history_size;

	u64 history_sum = 0u;
	for (u32 ihistory = 0u; ihistory != history_size; ++ihistory) history_sum += history[ihistory];
	time_delta = history_sum / history_size;

	// tick residual
	u64 new_tick_residual = tick_residual + history_sum % history_size;
	time_delta += new_tick_residual / history_size;
	new_tick_residual = new_tick_residual % history_size;
	tick_residual = new_tick_residual;

	// accumulate and count steps
	tick_accumulator = tick_accumulator + time_delta;
	u64 nsteps = tick_accumulator / tick_per_step;

	nsteps = (nsteps / step_multiplicity) * step_multiplicity;

	if (nsteps > step_maximum){
		nsteps = step_maximum;
		resync_next_step();
	}
	else{
		tick_accumulator = tick_accumulator - nsteps * tick_per_step;
	}

	time = new_time;
	step_count = step_count + nsteps;

	return (int)nsteps;
}

void Frame_Controller::resync_next_step()
{
	time = 0u;
	tick_accumulator = 0u;
	tick_residual = 0u;

	history_index = 0u;
	for( int ihistory = 0; ihistory != carray_size(Frame_Controller::history); ++ihistory ) history[ihistory] = tick_per_step;
}

void Pixel_Canvas::create(){
	width = 0;
	height = 0;
	canvas = NULL;
}
void Pixel_Canvas::destroy(){
	free(canvas);
}

void Pixel_Canvas::set_resolution(int new_width, int new_height){
	size_t new_size = new_width * new_height * sizeof(RGBA);
	size_t current_size = width * height * sizeof(RGBA);
	if (new_size != current_size){
		free(canvas);
		canvas = (RGBA*)malloc(new_size);
	}
	width = new_width;
	height = new_height;
}

void Pixel_Canvas::set_pixel(int x, int y, RGBA color){
	ram_assert(x >= 0 && x < width);
	ram_assert(y >= 0 && y < height);

	canvas[y * width + x] = color;
}

void Pixel_Canvas::clear(RGBA color){
	int pix_count = width * height;
	for (int ipix = 0; ipix != pix_count; ++ipix) canvas[ipix] = color;

}
