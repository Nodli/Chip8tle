#pragma once

// REF: https://youtu.be/fcBZEZWGYek?t=2045 [Bluepoint memory allocations]

#define ram_project "Chip8tle"

#define ram_debug
//#define ram_release
//#define ram_retail

// ---- standard library

#include <cmath>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ---- external libraries

// ---- typedefs / defines

typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef unsigned int   uint;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef int8_t  int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

#define ram_concatenate_internal(expA, expB) expA##expB
#define ram_concatenate(expA, expB) ram_concatenate_internal(expA, expB)

#define ram_stringify_internal(exp) #exp
#define ram_stringify(exp) ram_stringify_internal(exp)

#define ram_filepath __FILE__
#define ram_function	__FUNC__
#define ram_linenumber	__LINE__

#define ram_path_separator '\\'

#define ram_file() []() consteval { constexpr size_t index = strrchr(ram_filepath, ram_project ram_path_separator); static_assert(index != SIZE_MAX); return ram_filepath + index; }()

#define ram_message(msg) __pragma(message(ram_filepath "(" ram_stringify(ram_linenumber) "):" #msg));
#define ram_todo(msg) ram_message(msg)

#define ram_cacheline_size 64

#define carray_size(var) (sizeof(var) / sizeof(*var))
#define cstring_size(str) (sizeof(str) - 1)

#define ignore_exp_internal(exp) do{ (void)sizeof(exp); }while(false)
#define ignore_exp(exp) ignore_exp_internal(exp)

#if defined(ram_debug) || defined(ram_release)
	#define ram_assert_dependency(exp) exp
	#define ram_debugbreak() do{ __debugbreak(); } while(false)
	#define ram_assert_internal(exp) do{ if((exp) == false){ ram_error("FAILED assert: " #exp); ram_debugbreak(); } }while(false)
	#define ram_assert(exp) ram_assert_internal(exp)
	#define ram_assert_msg_internal(exp, msg, ...) do{ if((exp) == false){ ram_error("FAILED assert: " #exp " with msg: " #msg); ram_debugbreak(); } }while(false)
	#define ram_assert_msg(exp, msg, ...) ram_assert_msg_internal(exp, msg, __VA_ARGS__)
#else
	#define ram_assert_dependency(exp)
	#define ram_debugbreak()
	#define ram_assert(exp) ignore_exp
	#define ram_assert_msg(exp, msg, ...) ignore_exp
#endif

#define ram_defer(captures) DEFER_Container ram_concatenate(DEFER_variable_at_, __LINE__) = [captures]() mutable

// ---- constants

template<typename T>
T max(T a, T b);
template<typename T>
T min(T a, T b);
template<typename T>
T clamp(T x, T min, T max);
template<typename T>
T abs(T x);

constexpr size_t Kilobytes( size_t size );
constexpr size_t Megabytes( size_t size );
constexpr size_t Gigabytes( size_t size );

template<typename T>
constexpr T PI = (T)3.14159265358979323846L;

// ---- API

int thread_id();
int main_thread_id();

char atomic_read(char volatile* src);
short atomic_read(short volatile* src);
uint atomic_read(uint volatile* src);
int64 atomic_read(int64 volatile* src);

short	atomic_increment(short volatile* dst);
uint	atomic_increment(uint volatile*	 dst);
int64	atomic_increment(int64 volatile* dst);

short	atomic_decrement(short volatile* dst);
uint	atomic_decrement(uint volatile*	 dst);
int64	atomic_decrement(int64 volatile* dst);

char	atomic_exchange(char volatile*	dst, char	src);
short	atomic_exchange(short volatile* dst, short	src);
uint	atomic_exchange(uint volatile*	dst, uint	src);
int64	atomic_exchange(int64 volatile* dst, int64	src);

char	atomic_compare_exchange(char volatile*	dst, char	src, char	cmp);
short	atomic_compare_exchange(short volatile*	dst, short	src, short	cmp);
uint	atomic_compare_exchange(uint volatile*	dst, uint	src, uint	cmp);
int64	atomic_compare_exchange(int64 volatile*	dst, int64	src, int64	cmp);

