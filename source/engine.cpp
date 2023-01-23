#include "engine.h"

int g_argc;
char** g_argv;

Logger* g_logger;
Window_Manager* g_window_manager;
Audio* g_audio;
File_System* g_file_system;
Timer* g_timer;
Input* g_input;
Engine* g_engine;

ram_assert_dependency(Atomic<int> debug_update_window_manager_guard;)
ram_assert_dependency(Atomic<int> debug_update_input_guard;)

void* Audio_DSP::get_param(){
	ram_assert( user_param > 0 && user_param < 4 );
	return (char*)memory + param_offset * (user_param - 1);
}

void Audio_DSP::commit_param(){
	// TODO: use alloca / scratch memory here
	char scratch[1024];
	ram_assert(param_size <= sizeof(scratch));

	ram_assert( user_param > 0 && user_param < 4 );

	void* user_param_data = (char*)memory + param_offset * (user_param - 1);
	memcpy(scratch, user_param_data, param_size);

	int new_user_param = transient_param.exchange( - user_param );
	new_user_param = abs( new_user_param );
	user_param = new_user_param;

	ram_assert( new_user_param > 0 && new_user_param < 4 );

	void* new_user_param_data = (char*)memory + param_offset * (new_user_param - 1);
	memcpy(new_user_param_data, scratch, param_size);
}

void* Audio_DSP::audio_thread_update_param(){
	ram_assert(thread_id() == g_audio->audio_thread_id());

	int new_param = transient_param.get();
	ram_assert( ( new_param > 0 && new_param < 4 ) || ( new_param > -4 && new_param < 0 ) );

	if( new_param < 0 )
	{
		new_param = transient_param.exchange( driver_param );
		new_param = abs( new_param );
		driver_param = new_param;
	}

	return (char*)memory + param_offset * ( driver_param - 1 );
}

void* Audio_DSP::audio_thread_access_internal_data(){
	ram_assert(thread_id() == g_audio->audio_thread_id());
	return (char*)memory + param_offset * 3;
}

Audio_DSP* Audio::create_DSP(size_t param_size, void* internal_data, size_t internal_data_size){
	Audio_DSP* DSP = NULL;
	for (int iDSP = 0; iDSP != max_DSP_count; ++iDSP){
		if (DSP_states[iDSP].compare_exchange(Available, Reserved) == Available){
			DSP = &DSPs[iDSP];
			break;
		}
	}

	if (!DSP) crash("Failure to create a new DSP");

	size_t aligned_to_cacheline =
		(param_size / ram_cacheline_size) * ram_cacheline_size
		+ ((param_size % ram_cacheline_size) ? ram_cacheline_size : 0);

	void* memory = malloc(aligned_to_cacheline * 3 + internal_data_size);

	DSP->process = NULL;
	DSP->destroy = NULL;

	DSP->user_param = 1;
	DSP->transient_param.set(2);
	DSP->driver_param = 3;

	DSP->param_size = param_size;
	DSP->param_offset = aligned_to_cacheline;
	DSP->memory = memory;

	void* DSP_internal_data = (char*)memory + aligned_to_cacheline * 3;
	memcpy(DSP_internal_data, internal_data, internal_data_size);

	return DSP;
}

void Audio::activate_DSP(Audio_DSP* DSP){
	size_t iDSP = DSP - DSPs;
	ram_assert(iDSP < max_DSP_count);
	ram_assert(DSP_states[iDSP].get() == Reserved);
	DSP_states[iDSP].set(Active);
}

int Audio::deactivate_DSP(Audio_DSP* DSP){
	size_t iDSP = DSP - DSPs;
	ram_assert(iDSP < max_DSP_count);
	ram_assert(DSP_states[iDSP].get() == Active);
	DSP_states[iDSP].set(Inactive);
	return audio_thread_counter.get();
}

void Audio::destroy_destroyable_DSPs(){
	for (int iDSP = 0; iDSP != max_DSP_count; ++iDSP){
		if (DSP_states[iDSP].get() == Destroyable){
			Audio_DSP& DSP = DSPs[iDSP];

			void* data_ptr = (char*)DSP.memory + DSP.param_offset * 3;
			if( DSP.destroy ) DSP.destroy( data_ptr );
			free(DSP.memory);

			DSP_states[iDSP].set(Available);
		}
	}
}

static void create_Control_Data( Input::Control_Type type, Input::Control_Data& data )
{
	if( type == Input::Control_Button )
	{
		data.button.down = false;
		data.button.transition_count = 0;
	}
	else
		ram_error( "Unknown Control_Type in create_Control_Data" );
}

void Input::Listener::register_action(const char* name, Control_Type type, u32 scancode)
{
	ram_assert(
		[&]()
	{
		for( int iaction = 0; iaction != actions.size(); ++iaction )
		{
			if( actions[iaction].name == name ) return false;
		}
	return true;
	}( )
	);

	Action new_action;
	new_action.name = name;
	new_action.type = type;
	new_action.scancode = scancode;

	create_Control_Data(type, new_action.data);

	actions.push( new_action );
}

void Input::Listener::unregister_action( const char* name )
{
	for( int iaction = 0; iaction != actions.size(); ++iaction )
	{
		if( actions[iaction].name == name )
		{
			actions.remove_swap( iaction );
			return;
		}
	}

	ram_error("Unregistering an action [%s] that is unknown to the listener", name);
}

Input::Control_Data Input::Listener::get_action_status( const char* name )
{
	for( int iaction = 0; iaction != actions.size(); ++iaction )
	{
		if (strcmp(actions[iaction].name, name) == 0) return actions[iaction].data;
	}

	ram_error( "Retrieving the status of an unknown action [%s]", name );

	Input::Control_Data empty;
	memset(&empty, 0x00, sizeof(Input::Control_Data));
	return empty;
}
