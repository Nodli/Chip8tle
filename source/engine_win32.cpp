#include "engine.h"
#include "core.h"

// ---- platform includes

// REF: https://aras-p.info/blog/2018/01/12/Minimizing-windows.h/
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef NOMINMAX
#include <sysinfoapi.h>

#include <debugapi.h>	// DebugBreak, IsDebuggerPresent, OutputDebugStringA
#include <winuser.h>	// SendMessageA

#include <dwmapi.h>		// DwmFlush

#include <Fileapi.h>	// CreateFileA, DeleteFileA

#include <combaseapi.h> // CoInitializeEx

#include <profileapi.h> // QueryPerformanceCounter, QueryPerformanceFrequency

#include <Mmdeviceapi.h> // IMMNotificationClient, IMMDeviceEnumerator, IMMDevice
#include <Audioclient.h> // IAudioClient, IAudioRenderClient
#include <propsys.h>	 // IPropertyStore
#include <devpkey.h>	
#include <initguid.h>
#include <setupapi.h>
// PKEY_Device_FriendlyName

#include <libloaderapi.h> // LoadLibraryA

#include <Synchapi.h>	// CreateEventEx

#include <wchar.h>		// wcstombs

#include <intrin.h>

// ---- DECLARATION

struct Mutex_Win32{
	CRITICAL_SECTION csection;
};
static_assert(sizeof(Mutex_Win32) <= sizeof(Mutex), "Mutex_Win32 too big compared to Mutex");

struct MutexRW_Win32{
	SRWLOCK srwlock;
};
static_assert(sizeof(MutexRW_Win32) <= sizeof(MutexRW), "MutexRW_Win32 too bing compare to MutexRW");

struct Logger_Win32 : Logger {
};

struct Window_Win32 : Window {
	HWND handle;
};

LRESULT CALLBACK Window_Win32_WindowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam);

struct Window_Manager_Win32 : Window_Manager {
};

struct BGRA{
	u8 b;
	u8 g;
	u8 r;
	u8 a;
};

struct Audio_WASAPI : Audio {
	enum Request : int{
		None,
		Disconnect,
		Try_Connect,
		Destroy,
	};
	Atomic<Request> request;

	int connected;
	int started;

	HANDLE thread;
	HANDLE wake_event;
};

struct WASAPI_Watcher : IMMNotificationClient {
	HRESULT __stdcall QueryInterface(REFIID iid, void** ppUnk) override;

	// /!\ WARNING: WASAPI_Watcher must be on the stack and in Unregistered before destruction
	ULONG _stdcall AddRef() override;
	ULONG _stdcall Release() override;

	HRESULT _stdcall OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override;
	HRESULT _stdcall OnDeviceAdded(LPCWSTR pwstrDeviceId) override;
	HRESULT _stdcall OnDeviceRemoved(LPCWSTR pwstrDeviceId) override;
	HRESULT _stdcall OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
	HRESULT _stdcall OnPropertyValueChanged(LPCWSTR pwstrDefaultDeviceId, const PROPERTYKEY key) override;

	IMMDeviceEnumerator* enumerator;
};

DWORD WINAPI WASAPI_ThreadProc(LPVOID lpparam);

struct Listener_RawInput : Input::Listener{
	HANDLE device;
};

void create_Listener_RawInput(Listener_RawInput& listener);

struct Keyboard_RawInput{
	HANDLE device;
	u64 frame_timestamp;
	Input::Button keys[255];
};

void create_Keyboard_RawInput(Keyboard_RawInput& keyboard);

struct Input_RawInput : Input {
	array_raw<Keyboard_RawInput> keyboards;
};

void get_device_name(HANDLE device, char* buffer, size_t buffer_size);
int search_keyboard_by_device(Input_RawInput* input, HANDLE device);

struct Engine_Win32 : Engine {
};

#if 0
// REF: https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes [Virtual-Key Codes]
#define FOREACH_RAMVK_WIN32VK(FUNC) \
		FUNC(Input::VK_1, 0x31) \
		FUNC(Input::VK_2, 0x32) \
		FUNC(Input::VK_3, 0x33) \
		FUNC(Input::VK_4, 0x34) \
		FUNC(Input::VK_Q, 0x51) \
		FUNC(Input::VK_W, 0x57) \
		FUNC(Input::VK_E, 0x45) \
		FUNC(Input::VK_R, 0x52) \
		FUNC(Input::VK_A, 0x41) \
		FUNC(Input::VK_S, 0x53) \
		FUNC(Input::VK_D, 0x44) \
		FUNC(Input::VK_F, 0x46) \
		FUNC(Input::VK_Z, 0x5A) \
		FUNC(Input::VK_X, 0x58) \
		FUNC(Input::VK_C, 0x43) \
		FUNC(Input::VK_V, 0x56)

struct Keyboard_GameInput{
	void update_state_with_reading(IGameInputReading* new_reading);

	IGameInputDevice* device;
	IGameInputReading* reading;
	Input::Button keys[256];

	int temp_listener_index;
};

struct Input_GameInput : Input{
	struct Watcher_Message{
		enum Type{
			None,
			Connect,
			Disconnect,
		};
		Type type;
		IGameInputDevice* device;
	};

	void connect_keyboard(IGameInputDevice* device);
	void disconnect_keyboard(IGameInputDevice* device);
	void disconnect_keyboard(int keyboard_index);

	void process_watcher_messages();
	void update_device_states();
	void update_listener_pairings();
	void update_listener_states();

	IGameInput* igameinput;

	GameInputCallbackToken watcher_token;
	MuProSiCo<Watcher_Message> watcher_to_driver;

	array_raw<Keyboard_GameInput> keyboards;
};

void __stdcall GameInput_Watcher(GameInputCallbackToken callbackToken, void* context,
	IGameInputDevice* device, u64 timestamp,
	GameInputDeviceStatus currentStatus,
	GameInputDeviceStatus previousStatus);
#endif

// ---- EXTERN & STATIC DATA

static const char* g_window_class_name = "Window_Win32";
static HINSTANCE g_instance;
static DWORD g_main_thread_id;

static char g_argv_data[4096];
static char* g_argv_ptr_data[2048];

static struct Crash_Handler_Win32 {
	static constexpr size_t memory_size = Megabytes(4);

	CRITICAL_SECTION critical_section;
	void* memory;
} g_crash_handler;

static u8 g_RAMKey_to_scancode[255];

// ---- IMPLEMENTATION

char atomic_read(char volatile* src){ char copy = *src; return copy; }
short atomic_read(short volatile* src){ short copy = *src; return copy; }
uint atomic_read(uint volatile* src){ uint copy = *src; return copy; }
int64 atomic_read(int64 volatile* src){ int64 copy = *src; return copy; }

short atomic_increment(short volatile* dst){ return InterlockedIncrement16(dst); };
uint atomic_increment(uint volatile* dst){ return InterlockedIncrement(dst); };
int64 atomic_increment(int64 volatile* dst){ return InterlockedIncrement64(dst); };

short atomic_decrement(short volatile* dst){ return InterlockedDecrement16(dst); };
uint atomic_decrement(uint volatile* dst){ return InterlockedDecrement(dst); };
int64 atomic_decrement(int64 volatile* dst){ return InterlockedDecrement64(dst); };

char atomic_exchange(char volatile* dst, char src){ return InterlockedExchange8(dst, src); };
short atomic_exchange(short volatile* dst, short src){ return InterlockedExchange16(dst, src); };
uint atomic_exchange(uint volatile* dst, uint src){ return InterlockedExchange(dst, src); };
int64 atomic_exchange(int64 volatile* dst, int64 src){ return InterlockedExchange64(dst, src); };

char atomic_compare_exchange(char volatile* dst, char src, char cmp){ return _InterlockedCompareExchange8(dst, src, cmp); }
short atomic_compare_exchange(short volatile* dst, short src, short cmp){ return InterlockedCompareExchange16(dst, src, cmp); }
uint atomic_compare_exchange(uint volatile* dst, uint src, uint cmp){ return InterlockedCompareExchange(dst, src, cmp); }
int64 atomic_compare_exchange(int64 volatile* dst, int64 src, int64 cmp){ return InterlockedCompareExchange64(dst, src, cmp); }

void Mutex::acquire(){
	Mutex_Win32* win32 = (Mutex_Win32*)this;
	EnterCriticalSection(&win32->csection);
}

void Mutex::release(){
	Mutex_Win32* win32 = (Mutex_Win32*)this;
	LeaveCriticalSection(&win32->csection);
}

void create_mutex(Mutex* mutex){
	Mutex_Win32* win32 = (Mutex_Win32*)mutex;
	InitializeCriticalSection(&win32->csection);
}

void destroy_mutex(Mutex* mutex){
	Mutex_Win32* win32 = (Mutex_Win32*)mutex;
	DeleteCriticalSection(&win32->csection);
}

void MutexRW::acquire_read(){
	MutexRW_Win32* win32 = (MutexRW_Win32*)this;
	AcquireSRWLockShared(&win32->srwlock);
}

