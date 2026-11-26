#pragma once

// libdbus, loaded at run time (dlopen "libdbus-1.so.3"), for the desktop portal (screen capture). Like PipeWire it
// is on every desktop Linux, but Nitid doesn't link it: without it only screen capture is unavailable.
#include <dbus/dbus.h>

#include <string>

namespace nitid::platform {

struct DBusLib {
    decltype(&::dbus_error_init) error_init;
    decltype(&::dbus_error_free) error_free;
    decltype(&::dbus_error_is_set) error_is_set;
    decltype(&::dbus_bus_get_private) bus_get_private;
    decltype(&::dbus_bus_get_unique_name) bus_get_unique_name;
    decltype(&::dbus_bus_add_match) bus_add_match;
    decltype(&::dbus_connection_close) connection_close;
    decltype(&::dbus_connection_unref) connection_unref;
    decltype(&::dbus_connection_set_exit_on_disconnect) connection_set_exit_on_disconnect;
    decltype(&::dbus_connection_send_with_reply_and_block) connection_send_with_reply_and_block;
    decltype(&::dbus_connection_read_write) connection_read_write;
    decltype(&::dbus_connection_pop_message) connection_pop_message;
    decltype(&::dbus_message_new_method_call) message_new_method_call;
    decltype(&::dbus_message_unref) message_unref;
    decltype(&::dbus_message_is_signal) message_is_signal;
    decltype(&::dbus_message_get_path) message_get_path;
    decltype(&::dbus_message_iter_init) message_iter_init;
    decltype(&::dbus_message_iter_init_append) message_iter_init_append;
    decltype(&::dbus_message_iter_open_container) message_iter_open_container;
    decltype(&::dbus_message_iter_close_container) message_iter_close_container;
    decltype(&::dbus_message_iter_append_basic) message_iter_append_basic;
    decltype(&::dbus_message_iter_get_arg_type) message_iter_get_arg_type;
    decltype(&::dbus_message_iter_get_basic) message_iter_get_basic;
    decltype(&::dbus_message_iter_recurse) message_iter_recurse;
    decltype(&::dbus_message_iter_next) message_iter_next;
};

// Loads the library once per process. nullptr if it is not installed; *error says why.
const DBusLib* dbus(std::string* error = nullptr);

} // namespace nitid::platform