template<typename T>
struct Atomic{
	T get();
	void set(T new_value);

	T increment();
	T decrement();

	T exchange(T new_value);
	T compare_exchange(T expected, T new_value);

	volatile T value;
};

template<typename T>
struct alignas(64) Atomic_Aligned : Atomic<T> {};

struct alignas(8) Mutex{
	void acquire();
	void release();

	u8 memory[64];
};

void create_mutex(Mutex* mutex);
void destroy_mutex(Mutex* mutex);

struct alignas(8) MutexRW{
	void acquire_read();
	void release_read();

	void acquire_write();
	void release_write();

	u8 memory[8];
};

void create_mutexRW(MutexRW* mutex);
void destroy_mutexRW(MutexRW* mutex);

// Mu-ltiple Pro-ducers Si-ngle Co-nsumer 
template<typename T>
struct MuProSiCo{
	struct Link{
		T data;
		Link* next;
	};

	void create();
	void destroy();

	void push(Link * new_link);

	Link* get_everything_reversed();
	static Link* reverse_order(Link* head);

	Atomic<Link*> head; // [most recent -> ... -> least recent]
};

// REF: https://github.com/nothings/stb/blob/master/stb_ds.h  [Easy-to-use dynamic arrays and hash tables for C]

template<typename T>
struct array_raw{
	void create();
	void destroy();

	const T& operator[](const u64 index) const;
	T& operator[](const u64 index);

	void push(const T& v);
	void pop();
	void pop(T& v);
	void insert(u64 index, const T& v);
	void insert_swap(u64 index, const T& v);
	void remove(u64 index);
	void remove_swap(u64 index);

	u64 size() const;
	u64 capacity() const;
	void set_size(u64 new_size);
	void set_capacity(u64 new_capacity);

	T* data();
	const T* data() const;

	u64 size_bytes() const;
	u64 capacity_bytes() const;

	T* ptr;
	u64 ptr_size;
	u64 ptr_capacity;
};

extern int g_argc;
extern char** g_argv;

void create_argc_argv();
void destroy_argc_argv();

void crash(const char* context_format, ...);

void create_crash_handler();
void destroy_crash_handler();

struct Logger{
	enum log_type{
		log_info,
		log_warning,
		log_error,
		log_type_count
	};
	static constexpr const char* log_type_str[3u] = { "info", "warning", "error" };
	static_assert(carray_size(log_type_str) == log_type_count, "Mismatch in size between log_type_str and log_type_count");

	static constexpr size_t msg_max_size = 1024u;

	void log_message_from_macro(log_type type, int line, const char* filepath, const char* format, ...);
};

struct Log{
	Logger::log_type type;
	int line;
	const char* filepath;
	char message[Logger::msg_max_size];
};

void create_logger();
void destroy_logger();

extern Logger* g_logger;

#define ram_info(...)		do{ g_logger->log_message_from_macro(Logger::log_info,		ram_linenumber, ram_filepath, __VA_ARGS__); }while(false)
#define ram_warning(...)	do{ g_logger->log_message_from_macro(Logger::log_warning,	ram_linenumber, ram_filepath, __VA_ARGS__); }while(false)
#define ram_error(...)		do{ g_logger->log_message_from_macro(Logger::log_error,	ram_linenumber, ram_filepath, __VA_ARGS__); ram_debugbreak(); }while(false)

struct Window{
	void set_title(const char* title);

	void enable_resizing();
	void disable_resizing();

	void set_size(int width, int height);
	void get_size(int& width, int& height);

	int user_requested_close;
};
struct Window_Manager{
	Window* create_window();
	void destroy_window(Window* window);

	Mutex windows_mutex;
	array_raw<Window*> windows;
};

void create_window_manager();
void udpate_window_manager();
void destroy_window_manager();