void MutexRW::release_read(){
	MutexRW_Win32* win32 = (MutexRW_Win32*)this;
	ReleaseSRWLockShared(&win32->srwlock);
}

void MutexRW::acquire_write(){
	MutexRW_Win32* win32 = (MutexRW_Win32*)this;
	AcquireSRWLockExclusive(&win32->srwlock);
}

void MutexRW::release_write(){
	MutexRW_Win32* win32 = (MutexRW_Win32*)this;
	ReleaseSRWLockExclusive(&win32->srwlock);
}

void create_mutexRW(MutexRW* mutex){
	MutexRW_Win32* win32 = (MutexRW_Win32*)&mutex;
	InitializeSRWLock(&win32->srwlock);
}

void destroy_mutexRW(MutexRW* mutex){
}

int thread_id(){
	return GetCurrentThreadId();
}

int main_thread_id(){
	return g_main_thread_id;
}

int is_whitespace( const char c ){ return c == ' ' || c == '\t'; }
void eat_whitespaces(const char*& str){ while (is_whitespace(*str)) ++str; }

// REF: https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments?view=msvc-170 [CommandLineA to argc, argv]
void create_argc_argv(){
	int argc = 0;

	const char* rcursor = GetCommandLineA();

	char* wcursor = g_argv_data;
	char** pcursor = g_argv_ptr_data;

	// first argument ; program name
	char* arg_begin = wcursor;
	while (!is_whitespace(*rcursor))
	{
		// parts surrounded by double quotes are allowed
		if( *rcursor == '\"' )
		{
			const char* rcopy = rcursor + 1;

			// find corresponding double quote
			while( *rcopy != '\"' ) ++rcopy;

			size_t copy_size = rcopy - rcursor - 1;
			memcpy( wcursor, rcursor + 1, copy_size );

			wcursor = wcursor + copy_size;
			rcursor = rcopy + 1;
		}
		else
		{
			*wcursor++ = *rcursor++;
		}
	}

	++argc;
	*wcursor++ = '\0';
	*pcursor++ = arg_begin;

	eat_whitespaces(rcursor);

	bool whitespace_as_literal = false;

	// other arguments
	while( *rcursor != '\0' )
	{
		arg_begin = wcursor;

		while ((whitespace_as_literal || !is_whitespace(*rcursor)) && *rcursor != '\0'){
			if (*rcursor == '\\')
			{
				// look for the character after the backslashes
				const char* rcopy = rcursor;
				while (*rcopy == '\\') ++rcopy;

				// backslashes escape themselves ie literal backslash per pair or backslashes
				if (*rcopy == '\"'){
					int bs_count = (int)(rcopy - rcursor);

					int bs_insert = bs_count / 2;
					for (int i = 0; i != bs_insert; ++i) *wcursor++ = '\\';

					rcursor = rcopy;

					// backslash + quotation mark is an escape sequence ie literal quotation mark
					if (bs_count % 2){
						*wcursor++ = '\"';
						++rcursor;
					}
				}
				// literal backslashes
				else{
					int bs_count = (int)(rcopy - rcursor);
					for (int i = 0; i != bs_count; ++i) *wcursor++ = '\\';
					rcursor = rcopy;
				}
			}
			else if (*rcursor == '\"')
			{
				if (!whitespace_as_literal){
					// look for the quotation mark pair or NUL
					const char* rcopy = rcursor + 1;
					while (*rcopy != '\"' && *rcopy != '\0') ++rcopy;

					// no pair ie literal quotation mark
					if (*rcopy == '\0') *wcursor++ = '\"';
				}

				whitespace_as_literal = !whitespace_as_literal;
				++rcursor;
			}
			else
			{
				*wcursor++ = *rcursor++;
			}
		}

		++argc;
		*wcursor++ = '\0';
		*pcursor++ = arg_begin;

		eat_whitespaces(rcursor);
	}

	g_argc = argc;
	g_argv = g_argv_ptr_data;
}

void destroy_argc_argv(){

}

constexpr size_t win32_error_size = 1024;

void win32_error(char* msg, size_t msg_size){
	DWORD error_code = GetLastError();
	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)msg, (DWORD)msg_size, NULL))
		snprintf(msg, msg_size, "FormatMessageA failed to identify the error code from GetLastError : %ul / 0x%x)", error_code, error_code);
}

#define ram_error_win32() do{ char msg[win32_error_size]; win32_error(msg, win32_error_size); ram_error("win32_error: %ul", msg); }while(false)

void crash( const char* context_format, ... )
{
	EnterCriticalSection(&g_crash_handler.critical_section);

	if(g_crash_handler.memory) VirtualFree(g_crash_handler.memory, 0, MEM_RELEASE);

	const char* title = ram_project " - FATAL ERROR";

	char context_msg[win32_error_size];
	va_list args;
	va_start(args, context_format);
	int context_msg_size = vsnprintf(context_msg, sizeof(context_msg), context_format, args);
	va_end(args);

	char error_msg[win32_error_size];
	win32_error(error_msg, win32_error_size);

	char msg[win32_error_size * 2 + 1];
	if (context_msg_size){
		memcpy(msg, context_msg, context_msg_size);
		msg[context_msg_size] = '\n';
	}
	memcpy(msg + context_msg_size + 1, error_msg, win32_error_size);

	MessageBoxA( NULL, msg, title, MB_OK | MB_ICONERROR | MB_SETFOREGROUND );

	ExitProcess( 42 );
}

void create_crash_handler()
{
	InitializeCriticalSection(&g_crash_handler.critical_section);

	void* memory = VirtualAlloc(NULL, Crash_Handler_Win32::memory_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!memory) crash("Failed to allocate crash handler memory");

	memset(memory, 0xFF, Crash_Handler_Win32::memory_size);
	g_crash_handler.memory = memory;
}

void destroy_crash_handler()
{
	VirtualFree( g_crash_handler.memory, 0, MEM_RELEASE );
	g_crash_handler.memory = NULL;

	DeleteCriticalSection(&g_crash_handler.critical_section);
}

void Logger::log_message_from_macro(log_type type, int line, const char* filepath, const char* format, ...){
	Logger_Win32* ptr = (Logger_Win32*)this;

	Log log;
	log.type = type;
	log.line = line;
	log.filepath = filepath;

	va_list args;
	va_start(args, format);
	int msg_size = vsnprintf(log.message, sizeof(Log::message), format, args);
	va_end(args);

	char output_scratch[Logger::msg_max_size + 1024u];

	// debug output
	if (IsDebuggerPresent()){
		size_t offset = 0;
		auto buffer_bytesize = [&]() -> size_t { return sizeof(output_scratch) - offset; };

		// filepath(line)
		{
			int write_len = snprintf(output_scratch + offset, buffer_bytesize(), "%s(%d):", filepath, line);
			offset += write_len;
		}

		// time
		{
			time_t cur_time = time(NULL);
			tm* loc_time = localtime(&cur_time);
			size_t time_size = strftime(output_scratch + offset, buffer_bytesize(), "[%H:%M:%S] ", loc_time);
			offset += time_size;
		}

		// type  
		{
			ram_assert(type < Logger::log_type_count);
			const char* type_str = Logger::log_type_str[type];
			size_t type_size = strlen(type_str);

			size_t copy_size = min(type_size, buffer_bytesize());
			for (int ichar = 0; ichar != copy_size; ++ichar)
				output_scratch[offset + ichar] = type_str[ichar];
			offset += copy_size;

			if (buffer_bytesize())
				output_scratch[offset++] = ' ';
		}

		// message
		{
			size_t copy_size = min((size_t)msg_size, buffer_bytesize());
			memcpy(output_scratch + offset, log.message, copy_size);
			offset += copy_size;
		}

		// trailing \n\0
		{
			size_t trailing_offset = min(offset + 2, sizeof(output_scratch)) - 2;
			output_scratch[trailing_offset] = '\n';
			output_scratch[trailing_offset + 1] = '\0';
		}

		OutputDebugStringA(output_scratch);
	}
}

void create_logger(){
	Logger_Win32* win32 = (Logger_Win32*)malloc(sizeof(Logger_Win32));
	g_logger = (Logger*)win32;
}

void destroy_logger(){
	Logger_Win32* win32 = (Logger_Win32*)g_logger;
	free(g_logger);
	g_logger = NULL;
}

void Window::set_title(const char* title){
	Window_Win32* win32 = (Window_Win32*)this;
	SetWindowTextA(win32->handle, title);
}

void Window::enable_resizing(){
	Window_Win32* win32 = (Window_Win32*)this;

	LONG_PTR new_style = GetWindowLongPtrA(win32->handle, GWL_STYLE);
	if (new_style){
		new_style |= (WS_SIZEBOX | WS_MAXIMIZEBOX);
		SetWindowLongPtrA(win32->handle, GWL_STYLE, new_style);
	}
}

