#include "input/screencast_portal.hpp"

#include "core/log.hpp"
#include "platform/dbus_lib.hpp"

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <variant>
#include <vector>

namespace nitid::input {

namespace fs = std::filesystem;

namespace {

constexpr const char* kService = "org.freedesktop.portal.Desktop";
constexpr const char* kObject = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenCast = "org.freedesktop.portal.ScreenCast";

using Value = std::variant<std::string, uint32_t, bool>;
struct Entry {
    const char* key;
    Value value;
};

// Appends an a{sv} dictionary.
void append_dict(const platform::DBusLib& d, DBusMessageIter* it, const std::vector<Entry>& entries)
{
    DBusMessageIter dict;
    d.message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &dict);
    for (const auto& e : entries) {
        DBusMessageIter entry, var;
        d.message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
        d.message_iter_append_basic(&entry, DBUS_TYPE_STRING, &e.key);
        if (auto* s = std::get_if<std::string>(&e.value)) {
            const char* v = s->c_str();
            d.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
            d.message_iter_append_basic(&var, DBUS_TYPE_STRING, &v);
        } else if (auto* u = std::get_if<uint32_t>(&e.value)) {
            d.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
            d.message_iter_append_basic(&var, DBUS_TYPE_UINT32, u);
        } else {
            dbus_bool_t b = std::get<bool>(e.value);
            d.message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
            d.message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &b);
        }
        d.message_iter_close_container(&entry, &var);
        d.message_iter_close_container(&dict, &entry);
    }
    d.message_iter_close_container(it, &dict);
}

// What we read from a Response's results dictionary.
struct Results {
    std::string session_handle, restore_token;
    uint32_t node = 0;
    bool has_node = false;
};

void parse_results(const platform::DBusLib& d, DBusMessageIter* dict, Results& r)
{
    if (d.message_iter_get_arg_type(dict) != DBUS_TYPE_ARRAY) return;
    DBusMessageIter entries;
    d.message_iter_recurse(dict, &entries);
    while (d.message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, var;
        d.message_iter_recurse(&entries, &entry);
        const char* key = nullptr;
        d.message_iter_get_basic(&entry, &key);
        d.message_iter_next(&entry);
        d.message_iter_recurse(&entry, &var);
        const std::string k = key ? key : "";
        const int t = d.message_iter_get_arg_type(&var);
        if ((k == "session_handle" || k == "restore_token") && (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH)) {
            const char* v = nullptr;
            d.message_iter_get_basic(&var, &v);
            (k == "session_handle" ? r.session_handle : r.restore_token) = v ? v : "";
        } else if (k == "streams" && t == DBUS_TYPE_ARRAY) {
            // a(ua{sv}): take the first stream's node id
            DBusMessageIter streams, st;
            d.message_iter_recurse(&var, &streams);
            if (d.message_iter_get_arg_type(&streams) == DBUS_TYPE_STRUCT) {
                d.message_iter_recurse(&streams, &st);
                if (d.message_iter_get_arg_type(&st) == DBUS_TYPE_UINT32) {
                    d.message_iter_get_basic(&st, &r.node);
                    r.has_node = true;
                }
            }
        }
        d.message_iter_next(&entries);
    }
}

} // namespace

ScreenCastPortal::ScreenCastPortal() = default;

ScreenCastPortal::~ScreenCastPortal()
{
    const auto* d = platform::dbus();
    if (!d || !conn_) return;
    if (!session_.empty()) {
        // Ends the capture on the desktop's side (the "screen is shared" indicator goes away).
        if (DBusMessage* m = d->message_new_method_call(kService, session_.c_str(), "org.freedesktop.portal.Session", "Close")) {
            DBusError err;
            d->error_init(&err);
            if (DBusMessage* r = d->connection_send_with_reply_and_block(conn_, m, 2000, &err)) d->message_unref(r);
            d->error_free(&err);
            d->message_unref(m);
        }
    }
    d->connection_close(conn_);
    d->connection_unref(conn_);
}

std::string ScreenCastPortal::token_path()
{
    fs::path base;
    if (const char* x = std::getenv("XDG_STATE_HOME"); x && *x) base = x;
    else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".local/state";
    else return {};
    return (base / "nitid" / "screencast-token").string();
}

std::string ScreenCastPortal::load_token()
{
    std::string t;
    if (auto p = token_path(); !p.empty()) std::ifstream(p) >> t;
    return t;
}

void ScreenCastPortal::save_token(const std::string& token)
{
    auto p = token_path();
    if (p.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(p).parent_path(), ec);
    std::ofstream(p) << token;
}