ram_assert_dependency(extern Atomic<int> debug_update_window_manager_guard;)
extern Window_Manager* g_window_manager;

struct RGBA{
	u8 r;
	u8 g;
	u8 b;
	u8 a;
};
void copy_image_to_window(int width, int height, RGBA* data, Window* window);

struct Audio_DSP{
	void* get_param();
	void commit_param();

	void* audio_thread_update_param();
	void* audio_thread_access_internal_data();

	void (*process)(u32 frame_count, s16* output, void* param, void* internal_data);
	void (*destroy)(void* data);

	// transient_param is negative when the main thread exchanges
	//					  positive when the audio thread exchanges
	// param indices are offset by one because negative zero does not exist
	int user_param;
	Atomic<int> transient_param;
	int driver_param;

	size_t param_size;
	size_t param_offset;
	void* memory;
};

struct Audio{
	static constexpr u64 audio_latency_ms = 4;
	static constexpr u64 samples_per_second = 44100;
	static constexpr u32 max_DSP_count = 16;

	enum DSP_State{
		Available,
		Reserved,
		Active,
		Inactive,
		Destroyable
	};

	int audio_thread_id();

	Audio_DSP* create_DSP(size_t param_size, void* internal_data, size_t internal_data_size);
	void activate_DSP(Audio_DSP* DSP);
	int deactivate_DSP(Audio_DSP* DSP); // data dependencies must stay valid until audio_thread_counter != /returned_value/
	void destroy_destroyable_DSPs();

	Atomic<int> audio_thread_counter;
	Atomic<DSP_State> DSP_states[max_DSP_count];
	Audio_DSP DSPs[max_DSP_count];
};

void create_audio();
void update_audio();
void destroy_audio();

extern Audio* g_audio;

struct File_System{
	void ReadFile( const char* path, void*& data, size_t& data_size );
};

extern File_System* g_file_system;

void create_file_system();
void destroy_file_system();

struct Timer{
	u64 ticks();
	u64 ticks_per_second();

	float as_ms(u64 ticks);
	float as_seconds(u64 ticks);
};

extern Timer* g_timer;

void create_timer();
void destroy_timer();

struct Input{
	enum Device_Type
	{
		Device_None,
		Device_Keyboard,
	};
	enum Control_Type
	{
		Control_None,
		Control_Button,
	};
	enum Pairing_Mode{
		Pairing_None,
		Pairing_Most_Recent,
		Pairing_Most_Recent_Persistent,
	};

	struct Button{
		int down;
		int transition_count;
	};
	union Control_Data
	{
		Button button;
	};

	struct Action
	{
		const char* name;
		Control_Type type;
		u32 scancode;
		Control_Data data;
	};
	struct Listener
	{
		void register_action(const char* name, Control_Type type, u32 scancode);
		void unregister_action(const char* name);
		Control_Data get_action_status(const char* name);

		array_raw<Action> actions;
		Device_Type device_type;
		Pairing_Mode pairing_mode;
	};

	Listener* create_listener();
	void destroy_listener(Listener* listener);

	u64 frame_counter;

	Mutex listeners_mutex;
	array_raw<Listener*> listeners;
};

enum RAM_Key{
	RAMK_1,
	RAMK_2,
	RAMK_3,
	RAMK_4,
	RAMK_Q,
	RAMK_W,
	RAMK_E,
	RAMK_R,
	RAMK_A,
	RAMK_S,
	RAMK_D,
	RAMK_F,
	RAMK_Z,
	RAMK_X,
	RAMK_C,
	RAMK_V,
};
u32 RAMKey_to_scancode(RAM_Key key);

void create_input();
void update_input();
void destroy_input();

ram_assert_dependency(extern Atomic<int> debug_update_input_guard;)
extern Input* g_input;

struct Engine{
};

void create_engine();
void destroy_engine();

extern Engine* g_engine;

// ---- user interface

extern void (*g_game_create)();
extern int (*g_game_update)();
extern void (*g_game_render)();
extern void (*g_game_destroy)();

#include "engine.inl"