void Window::disable_resizing(){
	Window_Win32* win32 = (Window_Win32*)this;

	LONG_PTR new_style = GetWindowLongPtrA(win32->handle, GWL_STYLE);
	if (new_style){
		new_style = new_style & ~(WS_SIZEBOX | WS_MAXIMIZEBOX);
		SetWindowLongPtrA(win32->handle, GWL_STYLE, new_style);
	}
}

// REF: https://devblogs.microsoft.com/oldnewthing/20131017-00/?p=2903 [Inverse of AdjustWindowRectEx]
// inverse of AdjustWindowRectEx ie compute client rect based on window rect
BOOL UnadjustWindowRectEx(LPRECT prc, DWORD dwStyle, BOOL fMenu, DWORD dwExStyle){
	RECT rc;
	SetRectEmpty(&rc);
	BOOL fRc = AdjustWindowRectEx(&rc, dwStyle, fMenu, dwExStyle);
	if (fRc) {
		prc->left -= rc.left;
		prc->top -= rc.top;
		prc->right -= rc.right;
		prc->bottom -= rc.bottom;
	}
	return fRc;
}

void Window::set_size(int width, int height){
	Window_Win32* win32 = (Window_Win32*)this;

	// adjusting the window rect based on the input client rect (width, height)
	RECT wrect;
	wrect.bottom = height;
	wrect.left = 0;
	wrect.top = 0;
	wrect.right = width;

	LONG_PTR wstyle = GetWindowLongPtrA(win32->handle, GWL_STYLE);
	LONG_PTR wexstyle = GetWindowLongPtrA(win32->handle, GWL_EXSTYLE);
	HMENU wmenu = GetMenu(win32->handle);
	AdjustWindowRectEx(&wrect, (DWORD)wstyle, wmenu ? true : false, (DWORD)wexstyle);

	int wwidth = wrect.right - wrect.left;
	int wheight = wrect.bottom - wrect.top;

	SetWindowPos(win32->handle, 0, 0, 0, wwidth, wheight, SWP_NOZORDER | SWP_NOMOVE);
}

void Window::get_size(int& width, int& height){
	Window_Win32* win32 = (Window_Win32*)this;
	RECT rect;
	if (GetClientRect(win32->handle, &rect)){
		width = rect.right - rect.left;
		height = rect.bottom - rect.top;
	}
	else if (GetWindowRect(win32->handle, &rect)){
		LONG_PTR wstyle = GetWindowLongPtrA(win32->handle, GWL_STYLE);
		LONG_PTR wexstyle = GetWindowLongPtrA(win32->handle, GWL_EXSTYLE);
		HMENU wmenu = GetMenu(win32->handle);
		UnadjustWindowRectEx(&rect, (DWORD)wstyle, wmenu ? true : false, (DWORD)wexstyle);
	}
	else{
		width = 0;
		height = 0;
	}
}

LRESULT CALLBACK Window_Win32_WindowProc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam){
	// GetWindowLongPtr returns 0 on failure (ie until the call to SetWindowLongPtrA)
	Window_Win32* win32 = (Window_Win32*)GetWindowLongPtrA(handle, GWLP_USERDATA);

	// https://learn.microsoft.com/en-us/windows/win32/winmsg/about-messages-and-message-queues#system-defined-messages
	switch (message){
	case WM_CREATE:
	{
		ram_info("WM_CREATE");
		CREATESTRUCT* createstruct = (CREATESTRUCT*)lparam;
		win32 = (Window_Win32*)createstruct->lpCreateParams;

		win32->user_requested_close = false;

		win32->handle = handle;
		SetWindowLongPtrA(handle, GWLP_USERDATA, (LONG_PTR)win32);

		return false;
	}
	case WM_SIZING:
	{
#if 0
		ram_info("WM_SIZING");
		RECT* new_wrect = (RECT*)lparam;

		LONG_PTR wstyle = GetWindowLongPtrA(win32->handle, GWL_STYLE);
		LONG_PTR wexstyle = GetWindowLongPtrA(win32->handle, GWL_EXSTYLE);
		HMENU wmenu = GetMenu(win32->handle);

		RECT new_crect = *new_wrect;
		UnadjustWindowRectEx(&new_crect, (DWORD)wstyle, wmenu ? true : false, (DWORD)wexstyle);

		int new_cwidth = new_crect.right - new_crect.left;
		int new_cheight = new_crect.bottom - new_crect.top;

		//if (win32->sizing_func)
		//	win32->sizing_func(new_cwidth, new_cheight);

		new_crect.right = new_crect.left + new_cwidth;
		new_crect.bottom = new_crect.top + new_cheight;

		AdjustWindowRectEx(&new_crect, (DWORD)wstyle, wmenu ? true : false, (DWORD)wexstyle);
		new_wrect->right = new_crect.right;
		new_wrect->bottom = new_crect.bottom;
#endif

		break;
	}
	case WM_PAINT:
	{
		ram_info("WM_PAINT");
		PAINTSTRUCT paint;
		HDC context = BeginPaint(handle, &paint);
		FillRect(context, &paint.rcPaint, (HBRUSH)COLOR_BACKGROUND);
		g_game_render();
		EndPaint(handle, &paint);
		return false;
	}
	case WM_CLOSE:
	{
		ram_info("WM_CLOSE");
		win32->user_requested_close = true;
		return false;
	}
	case WM_DESTROY:
	{
		ram_info("WM_DESTROY");
		ram_assert(
			[&](){
				Window_Manager_Win32* manager_win32 = (Window_Manager_Win32*)g_window_manager;
				int found_window = true;

				manager_win32->windows_mutex.acquire();

				for (int iwin = 0; iwin != manager_win32->windows.size(); ++iwin){
					if (manager_win32->windows[iwin] == win32){
						found_window = true;
						break;
					}
				}

				manager_win32->windows_mutex.release();

				return found_window;
			}()
		);
		return false;
	}
	case WM_INPUT:
	{
		ram_error("Unhandled WM_INPUT message");
		break;
	}
	}

	return DefWindowProcA(handle, message, wparam, lparam);
}

Window* Window_Manager::create_window(){
	ram_assert(!debug_update_window_manager_guard.get());

	Window_Win32* win32 = (Window_Win32*)malloc(sizeof(Window_Win32));

	HWND window_handle = CreateWindowExA(
		0,
		g_window_class_name,
		NULL,
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		g_instance,
		win32
	);
	if (!window_handle) ram_error("Failed to create_window");

	windows_mutex.acquire();
	windows.push(win32);
	windows_mutex.release();

	return win32;
}

static void destroy_window_ptr(Window_Win32* win32){
	DestroyWindow(win32->handle);
	free(win32);
}

void Window_Manager::destroy_window(Window* window){
	ram_assert(!debug_update_window_manager_guard.get());

	int found_window = false;

	windows_mutex.acquire();

	for (int iwindow = 0; iwindow != windows.size(); ++iwindow){
		Window_Win32* window_win32 = (Window_Win32*)windows[iwindow];
		if (window_win32 == window){
			windows.remove(iwindow);
			found_window = true;
			break;
		}
	}

	windows_mutex.release();

	if (!found_window) ram_error("Destroying a window that is unknown to the window manager");

	destroy_window_ptr((Window_Win32*)window);
}

void create_window_manager(){
	Window_Manager_Win32* win32 = (Window_Manager_Win32*)malloc(sizeof(Window_Manager_Win32));
	if (!win32) crash("Failed to allocate the window manager");

	ram_assert_dependency(debug_update_window_manager_guard.set(false));

	create_mutex(&win32->windows_mutex);

	win32->windows.create();
	g_window_manager = win32;

	WNDCLASSEXA wndclass = {};
	wndclass.cbSize = sizeof(WNDCLASSEXA);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = Window_Win32_WindowProc;
	wndclass.hInstance = g_instance;
	wndclass.lpszClassName = g_window_class_name;

	ATOM atom = RegisterClassExA(&wndclass);
	if (!atom) crash("Failed to register the window class");
}

void update_window_manager()
{
	ram_assert_dependency(debug_update_window_manager_guard.set(true);)
	ram_assert_dependency(debug_update_window_manager_guard.set(false);)
}

void destroy_window_manager(){
	Window_Manager_Win32* win32 = (Window_Manager_Win32*)g_window_manager;

	for (int iwin = 0; iwin != win32->windows.size(); ++iwin){
		Window_Win32* window = (Window_Win32*)win32->windows[iwin];
		if (window) destroy_window_ptr(window);
	}

	win32->windows.destroy();
	destroy_mutex(&win32->windows_mutex);

	free(g_window_manager);
	g_window_manager = NULL;
};

