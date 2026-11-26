#pragma once

// Screen capture permission through the desktop portal (org.freedesktop.portal.ScreenCast over D-Bus; spike S6 /
// M4): CreateSession -> SelectSources -> Start (the desktop shows its dialog: which screen or window) ->
// OpenPipeWireRemote. Gives a PipeWire connection (fd) and the stream's node id.
//
// With persist_mode the portal returns a restore token; passing it next time skips the dialog (the desktop
// remembers the choice). Nitid keeps it in $XDG_STATE_HOME/nitid/screencast-token.
//
// The session lives as long as this object (its D-Bus connection): keep it alive while capturing.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

struct DBusConnection;

namespace nitid::input {

class ScreenCastPortal {
public:
    struct Options {
        bool monitors = true;   // offer whole screens
        bool windows = true;    // offer single windows
        bool cursor = true;     // draw the mouse pointer into the frames
        bool remember = true;   // ask the desktop to remember the choice (restore token)
    };
    ScreenCastPortal();
    ~ScreenCastPortal();
    ScreenCastPortal(const ScreenCastPortal&) = delete;
    ScreenCastPortal& operator=(const ScreenCastPortal&) = delete;

    // Blocks until the user has chosen (or `stop` is set). `restore_token`: in = last token (may be empty),
    // out = the new one. On success returns true with a PipeWire fd (owned by the caller) and node id.
    bool start(const Options& opt, std::string& restore_token, int& fd, uint32_t& node, std::string& error,
               const std::atomic<bool>& stop, const std::function<void(std::string)>& status);

    // Where Nitid keeps the restore token.
    static std::string token_path();
    static std::string load_token();
    static void save_token(const std::string& token);

private:
    DBusConnection* conn_ = nullptr;
    std::string session_;
    int counter_ = 0;
};

} // namespace nitid::input
