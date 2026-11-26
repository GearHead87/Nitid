#pragma once

// PipeWire client library, loaded at run time (dlopen "libpipewire-0.3.so.0"), like SDL does. Nitid needs it for
// sound (and later for screen capture), but a machine without it still plays video. Only the functions below are
// used; everything else from the PipeWire / SPA headers is inline (format builders, pods).
#include <pipewire/pipewire.h>

#include <string>

namespace nitid::platform {

struct PipeWireLib {
    decltype(&::pw_init) init;
    decltype(&::pw_get_library_version) get_library_version;
    decltype(&::pw_thread_loop_new) thread_loop_new;
    decltype(&::pw_thread_loop_get_loop) thread_loop_get_loop;
    decltype(&::pw_thread_loop_start) thread_loop_start;
    decltype(&::pw_thread_loop_stop) thread_loop_stop;
    decltype(&::pw_thread_loop_destroy) thread_loop_destroy;
    decltype(&::pw_thread_loop_lock) thread_loop_lock;
    decltype(&::pw_thread_loop_unlock) thread_loop_unlock;
    decltype(&::pw_properties_new) properties_new;
    decltype(&::pw_stream_new_simple) stream_new_simple;
    decltype(&::pw_stream_connect) stream_connect;
    decltype(&::pw_stream_destroy) stream_destroy;
    decltype(&::pw_stream_dequeue_buffer) stream_dequeue_buffer;
    decltype(&::pw_stream_queue_buffer) stream_queue_buffer;
    decltype(&::pw_stream_get_time_n) stream_get_time_n;
    decltype(&::pw_stream_get_state) stream_get_state;
    // Video capture
    decltype(&::pw_context_new) context_new;
    decltype(&::pw_context_destroy) context_destroy;
    decltype(&::pw_context_connect) context_connect;
    decltype(&::pw_context_connect_fd) context_connect_fd;
    decltype(&::pw_core_disconnect) core_disconnect;
    decltype(&::pw_stream_new) stream_new;
    decltype(&::pw_stream_add_listener) stream_add_listener;
    decltype(&::pw_stream_update_params) stream_update_params;
    std::string version;
};

// Loads the library once per process (pw_init included). nullptr if it is not installed; *error says why.
const PipeWireLib* pipewire(std::string* error = nullptr);

} // namespace nitid::platform