void copy_image_to_window(int width, int height, RGBA* data, Window* window){
	Window_Win32* window_win32 = (Window_Win32*)window;

	int win_width, win_height;
	window_win32->get_size(win_width, win_height);

	BITMAPINFO info;
	info.bmiHeader.biSize = sizeof(info);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = sizeof(BGRA) * 8;
	info.bmiHeader.biCompression = BI_RGB;

	// TODO: use scratch memory
	BGRA* BITMAP_DATA = (BGRA*)malloc(width * height * sizeof(BGRA));
	for (int ipix = 0; ipix != width * height; ++ipix){
		BITMAP_DATA[ipix].b = data[ipix].b;
		BITMAP_DATA[ipix].g = data[ipix].g;
		BITMAP_DATA[ipix].r = data[ipix].r;
		BITMAP_DATA[ipix].a = data[ipix].a;
	}

	HDC device_context = GetDC(window_win32->handle);
	if (device_context){
		StretchDIBits(device_context,
			0, 0, win_width, win_height,
			0, 0, width, height,
			BITMAP_DATA, &info,
			DIB_RGB_COLORS, SRCCOPY
		);
		ReleaseDC(window_win32->handle, device_context);
	}

	free(BITMAP_DATA);
}

int Audio::audio_thread_id(){
	Audio_WASAPI* wasapi = (Audio_WASAPI*)this;
	return GetThreadId(wasapi->thread);
}

HRESULT WASAPI_Watcher::QueryInterface(REFIID iid, void** ppUnk){
	if (iid == __uuidof(IMMNotificationClient) || iid == __uuidof(IUnknown)){
		*ppUnk = (IMMNotificationClient*)this;
		return S_OK;
	}

	*ppUnk = NULL;
	return E_NOINTERFACE;
}

void create_threading(){
}
void destroy_threading(){
}

ULONG WASAPI_Watcher::AddRef() { return 1; };
ULONG WASAPI_Watcher::Release(){ return 1; };

HRESULT WASAPI_Watcher::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState){ return S_OK; }
HRESULT WASAPI_Watcher::OnDeviceAdded(LPCWSTR pwstrDeviceId){ return S_OK; };
HRESULT WASAPI_Watcher::OnDeviceRemoved(LPCWSTR pwstrDeviceId){ return S_OK; };
HRESULT WASAPI_Watcher::OnPropertyValueChanged(LPCWSTR pwstrDefaultDeviceId, const PROPERTYKEY key){ return S_OK; };

HRESULT WASAPI_Watcher::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId){
	if (flow == eRender && role == eConsole){
		Audio_WASAPI* wasapi = (Audio_WASAPI*)g_audio;
		wasapi->request.set(Audio_WASAPI::Try_Connect);
		SetEvent(wasapi->wake_event);
	}
	return S_OK;
};

void get_IMMDevice_FriendlyName(IMMDevice* device, char device_name[2048]){
	IPropertyStore* device_property_store = NULL;
	PROPVARIANT name;
	PROPERTYKEY PKEY_Device_FriendlyName;
	size_t mbsize;
	HRESULT tmp;

	PropVariantInit(&name);

	tmp = device->OpenPropertyStore(STGM_READ, &device_property_store);
	if (FAILED(tmp)) goto failure;

	PKEY_Device_FriendlyName.pid = 14;
	PKEY_Device_FriendlyName.fmtid = { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } };

	tmp = device_property_store->GetValue(PKEY_Device_FriendlyName, &name);
	if (FAILED(tmp)) goto failure;

	mbsize = wcstombs(device_name, name.pwszVal, 2047);
	if (mbsize == -1) goto failure;

	device_name[mbsize] = '\0';
	goto cleanup;

failure:
	memcpy(device_name, "UNKNOWN", sizeof("UNKNOWN"));

cleanup:
	PropVariantClear(&name);
	if (device_property_store) device_property_store->Release();
}

// WASAPI_use_wake_event uses less CPU because of the wait
// but this requires audio_latency_ms to be quite high (16+ ms)
// otherwise we get audio glitches (ghost frequencies, pops, ...)
static int WASAPI_use_wake_event = false;

// REF:
// https://gist.github.com/mmozeiko/38c64bb65855d783645c [Handmade Hero WASAPI Port on Main Thread]
// https://hero.handmade.network/forums/code-discussion/t/8433-correct_implementation_of_wasapi [WASAPI Audio Thread]

DWORD WINAPI WASAPI_ThreadProc(LPVOID lpparam){
	Audio_WASAPI* wasapi = (Audio_WASAPI*)g_audio;

	wasapi->request.set(Audio_WASAPI::Try_Connect);
	wasapi->connected = false;
	wasapi->started = false;

	HRESULT coinit = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);
	if (FAILED(coinit)) crash("Failed to CoInitializeEx the audio thread");

	IMMDeviceEnumerator* enumerator;
	HRESULT cocreate = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator));
	if(FAILED(cocreate)) crash("Failed to CoCreateInstance the MMDeviceEnumerator");

	WASAPI_Watcher watcher;
	watcher.enumerator = enumerator;
	HRESULT regwatcher = enumerator->RegisterEndpointNotificationCallback(&watcher);
	if (FAILED(regwatcher)) crash("Failed to register the IMMNotificationClient");

	HANDLE wake_event = CreateEventExA(NULL, NULL, CREATE_EVENT_INITIAL_SET, SYNCHRONIZE | EVENT_MODIFY_STATE);
	if (!wake_event) crash("Failed to CreateEventEx the wake event");
	wasapi->wake_event = wake_event;

	IMMDevice* device = NULL;
	IAudioClient* client_audio = NULL;
	IAudioRenderClient* client_render = NULL;

	u32 latency_frames = (u32)ceil(Audio::audio_latency_ms * ((double)Audio::samples_per_second / 1000.));
	u32 buffer_frames = 0u;

	Audio_WASAPI::Request current_request;

	while (true){
		if(WASAPI_use_wake_event) WaitForSingleObject(wake_event, INFINITE);

		current_request = wasapi->request.exchange(Audio_WASAPI::None);

		if (current_request == Audio_WASAPI::Destroy) break;

		// disconnect
		if (current_request == Audio_WASAPI::Disconnect || current_request == Audio_WASAPI::Try_Connect){
			wasapi->connected = false;

			if (client_audio) client_audio->Stop();

			if (client_render) client_render->Release();
			if (client_audio) client_audio->Release();
			if (device) device->Release();

			client_render = NULL;
			client_audio = NULL;
			device = NULL;
		}

		// reconnect
		if (current_request == Audio_WASAPI::Try_Connect){
			int cleanup_stage = 0;

			HRESULT result;

			WAVEFORMATEXTENSIBLE waveformat;
			DWORD flags;
			REFERENCE_TIME duration;

			result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
			if (FAILED(result)) goto Try_Connect_finish;
			++cleanup_stage;

			result = device->Activate(__uuidof(IAudioClient), CLSCTX_INPROC_SERVER, NULL, (void**)&client_audio);
			if (FAILED(result)) goto Try_Connect_finish;
			++cleanup_stage;

			waveformat.Format.cbSize = 22;
			waveformat.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
			waveformat.Format.wBitsPerSample = 16;
			waveformat.Format.nChannels = 1;
			waveformat.Format.nSamplesPerSec = Audio_WASAPI::samples_per_second;
			// nBlockAlign = bytes per sample / frame
			waveformat.Format.nBlockAlign = (waveformat.Format.nChannels * waveformat.Format.wBitsPerSample) / 8;
			waveformat.Format.nAvgBytesPerSec = waveformat.Format.nSamplesPerSec * waveformat.Format.nBlockAlign;
			waveformat.Samples.wValidBitsPerSample = waveformat.Format.wBitsPerSample;
			waveformat.dwChannelMask = KSAUDIO_SPEAKER_MONO;
			waveformat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

			flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
			duration = Audio_WASAPI::audio_latency_ms * 10000;

			result = client_audio->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, duration, 0, (WAVEFORMATEX*)&waveformat, &GUID_NULL);
			if (FAILED(result)) goto Try_Connect_finish;

			result = client_audio->GetService(IID_PPV_ARGS(&client_render));
			if (FAILED(result)) goto Try_Connect_finish;
			++cleanup_stage;

			result = client_audio->SetEventHandle(wake_event);
			if (FAILED(result)) goto Try_Connect_finish;

			result = client_audio->GetBufferSize(&buffer_frames);
			if (FAILED(result)) goto Try_Connect_finish;

			// success ; don't clean up and continue
			wasapi->connected = true;
			wasapi->started = false;
			cleanup_stage = -1;

		Try_Connect_finish:
			switch (cleanup_stage){
			default:
			case 3:
				client_render->Release();
				client_render = NULL;
			case 2:
				client_audio->Release();
				client_audio = NULL;
			case 1:
				device->Release();
				device = NULL;
			case 0:
				ram_info("WASAPI_ThreadProc failed to connect to a DefaultAudioEndpoint");
				break;
			case -1:
				char device_name[2048];
				get_IMMDevice_FriendlyName(device, device_name);
				ram_info("WASAPI_ThreadProc succesfully connected to the DefaultAudioEndpoint [%s]", device_name);
				break;
			}
		}

		// stream audio ; bail out but don't cleanup on failure
		if (wasapi->connected){
			HRESULT result;

			u32 buffer_padding;
			u32 buffer_available;
			u32 frames_to_write;

			result = client_audio->GetCurrentPadding(&buffer_padding);
			if (FAILED(result)) goto Finish_Frame;

			buffer_available = buffer_frames - buffer_padding;
			frames_to_write = min(buffer_available, latency_frames);

			if (frames_to_write){
				BYTE* data;
				result = client_render->GetBuffer(frames_to_write, &data);
				if (FAILED(result)) goto Finish_Frame;

				memset(data, 0, frames_to_write * sizeof(s16));

				// execute DSP processors
				for (int iDSP = 0; iDSP != Audio::max_DSP_count; ++iDSP){
					Audio::DSP_State state = g_audio->DSP_states[iDSP].get();
					if ( state == Audio::Active){
						Audio_DSP& DSP = g_audio->DSPs[iDSP];
						void* param = DSP.audio_thread_update_param();
						DSP.process(frames_to_write, (s16*)data, param, DSP.audio_thread_access_internal_data());
					}
					else if (state == Audio::Inactive){
						g_audio->DSP_states[iDSP].set(Audio::Destroyable);
					}
				}

				result = client_render->ReleaseBuffer(frames_to_write, 0);
				if (FAILED(result)) goto Finish_Frame;

				if (!wasapi->started){
					client_audio->Start();
					wasapi->started = true;
				}
			}
		}
	Finish_Frame:
		continue;
	}

	if (client_audio) client_audio->Stop();

	if (client_render) client_render->Release();
	if (client_audio) client_audio->Release();
	if (device) device->Release();

	CloseHandle(wake_event);
	enumerator->UnregisterEndpointNotificationCallback(&watcher);
	enumerator->Release();
	CoUninitialize();

	return 0;
}

