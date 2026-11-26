#include "platform/pipewire_lib.hpp"

#include "core/log.hpp"

#include <dlfcn.h>

#include <mutex>

namespace nitid::platform {

namespace {

std::once_flag once;
PipeWireLib lib{};
bool loaded = false;
std::string load_error;

template <class F> bool sym(void* h, const char* name, F& out)
{
    out = reinterpret_cast<F>(dlsym(h, name));
    if (!out) load_error = std::string("PipeWire library has no ") + name;
    return out != nullptr;
}

void load()
{
    void* h = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        load_error = "PipeWire is not installed (libpipewire-0.3.so.0 not found)";
        return;
    }
    bool ok = sym(h, "pw_init", lib.init) && sym(h, "pw_get_library_version", lib.get_library_version) &&
              sym(h, "pw_thread_loop_new", lib.thread_loop_new) &&
              sym(h, "pw_thread_loop_get_loop", lib.thread_loop_get_loop) &&
              sym(h, "pw_thread_loop_start", lib.thread_loop_start) && sym(h, "pw_thread_loop_stop", lib.thread_loop_stop) &&
              sym(h, "pw_thread_loop_destroy", lib.thread_loop_destroy) &&
              sym(h, "pw_thread_loop_lock", lib.thread_loop_lock) && sym(h, "pw_thread_loop_unlock", lib.thread_loop_unlock) &&
              sym(h, "pw_properties_new", lib.properties_new) && sym(h, "pw_stream_new_simple", lib.stream_new_simple) &&
              sym(h, "pw_stream_connect", lib.stream_connect) && sym(h, "pw_stream_destroy", lib.stream_destroy) &&
              sym(h, "pw_stream_dequeue_buffer", lib.stream_dequeue_buffer) &&
              sym(h, "pw_stream_queue_buffer", lib.stream_queue_buffer) &&
              sym(h, "pw_stream_get_time_n", lib.stream_get_time_n) && sym(h, "pw_stream_get_state", lib.stream_get_state) &&
              sym(h, "pw_context_new", lib.context_new) && sym(h, "pw_context_destroy", lib.context_destroy) &&
              sym(h, "pw_context_connect", lib.context_connect) && sym(h, "pw_context_connect_fd", lib.context_connect_fd) &&
              sym(h, "pw_core_disconnect", lib.core_disconnect) && sym(h, "pw_stream_new", lib.stream_new) &&
              sym(h, "pw_stream_add_listener", lib.stream_add_listener) &&
              sym(h, "pw_stream_update_params", lib.stream_update_params);
    if (!ok) return;  // keep the handle: harmless, and some symbols may already be referenced
    lib.init(nullptr, nullptr);
    lib.version = lib.get_library_version();
    loaded = true;
    log::debug("PipeWire {} loaded", lib.version);
}

} // namespace

const PipeWireLib* pipewire(std::string* error)
{
    std::call_once(once, load);
    if (!loaded && error) *error = load_error;
    return loaded ? &lib : nullptr;
}

} // namespace nitid::platform