bool ScreenCastPortal::start(const Options& opt, std::string& restore_token, int& fd, uint32_t& node, std::string& error,
                             const std::atomic<bool>& stop, const std::function<void(std::string)>& status)
{
    const auto* d = platform::dbus(&error);
    if (!d) return false;
    DBusError err;
    d->error_init(&err);
    conn_ = d->bus_get_private(DBUS_BUS_SESSION, &err);
    if (!conn_) {
        error = std::string("can't reach the desktop's session bus: ") + (err.message ? err.message : "?");
        d->error_free(&err);
        return false;
    }
    d->connection_set_exit_on_disconnect(conn_, false);
    // Request objects live at /org/freedesktop/portal/desktop/request/<sender>/<token>, sender = unique name
    // without ':' and with '.' -> '_'.
    std::string sender = d->bus_get_unique_name(conn_);
    if (!sender.empty() && sender[0] == ':') sender.erase(0, 1);
    for (auto& c : sender)
        if (c == '.') c = '_';

    // Calls a portal method that answers with a Request; waits for its Response signal.
    auto call = [&](const char* method, const std::function<void(DBusMessageIter*, const std::string& token)>& args,
                    Results& out, int timeout_s) -> bool {
        const std::string token = "nitid" + std::to_string(getpid()) + "_" + std::to_string(++counter_);
        const std::string path = std::string(kObject) + "/request/" + sender + "/" + token;
        const std::string rule = "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='" + path + "'";
        d->bus_add_match(conn_, rule.c_str(), &err);
        if (d->error_is_set(&err)) {
            error = std::string("D-Bus match failed: ") + err.message;
            d->error_free(&err);
            return false;
        }
        DBusMessage* m = d->message_new_method_call(kService, kObject, kScreenCast, method);
        DBusMessageIter it;
        d->message_iter_init_append(m, &it);
        args(&it, token);
        DBusMessage* reply = d->connection_send_with_reply_and_block(conn_, m, 10000, &err);
        d->message_unref(m);
        if (!reply) {
            error = std::string("screen capture portal: ") + method + ": " + (err.message ? err.message : "no reply");
            d->error_free(&err);
            return false;
        }
        d->message_unref(reply);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
        while (!stop && std::chrono::steady_clock::now() < deadline) {
            d->connection_read_write(conn_, 100);
            while (DBusMessage* sig = d->connection_pop_message(conn_)) {
                const char* p = d->message_get_path(sig);
                if (d->message_is_signal(sig, "org.freedesktop.portal.Request", "Response") && p && path == p) {
                    DBusMessageIter ri;
                    uint32_t response = 2;
                    if (d->message_iter_init(sig, &ri) && d->message_iter_get_arg_type(&ri) == DBUS_TYPE_UINT32) {
                        d->message_iter_get_basic(&ri, &response);
                        d->message_iter_next(&ri);
                        parse_results(*d, &ri, out);
                    }
                    d->message_unref(sig);
                    if (response == 1) error = "screen capture was cancelled";
                    else if (response != 0) error = std::string("screen capture portal: ") + method + " failed";
                    return response == 0;
                }
                d->message_unref(sig);
            }
        }
        error = stop ? "cancelled" : std::string("screen capture portal: no answer to ") + method;
        return false;
    };

    Results r;
    if (!call("CreateSession",
              [&](DBusMessageIter* it, const std::string& token) {
                  append_dict(*d, it, {{"handle_token", token}, {"session_handle_token", "nitid_session" + std::to_string(getpid())}});
              },
              r, 10))
        return false;
    session_ = r.session_handle;
    const char* sp = session_.c_str();

    uint32_t types = (opt.monitors ? 1u : 0u) | (opt.windows ? 2u : 0u);
    if (!call("SelectSources",
              [&](DBusMessageIter* it, const std::string& token) {
                  d->message_iter_append_basic(it, DBUS_TYPE_OBJECT_PATH, &sp);
                  std::vector<Entry> e{{"handle_token", token},
                                       {"types", types},
                                       {"multiple", false},
                                       {"cursor_mode", uint32_t(opt.cursor ? 2 : 1)}};  // 2 = drawn into the frames
                  if (opt.remember) e.push_back({"persist_mode", uint32_t(2)});        // until revoked
                  if (opt.remember && !restore_token.empty()) e.push_back({"restore_token", restore_token});
                  append_dict(*d, it, e);
              },
              r, 10))
        return false;

    status(restore_token.empty() ? "choose a screen or window in the desktop's dialog"
                                 : "starting (the desktop remembered your choice)");
    Results started;
    if (!call("Start",
              [&](DBusMessageIter* it, const std::string& token) {
                  d->message_iter_append_basic(it, DBUS_TYPE_OBJECT_PATH, &sp);
                  const char* parent = "";
                  d->message_iter_append_basic(it, DBUS_TYPE_STRING, &parent);
                  append_dict(*d, it, {{"handle_token", token}});
              },
              started, 600))  // the user may take a while to choose
        return false;
    if (!started.has_node) {
        error = "screen capture portal returned no stream";
        return false;
    }
    node = started.node;
    if (!started.restore_token.empty()) restore_token = started.restore_token;

    DBusMessage* m = d->message_new_method_call(kService, kObject, kScreenCast, "OpenPipeWireRemote");
    DBusMessageIter it;
    d->message_iter_init_append(m, &it);
    d->message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &sp);
    append_dict(*d, &it, {});
    DBusMessage* reply = d->connection_send_with_reply_and_block(conn_, m, 10000, &err);
    d->message_unref(m);
    if (!reply) {
        error = std::string("screen capture portal: OpenPipeWireRemote: ") + (err.message ? err.message : "no reply");
        d->error_free(&err);
        return false;
    }
    DBusMessageIter ri;
    fd = -1;
    if (d->message_iter_init(reply, &ri) && d->message_iter_get_arg_type(&ri) == DBUS_TYPE_UNIX_FD)
        d->message_iter_get_basic(&ri, &fd);  // a new fd, ours to close
    d->message_unref(reply);
    if (fd < 0) {
        error = "screen capture portal gave no PipeWire connection";
        return false;
    }
    log::info("screen capture: portal session {}, PipeWire node {}", session_, node);
    return true;
}

} // namespace nitid::input