void create_audio(){
	Audio_WASAPI* wasapi = (Audio_WASAPI*)malloc(sizeof(Audio_WASAPI));
	wasapi->audio_thread_counter.set(0);
	for (int istate = 0; istate != Audio::max_DSP_count; ++istate) wasapi->DSP_states[istate].set(Audio::Available);
	wasapi->request.set(Audio_WASAPI::None);
	wasapi->connected = false;
	wasapi->started = false;
	wasapi->thread = NULL;
	wasapi->wake_event = NULL;
	g_audio = (Audio*)wasapi;

	HANDLE handle = CreateThread(NULL, 0, WASAPI_ThreadProc, NULL, 0, NULL);
	if (!handle) crash("Failed to create audio thread");
	wasapi->thread = handle;

	SetThreadPriority(handle, THREAD_PRIORITY_TIME_CRITICAL);
}

void update_audio()
{
	Audio_WASAPI* wasapi = (Audio_WASAPI*)g_audio;
	wasapi->destroy_destroyable_DSPs();
}

void destroy_audio(){
	Audio_WASAPI* wasapi = (Audio_WASAPI*)g_audio;

	ram_assert(
		[&](){
			for (int iDSP = 0; iDSP != Audio::max_DSP_count; ++iDSP){
				if (wasapi->DSP_states[iDSP].get() == Audio::Reserved)
					return false;
			}
			return true;
		}()
	);

	// inactivate
	for (int iDSP = 0; iDSP != Audio::max_DSP_count; ++iDSP)
		wasapi->DSP_states[iDSP].compare_exchange(Audio::Active, Audio::Inactive);

	// wait until available or destroyable
	u32 ready_DSP_count;
	do{
		ready_DSP_count = 0;
		for (int iDSP = 0; iDSP != Audio::max_DSP_count; ++iDSP){
			Audio::DSP_State state = wasapi->DSP_states[iDSP].get();

			if (state == Audio::Available || state == Audio::Destroyable)
				++ready_DSP_count;
		}
	} while (ready_DSP_count != Audio::max_DSP_count);

	wasapi->destroy_destroyable_DSPs();

	wasapi->request.set(Audio_WASAPI::Destroy);
	SetEvent(wasapi->wake_event);
	WaitForSingleObject(wasapi->thread, INFINITE);
	CloseHandle(wasapi->thread);

	free(g_audio);
	g_audio = NULL;
}

struct File_System_Win32{
};

void File_System::ReadFile(const char* path, void*& data, size_t& data_size){
	File_System_Win32* win32 = (File_System_Win32*)this;

	DWORD access = GENERIC_READ | GENERIC_WRITE;
	DWORD share_mode = 0;
	DWORD creation = OPEN_EXISTING;
	DWORD attribute = FILE_ATTRIBUTE_NORMAL;

	HANDLE handle = CreateFileA(path, access, share_mode, NULL, creation, attribute, NULL);
	if (handle == INVALID_HANDLE_VALUE){
		ram_error_win32();
		ram_error("Failed to CreateFileA with path : %s", path);
		data_size = 0;
		data = NULL;
	}

	LARGE_INTEGER size;
	GetFileSizeEx(handle, &size);

	void* memory = malloc(size.QuadPart);

	LONGLONG read_total = 0;
	while (read_total != size.QuadPart){
		DWORD read_size = (DWORD)min((LONGLONG)0xFFFFFFFF, size.QuadPart - read_total);
		::ReadFile(handle, (u8*)memory + read_total, (DWORD)size.QuadPart, NULL, NULL);
		read_total = read_total + read_size;
	}

	CloseHandle(handle);

	data_size = size.QuadPart;
	data = memory;
}

void create_file_system(){
	File_System_Win32* win32 = (File_System_Win32*)malloc(sizeof(File_System));
	g_file_system = (File_System*)win32;
}

void destroy_file_system(){
	free(g_file_system);
	g_file_system = NULL;
}

struct Timer_Win32{
	u64 frequency;
};

u64 Timer::ticks(){
	Timer_Win32* win32 = (Timer_Win32*)this;

	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	return counter.QuadPart;
}

u64 Timer::ticks_per_second(){
	Timer_Win32* win32 = (Timer_Win32*)this;
	return win32->frequency;
}

float Timer::as_ms(u64 ticks){
	Timer_Win32* win32 = (Timer_Win32*)this;
	double ms = (double)ticks / (double)ticks_per_second() * 1000.;
	return (float)ms;
}

float Timer::as_seconds(u64 ticks){
	Timer_Win32* win32 = (Timer_Win32*)this;
	double seconds = (double)ticks / (double)ticks_per_second();
	return (float)seconds;
}

void create_timer(){
	Timer_Win32* win32 = (Timer_Win32*)malloc(sizeof(Timer_Win32));

	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	win32->frequency = frequency.QuadPart;

	g_timer = (Timer*)win32;
}

void destroy_timer(){
	free(g_timer);
	g_timer = NULL;
}

Input::Listener* Input::create_listener(){
	ram_assert(!debug_update_input_guard.get());

	Listener_RawInput* rawinput = (Listener_RawInput*)malloc(sizeof(Listener_RawInput));
	rawinput->actions.create();
	rawinput->device_type = Input::Device_None;
	rawinput->pairing_mode = Input::Pairing_None;

	create_Listener_RawInput(*rawinput);

	listeners_mutex.acquire();
	listeners.push(rawinput);
	listeners_mutex.release();

	return rawinput;
}

void Input::destroy_listener(Input::Listener* to_destroy){
	ram_assert(!debug_update_input_guard.get());

	int found_listener = false;

	listeners_mutex.acquire();

	for (int ilistener = 0; ilistener != listeners.size(); ++ilistener){
		Listener_RawInput* listener = (Listener_RawInput*)listeners[ilistener];
		if (listener == to_destroy){
			listeners.remove(ilistener);
			found_listener = true;
			break;
		}
	}

	listeners_mutex.release();

	if (!found_listener) ram_error("Destroying a listener that is unknown to the input manager");

	to_destroy->actions.destroy();
}

#if 0
void Keyboard_GameInput::update_state_with_reading(IGameInputReading* new_reading){
	GameInputKeyState scratch_keystates[32];

	ram_assert(new_reading->GetKeyCount() <= carray_size(scratch_keystates));
	u32 state_count = new_reading->GetKeyState(carray_size(scratch_keystates), scratch_keystates);

	u8 scratch_down[carray_size(Keyboard_GameInput::keys)];
	memset(scratch_down, 0x00, carray_size(scratch_down));

	for (int istate = 0; istate != state_count; ++istate){
		u8 win32VK = scratch_keystates[istate].virtualKey;
		u8 ramVK = g_input_map.VK_win32_to_ram[win32VK];
		if (ramVK != UINT8_MAX){
			scratch_down[ramVK] = true;
		}
	}

	for (int ikey = 0; ikey != carray_size(Keyboard_GameInput::keys); ++ikey){
		int transition = (keys[ikey].down != scratch_down[ikey]) ? 1 : 0;
		keys[ikey].transition_count += transition;
		keys[ikey].down = scratch_down[ikey];
	}

	if(reading) reading->Release();
	reading = new_reading;
	new_reading->AddRef();
}

