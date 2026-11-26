#include "platform/dbus_lib.hpp"

#include <dlfcn.h>

#include <mutex>

namespace nitid::platform {

namespace {

std::once_flag once;
DBusLib lib{};
bool loaded = false;
std::string load_error;

template <class F> bool sym(void* h, const char* name, F& out)
{
    out = reinterpret_cast<F>(dlsym(h, name));
    if (!out) load_error = std::string("libdbus has no ") + name;
    return out != nullptr;
}

void load()
{
    void* h = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        load_error = "D-Bus is not installed (libdbus-1.so.3 not found)";
        return;
    }
    loaded = sym(h, "dbus_error_init", lib.error_init) && sym(h, "dbus_error_free", lib.error_free) &&
             sym(h, "dbus_error_is_set", lib.error_is_set) && sym(h, "dbus_bus_get_private", lib.bus_get_private) &&
             sym(h, "dbus_bus_get_unique_name", lib.bus_get_unique_name) && sym(h, "dbus_bus_add_match", lib.bus_add_match) &&
             sym(h, "dbus_connection_close", lib.connection_close) && sym(h, "dbus_connection_unref", lib.connection_unref) &&
             sym(h, "dbus_connection_set_exit_on_disconnect", lib.connection_set_exit_on_disconnect) &&
             sym(h, "dbus_connection_send_with_reply_and_block", lib.connection_send_with_reply_and_block) &&
             sym(h, "dbus_connection_read_write", lib.connection_read_write) &&
             sym(h, "dbus_connection_pop_message", lib.connection_pop_message) &&
             sym(h, "dbus_message_new_method_call", lib.message_new_method_call) &&
             sym(h, "dbus_message_unref", lib.message_unref) && sym(h, "dbus_message_is_signal", lib.message_is_signal) &&
             sym(h, "dbus_message_get_path", lib.message_get_path) && sym(h, "dbus_message_iter_init", lib.message_iter_init) &&
             sym(h, "dbus_message_iter_init_append", lib.message_iter_init_append) &&
             sym(h, "dbus_message_iter_open_container", lib.message_iter_open_container) &&
             sym(h, "dbus_message_iter_close_container", lib.message_iter_close_container) &&
             sym(h, "dbus_message_iter_append_basic", lib.message_iter_append_basic) &&
             sym(h, "dbus_message_iter_get_arg_type", lib.message_iter_get_arg_type) &&
             sym(h, "dbus_message_iter_get_basic", lib.message_iter_get_basic) &&
             sym(h, "dbus_message_iter_recurse", lib.message_iter_recurse) &&
             sym(h, "dbus_message_iter_next", lib.message_iter_next);
}

} // namespace

const DBusLib* dbus(std::string* error)
{
    std::call_once(once, load);
    if (!loaded && error) *error = load_error;
    return loaded ? &lib : nullptr;
}

} // namespace nitid::platform