static const char* get_IGameInputDevice_name(IGameInputDevice* device){
	const GameInputDeviceInfo* info = device->GetDeviceInfo();
	const char* device_name = info->displayName ? info->displayName->data : "UNKNOWN";
	return device_name;
}

void Input_GameInput::connect_keyboard(IGameInputDevice* device){
	ram_assert_dependency(debug_update_input_guard.get());

	const char* device_name = get_IGameInputDevice_name(device);
	ram_info("Input_GameInput::connect_keyboard with device [%s](%p)", device_name, device);

	for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
		if (keyboards[ikeyb].device == device){
			ram_error("Failed to register the keyboard: device already registered");
			return;
		}
	}

	Keyboard_GameInput keyboard;
	memset(&keyboard, 0x00, sizeof(Keyboard_GameInput));

	IGameInputReading* reading;
	HRESULT get_reading = igameinput->GetCurrentReading(GameInputKindKeyboard, device, &reading);
	if (FAILED(get_reading)){
		ram_error("Failed to get a reading from the keyboard during connect_keyboard");
		return;
	}

	keyboard.update_state_with_reading(reading);

	device->AddRef();
	keyboard.device = device;

	keyboards.push(keyboard);
}

void Input_GameInput::disconnect_keyboard(IGameInputDevice* device){
	ram_assert_dependency(debug_update_input_guard.get());

	const char* device_name = get_IGameInputDevice_name(device);
	ram_info("Input_GameInput::disconnect_keyboard with device [%s](%p)", device_name, device);

	int keyboard_index = -1;

	for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
		if (keyboards[ikeyb].device == device){
			keyboard_index = ikeyb;
			break;
		}
	}

	if (keyboard_index == -1){
		ram_error("Failed to unregister the keyboard: device not registered");
		return;
	}

	disconnect_keyboard(keyboard_index);
}

void Input_GameInput::disconnect_keyboard(int keyboard_index){
	ram_assert(keyboards[keyboard_index].device != NULL);

	if (keyboards[keyboard_index].reading) keyboards[keyboard_index].reading->Release();

	keyboards[keyboard_index].device->Release();
	keyboards.remove(keyboard_index);
}

void Input_GameInput::process_watcher_messages(){
	ram_assert_dependency(debug_update_input_guard.get());

	typedef MuProSiCo<Input_GameInput::Watcher_Message>::Link Msg_Link;

	Msg_Link* link = watcher_to_driver.get_everything_reversed();
	link = MuProSiCo<Input_GameInput::Watcher_Message>::reverse_order(link);

	while (link){
		if (link->data.type == Input_GameInput::Watcher_Message::Connect)
			connect_keyboard(link->data.device);
		else if (link->data.type == Input_GameInput::Watcher_Message::Disconnect)
			disconnect_keyboard(link->data.device);

		Msg_Link* next = link->next;

		link->data.device->Release();
		free(link);

		link = next;
	}
}

void Input_GameInput::update_device_states(){
	ram_assert_dependency(debug_update_input_guard.get());

	for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
		Keyboard_GameInput& keyboard = keyboards[ikeyb];
		if (keyboard.device){
			for (int ikey = 0; ikey != carray_size(Keyboard_GameInput::keys); ++ikey)
				keyboard.keys[ikey].transition_count = 0;

			IGameInputReading* current_reading = keyboard.reading;
			IGameInputReading* next_reading;
			while (SUCCEEDED(igameinput->GetNextReading(current_reading, GameInputKindKeyboard, keyboard.device, &next_reading))){
				keyboard.update_state_with_reading(next_reading);
				current_reading = next_reading;
			}
		}
	}
}

void Input_GameInput::update_listener_pairings(){
	ram_assert_dependency(debug_update_input_guard.get());

	// invvalidate device to listener pairing
	for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb) keyboards[ikeyb].temp_listener_index = -1;

	// validate pairing states on listeners (and mark devices)
	for (int ilist = 0; ilist != listeners.size(); ++ilist){
		Listener_Win32* listener = (Listener_Win32*)listeners[ilist];

		if (listener->pairing_device){
			int device_index = -1;

			for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
				if (keyboards[ikeyb].device == listener->pairing_device){
					keyboards[ikeyb].temp_listener_index = ilist;
					device_index = ikeyb;
					break;
				}
			}

			if (device_index == -1) listener->pairing_device = NULL;
			listener->temp_device_index = device_index;
		}
	}

	// setup new pairings based on pairing mode

	// Phase 1 : pair devices to listeners that don't have devices and want one
	for (int ilist = 0; ilist != listeners.size(); ++ilist){
		Listener_Win32* listener = (Listener_Win32*)listeners[ilist];

		// see Phase 2
		if (listener->pairing_device) continue;

		// find a new pairing when necessary
		if (listener->pairing_mode == Pairing_Most_Recent || listener->pairing_mode == Pairing_Most_Recent_Persistent){
			u64 max_timestamp = 0;
			int keyboard_index = -1;

			for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
				Keyboard_GameInput& keyboard = keyboards[ikeyb];
				u64 timestamp;
				if (keyboard.temp_listener_index == -1 && (timestamp = keyboard.reading->GetTimestamp()) > max_timestamp){
					max_timestamp = timestamp;
					keyboard_index = ikeyb;
				}
			}

			if (keyboard_index != -1){
				listener->pairing_device = keyboards[keyboard_index].device;
				listener->temp_device_index = keyboard_index;
				keyboards[keyboard_index].temp_listener_index = ilist;
			}
		}
	}

	// Phase 2 : pair devices to listeners that already have a device but that may want to change
	for (int ilist = 0; ilist != listeners.size(); ++ilist){
		Listener_Win32* listener = (Listener_Win32*)listeners[ilist];

		// see Phase 1
		if (!listener->pairing_device) continue;

		// find a new pairing when necessary
		if (listener->pairing_mode == Pairing_Most_Recent_Persistent){
			ram_assert(listener->temp_device_index != -1);
			int current_device_index = listener->temp_device_index;
			Keyboard_GameInput& current_keyboard = keyboards[current_device_index];

			u64 max_timestamp = current_keyboard.reading->GetTimestamp();
			int keyboard_index = current_device_index;

			for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
				Keyboard_GameInput& keyboard = keyboards[ikeyb];
				u64 timestamp;
				if (keyboard.temp_listener_index == -1 && (timestamp = keyboard.reading->GetTimestamp()) > max_timestamp){
					max_timestamp = timestamp;
					keyboard_index = ikeyb;
				}
			}

			if (keyboard_index != listener->temp_device_index){
				keyboards[current_device_index].temp_listener_index = -1;

				listener->pairing_device = keyboards[keyboard_index].device;
				listener->temp_device_index = keyboard_index;
				keyboards[keyboard_index].temp_listener_index = ilist;
			}
		}
	}
}

void Input_GameInput::update_listener_states(){
	ram_assert_dependency(debug_update_input_guard.get());

	for (int ikeyb = 0; ikeyb != keyboards.size(); ++ikeyb){
		Keyboard_GameInput& keyboard = keyboards[ikeyb];

		if (keyboard.temp_listener_index != -1){
			Listener* listener = listeners[keyboard.temp_listener_index];

			for (int iaction = 0; iaction != listener->actions.size(); ++iaction){
				Action& action = listener->actions[iaction];

				ram_assert(action.control_identifier >= 0
					&& action.control_identifier <= carray_size(Input_Map_Win32::VK_ram_to_win32)
					&& g_input_map.VK_ram_to_win32[action.control_identifier] != 0xFF);

				Button keyboard_button = keyboard.keys[action.control_identifier];
				action.data.button = keyboard_button;
			}
		}
	}
}

void __stdcall GameInput_Watcher(GameInputCallbackToken callbackToken, void* context,
	IGameInputDevice* device, u64 timestamp,
	GameInputDeviceStatus currentStatus,
	GameInputDeviceStatus previousStatus){

	const char* device_name = get_IGameInputDevice_name(device);
	ram_info("GameInput_Watcher notified for device [%s](%p) currentState=%d previousState=%d", device_name, device, currentStatus, previousStatus);

	typedef Input_GameInput::Watcher_Message Msg;
	typedef MuProSiCo<Input_GameInput::Watcher_Message>::Link Msg_Link;

	Msg::Type type = Msg::None;
	const char* typestr = "None";

	if ((currentStatus & GameInputDeviceConnected) == GameInputDeviceConnected){
		type = Input_GameInput::Watcher_Message::Connect;
		typestr = "Connect";
	}
	else{
		type = Input_GameInput::Watcher_Message::Disconnect;
		typestr = "Disconnect";
	}

	if (type != Msg::None){
		ram_info("GameInput_Watcher pushing msg with type %s", typestr);
		Msg_Link* link = (Msg_Link*)malloc(sizeof(Msg_Link));
		link->data.type = type;

		device->AddRef();
		link->data.device = device;

		Input_GameInput* driver = (Input_GameInput*)context;
		driver->watcher_to_driver.push(link);
	}
}

static void create_Keybaord_GameInput(Keyboard_GameInput& keyboard){
	keyboard.device = NULL;
	keyboard.reading = NULL;
	for (int ikey = 0; ikey != carray_size(Keyboard_GameInput::keys); ++ikey)
	{
		Input::Button& button = keyboard.keys[ikey];
		button.down = 0;
		button.transition_count = 0;
	}
}

void create_input(){
	memset(g_input_map.VK_ram_to_win32, 0xFF, sizeof(Input_Map_Win32::VK_ram_to_win32));
	memset(g_input_map.VK_win32_to_ram, 0xFF, sizeof(Input_Map_Win32::VK_win32_to_ram));

#define REGISTER_VK_MAPPING(RAMVK, WIN32VK) g_input_map.VK_ram_to_win32[RAMVK] = WIN32VK; g_input_map.VK_win32_to_ram[WIN32VK] = RAMVK;
	FOREACH_RAMVK_WIN32VK(REGISTER_VK_MAPPING)
#undef REGISTER_VK_MAPPING

	Input_GameInput* gameinput = (Input_GameInput*)malloc(sizeof(Input_GameInput));

	create_mutex(&gameinput->listeners_mutex);
	gameinput->listeners.create();

	gameinput->igameinput = NULL;
	gameinput->watcher_token = GAMEINPUT_INVALID_CALLBACK_TOKEN_VALUE;
	gameinput->watcher_to_driver.create();
	gameinput->keyboards.create();

	IGameInput* igameinput;
	HRESULT create = GameInputCreate(&igameinput);
	if (FAILED(create)) crash("Failed to GameInputCreate");
	gameinput->igameinput = igameinput;

	igameinput->RegisterDeviceCallback(
		NULL,
		GameInputKindKeyboard,
		GameInputDeviceConnected,
		GameInputBlockingEnumeration,
		(void*)gameinput,
		GameInput_Watcher,
		&gameinput->watcher_token);

	g_input = (Input*)gameinput;

	ram_assert_dependency(debug_update_input_guard.set(false););
}

void update_input()
{
	ram_assert_dependency(debug_update_input_guard.set(true);)

	Input_GameInput* gameinput = (Input_GameInput*)g_input;

	gameinput->process_watcher_messages();
	gameinput->update_device_states();
	gameinput->update_listener_pairings();
	gameinput->update_listener_states();

	ram_assert_dependency(debug_update_input_guard.set(false);)
}

void destroy_input(){
	Input_GameInput* gameinput = (Input_GameInput*)g_input;

	gameinput->igameinput->UnregisterCallback(gameinput->watcher_token, UINT64_MAX);
	for (int ikeyboard = 0; ikeyboard != gameinput->keyboards.size(); ++ikeyboard)
	{
		if (gameinput->keyboards[ikeyboard].device)
			gameinput->disconnect_keyboard(ikeyboard);
	}

	gameinput->igameinput->Release();

	for (int ilist = 0; ilist != gameinput->listeners.size(); ++ilist){
		destroy_listener_ptr((Listener_Win32*)gameinput->listeners[ilist]);
	}

	gameinput->listeners.destroy();
	destroy_mutex(&gameinput->listeners_mutex);

	free(g_input);
	g_input = NULL;
}
#endif

// REF: https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input#scan-codes [Scan Codes]
u32 RAMKey_to_scancode(RAM_Key key){
	ram_assert(key < sizeof(g_RAMKey_to_scancode));
	ram_assert(g_RAMKey_to_scancode[key] != 0xFF);
	return g_RAMKey_to_scancode[key];
}

void create_Listener_RawInput(Listener_RawInput& listener){
	listener.device = INVALID_HANDLE_VALUE;
}

void create_Keyboard_RawInput(Keyboard_RawInput& keyboard){
	keyboard.device = INVALID_HANDLE_VALUE;
	keyboard.frame_timestamp = 0;
	memset(keyboard.keys, 0x00, sizeof(Keyboard_RawInput::keys));
}

void get_device_name(HANDLE device, char* buffer, size_t buffer_size){
	UINT usize = (UINT)buffer_size;
	UINT out = GetRawInputDeviceInfoA(device, RIDI_DEVICENAME, buffer, &usize);
	if (out == (UINT)-1){
		snprintf(buffer, buffer_size, "No Device Name");
		ram_error_win32();
	}
}

int search_keyboard_by_device(Input_RawInput* input, HANDLE device){
	for (int ikeyb = 0; ikeyb != input->keyboards.size(); ++ikeyb){
		if (input->keyboards[ikeyb].device == device){
			return ikeyb;
		}
	}
	return -1;
}

void create_input(){
	memset(g_RAMKey_to_scancode, 0xFF, sizeof(g_RAMKey_to_scancode));

	g_RAMKey_to_scancode[RAMK_1] = 2;
	g_RAMKey_to_scancode[RAMK_2] = 3;
	g_RAMKey_to_scancode[RAMK_3] = 4;
	g_RAMKey_to_scancode[RAMK_4] = 5;
	g_RAMKey_to_scancode[RAMK_Q] = 16;
	g_RAMKey_to_scancode[RAMK_W] = 17;
	g_RAMKey_to_scancode[RAMK_E] = 18;
	g_RAMKey_to_scancode[RAMK_R] = 19;
	g_RAMKey_to_scancode[RAMK_A] = 30;
	g_RAMKey_to_scancode[RAMK_S] = 31;
	g_RAMKey_to_scancode[RAMK_D] = 32;
	g_RAMKey_to_scancode[RAMK_F] = 33;
	g_RAMKey_to_scancode[RAMK_Z] = 44;
	g_RAMKey_to_scancode[RAMK_X] = 45;
	g_RAMKey_to_scancode[RAMK_C] = 46;
	g_RAMKey_to_scancode[RAMK_V] = 47;

	RAWINPUTDEVICE RIDs[1];

	// cf. <hidusage.h>
	constexpr USHORT HID_USAGE_PAGE_GENERIC = 0x01;
	constexpr USHORT HID_USAGE_GENERIC_KEYBOARD = 0x06;

	RIDs[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
	RIDs[0].usUsage = HID_USAGE_GENERIC_KEYBOARD;
	RIDs[0].dwFlags = RIDEV_NOLEGACY | RIDEV_NOHOTKEYS | RIDEV_DEVNOTIFY;
	RIDs[0].hwndTarget = 0;

	if (!RegisterRawInputDevices(RIDs, 1, sizeof(RAWINPUTDEVICE))) crash("Failed to RegisterRawInputDevices");

	Input_RawInput* rawinput = (Input_RawInput*)malloc(sizeof(Input_RawInput));
	if (!rawinput) crash("Failed to allocate the input manager");

	rawinput->frame_counter = 0u;
	create_mutex(&rawinput->listeners_mutex);
	rawinput->listeners.create();

	rawinput->keyboards.create();

	g_input = (Input*)rawinput;
}

void update_connected_devices_RawInput(){
	ram_assert_dependency(debug_update_input_guard.get());
	Input_RawInput* rawinput = (Input_RawInput*)g_input;

	UINT errcode;

	UINT device_count;
	errcode = GetRawInputDeviceList(NULL, &device_count, sizeof(RAWINPUTDEVICELIST));

	if (errcode == -1){
		ram_error_win32();
		ram_error("Failed to GetRawInputDeviceList");
		return;
	}
	if (device_count == 0u) return;

	// TODO: use scratch memory
	RAWINPUTDEVICELIST* device_list = (RAWINPUTDEVICELIST*)malloc(device_count * sizeof(RAWINPUTDEVICELIST));
	errcode = GetRawInputDeviceList(device_list, &device_count, sizeof(RAWINPUTDEVICELIST));

	if (errcode == -1){
		ram_error_win32();
		ram_error("Failed to GetRawInputDeviceList");
		return;
	}

	auto search_device_in_list = [&](HANDLE device){
		for (int idevice = 0; idevice != device_count; ++idevice)
			if (device_list[idevice].hDevice == device) return idevice;
		return -1;
	};

	// register disconnections
	int ikeyb = 0;
	while(ikeyb < rawinput->keyboards.size()){
		int device_index = search_device_in_list(rawinput->keyboards[ikeyb].device);
		if (device_index == -1){
			ram_info("RawInput keyboard disconnection at index %d", ikeyb);
			rawinput->keyboards.remove(ikeyb);
		}
		else
			++ikeyb;
	}

	// register connections
	for (int idevice = 0; idevice != device_count; ++idevice){
		RAWINPUTDEVICELIST& device = device_list[idevice];

		if (device.dwType == RIM_TYPEKEYBOARD){
			int keyboard_index = search_keyboard_by_device(rawinput, device.hDevice);
			if (keyboard_index == -1){
				char device_name[1024];
				get_device_name(device.hDevice, device_name, sizeof(device_name));
				ram_info("RawInput keyboard connection for device: %s", device_name);

				Keyboard_RawInput keyboard;
				create_Keyboard_RawInput(keyboard);

				keyboard.device = device.hDevice;
				rawinput->keyboards.push(keyboard);
			}
		}
	}

	free(device_list);
}

void update_device_listener_pairings(int* keyboard_to_listener){
	ram_assert_dependency(debug_update_input_guard.get());
	Input* input = (Input*)g_input;
	Input_RawInput* rawinput = (Input_RawInput*)g_input;

	for (int ikeyb = 0; ikeyb != rawinput->keyboards.size(); ++ikeyb) keyboard_to_listener[ikeyb] = -1;

	// refresh listener to device pairings
	for (int ilist = 0; ilist != input->listeners.size(); ++ilist){
		Listener_RawInput* listener = (Listener_RawInput*)input->listeners[ilist];
		if (listener->device != INVALID_HANDLE_VALUE){
			int keyboard_index = search_keyboard_by_device(rawinput, listener->device);
			if (keyboard_index == -1)
				listener->device = INVALID_HANDLE_VALUE;
			else
				keyboard_to_listener[keyboard_index] = ilist;
		}
	}

	// Phase 1 : pair devices to listeners without pairings
	for (int ilist = 0; ilist != input->listeners.size(); ++ilist){
		Listener_RawInput* listener = (Listener_RawInput*)input->listeners[ilist];

		// paired listener ; see Phase 2
		if (listener->device != INVALID_HANDLE_VALUE) continue;

		// find a pairing
		if (listener->pairing_mode == Input::Pairing_Most_Recent || listener->pairing_mode == Input::Pairing_Most_Recent_Persistent){
			u64 timestamp = 0;
			int index = -1;

			for (int ikeyb = 0; ikeyb != rawinput->keyboards.size(); ++ikeyb){
				Keyboard_RawInput& keyboard = rawinput->keyboards[ikeyb];
				if (keyboard_to_listener[ikeyb] == -1 && keyboard.frame_timestamp > timestamp){
					timestamp = keyboard.frame_timestamp;
					index = ikeyb;
				}
			}

			if (index != -1){
				listener->device = rawinput->keyboards[index].device;
				keyboard_to_listener[index] = ilist;
			}
		}
	}

	// Phase 2 : pair devices to listeners that may want to change
	for (int ilist = 0; ilist != input->listeners.size(); ++ilist){
		Listener_RawInput* listener = (Listener_RawInput*)input->listeners[ilist];

		// non-paired listener ; no pairing needed or available during Phase 1
		if (listener->device == INVALID_HANDLE_VALUE) continue;

		// try to find a better pairing
		if (listener->pairing_mode == Input::Pairing_Most_Recent_Persistent){
			int current_index = search_keyboard_by_device(rawinput, listener->device);

			u64 timestamp = rawinput->keyboards[current_index].frame_timestamp;
			int new_index = -1;

			for (int ikeyb = 0; ikeyb != rawinput->keyboards.size(); ++ikeyb){
				Keyboard_RawInput& keyboard = rawinput->keyboards[ikeyb];
				if (keyboard_to_listener[ikeyb] == -1 && keyboard.frame_timestamp > timestamp){
					timestamp = keyboard.frame_timestamp;
					new_index = ikeyb;
				}
			}

			if (new_index != -1){
				keyboard_to_listener[current_index] = -1;
				keyboard_to_listener[new_index] = ilist;

				listener->device = rawinput->keyboards[new_index].device;
			}
		}
	}
}

void update_listener_states(int* keyboard_to_listener){
	ram_assert_dependency(debug_update_input_guard.get());
	Input_RawInput* rawinput = (Input_RawInput*)g_input;

	for (int ikeyb = 0; ikeyb != rawinput->keyboards.size(); ++ikeyb){
		Keyboard_RawInput& keyboard = rawinput->keyboards[ikeyb];
		int list_index = keyboard_to_listener[ikeyb];

		if (list_index != -1){
			Listener_RawInput* listener = (Listener_RawInput*)rawinput->listeners[list_index];

			for (int iaction = 0; iaction != listener->actions.size(); ++iaction){
				Input::Action& action = listener->actions[iaction];

				ram_assert(action.scancode <= carray_size(Keyboard_RawInput::keys));
				action.data.button = keyboard.keys[action.scancode];
			}
		}
	}
}

void update_input(){
	ram_assert_dependency(debug_update_input_guard.set(true);)

	Input_RawInput* rawinput = (Input_RawInput*)g_input;

	update_connected_devices_RawInput();

	// TODO: use scratch memory
	int* keyboard_to_listener = (int*)malloc(sizeof(int) * rawinput->keyboards.size());

	update_device_listener_pairings(keyboard_to_listener);
	update_listener_states(keyboard_to_listener);

	free(keyboard_to_listener);

	++rawinput->frame_counter;

	ram_assert_dependency(debug_update_input_guard.set(false);)
}

void destroy_input(){
	Input_RawInput* rawinput = (Input_RawInput*)g_input;
	rawinput->keyboards.destroy();

	free(g_input);
	g_input = NULL;
}

void create_engine(){
	Engine_Win32* win32 = (Engine_Win32*)malloc(sizeof(Engine_Win32));
	if (!win32) crash("Failed to allocate the engine");

	g_engine = (Engine*)win32;
}
void destroy_engine(){
	Engine_Win32* win32 = (Engine_Win32*)g_engine;
	free(win32);
	g_engine = NULL;
}

int CALLBACK WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR command_line, int show_code){
	create_argc_argv();
	g_instance = instance;
	g_main_thread_id = GetCurrentThreadId();

	create_crash_handler();

	create_logger();
	create_window_manager();
	create_audio();
	create_file_system();
	create_timer();
	create_input();
	create_default_random();
	create_engine();

	g_game_create();

	while (true)
	{
		// Win32 Message Pump [Window_Manager, RawInput]
		ram_assert_dependency(debug_update_window_manager_guard.set(true);)
		ram_assert_dependency(debug_update_input_guard.set(true);)

		Window_Manager_Win32* manager_win32 = (Window_Manager_Win32*)g_window_manager;
		Input_RawInput* input_rawinput = (Input_RawInput*)g_input;

		for (int ikeyb = 0; ikeyb != input_rawinput->keyboards.size(); ++ikeyb)
			for (int ikey = 0; ikey != carray_size(Keyboard_RawInput::keys); ++ikey)
				input_rawinput->keyboards[ikeyb].keys[ikey].transition_count = 0;

		for (int iwindow = 0; iwindow != manager_win32->windows.size(); ++iwindow){
			Window_Win32* window_win32 = (Window_Win32*)manager_win32->windows[iwindow];
			if (window_win32){
				MSG message;
				while (PeekMessage(&message, window_win32->handle, 0, 0, PM_REMOVE)){
					if (message.message == WM_INPUT){
						HRAWINPUT input_handle = (HRAWINPUT)message.lParam;

						UINT scratch_size;
						GetRawInputData(input_handle, RID_INPUT, NULL, &scratch_size, sizeof(RAWINPUTHEADER));
						if (scratch_size == 0u) continue;

						// TODO: use scratch memory
						RAWINPUT* input = (RAWINPUT*)malloc(scratch_size);
						GetRawInputData(input_handle, RID_INPUT, input, &scratch_size, sizeof(RAWINPUTHEADER));

						ram_assert(input->header.dwType == RIM_TYPEKEYBOARD);
						HANDLE device = input->header.hDevice;

						int keyboard_index = search_keyboard_by_device(input_rawinput, device);
						if (keyboard_index != -1){
							Keyboard_RawInput& keyboard = input_rawinput->keyboards[keyboard_index];

							USHORT scancode = input->data.keyboard.MakeCode;
							USHORT state = input->data.keyboard.Flags;

							ram_assert(scancode < carray_size(Keyboard_RawInput::keys));

							if (state == RI_KEY_MAKE || state == RI_KEY_BREAK){
								int down = (state == RI_KEY_MAKE) ? 1 : 0;

								keyboard.keys[scancode].transition_count += keyboard.keys[scancode].down != down;
								keyboard.keys[scancode].down = down;

								keyboard.frame_timestamp = input_rawinput->frame_counter;
							}
						}

						free(input);
					}
					else{
						TranslateMessage(&message);
						DispatchMessage(&message);
					}
				}
			}
		}

		ram_assert_dependency(debug_update_input_guard.set(false);)
		ram_assert_dependency(debug_update_window_manager_guard.set(false);)

		update_window_manager();
		update_input();
		update_audio();

		int quit_request = false;
		quit_request = g_game_update();
		if (quit_request) break;

		g_game_render();

		DwmFlush();
	}

	g_game_destroy();

	destroy_engine();
	destroy_input();
	destroy_timer();
	destroy_file_system();
	destroy_audio();
	destroy_window_manager();
	destroy_logger();

	destroy_crash_handler();

	return 0;
}