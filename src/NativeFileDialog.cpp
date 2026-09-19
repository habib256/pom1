// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// NativeFileDialog.cpp — desktop OS file picker integration.
//
// Per-platform backends:
//   * Windows : Common Item Dialog via legacy comdlg32 GetOpenFileNameW /
//               GetSaveFileNameW. Wide-string round-trip through MultiByte
//               <-> WideChar so callers stay UTF-8.
//   * macOS   : implemented in NativeFileDialog_Mac.mm (Objective-C++).
//   * Linux   : the xdg-desktop-portal FileChooser over D-Bus (libdbus-1,
//               dlopen'd), else forks `zenity --file-selection ...` (GNOME /
//               generic), else `kdialog --getopenfilename ...` (KDE). Probed
//               once; a backend that cannot run hands the request to the next.
//   * WASM    : everything stubs out (isAvailable() = false), the existing
//               ImGui dialog stays as the only option.

#include "NativeFileDialog.h"
#include "POM1Build.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace pom1 {
namespace {
// Host event-loop pump — see NativeFileDialog::setWaitPump. Lives up here
// rather than in the shared tail section because the Linux backend below is
// its only consumer and is compiled before it. Touched from the render thread
// only (install at startup, call while a picker child runs).
std::function<void()>& waitPumpFn()
{
    static std::function<void()> fn;
    return fn;
}
} // namespace

void NativeFileDialog::setWaitPump(std::function<void()> pump)
{
    waitPumpFn() = std::move(pump);
}
} // namespace pom1

#if POM1_IS_WASM
// ── WASM: no native picker available, every call returns false. ─────────────
namespace pom1 {
bool NativeFileDialog::platformAvailable() { return false; }
bool NativeFileDialog::openFile(GLFWwindow*, const std::string&,
                                const std::string&,
                                const std::vector<FileFilter>&, std::string&)
{ return false; }
bool NativeFileDialog::saveFile(GLFWwindow*, const std::string&,
                                const std::string&, const std::string&,
                                const std::vector<FileFilter>&, std::string&)
{ return false; }
} // namespace pom1

#elif defined(_WIN32)
// ── Windows: GetOpenFileNameW / GetSaveFileNameW. ───────────────────────────
//
// Uses the legacy comdlg32 API rather than the IFileDialog COM interface
// because GetOpenFileNameW is already initialised by every Win32 process and
// avoids the CoInitializeEx dance — POM1's main thread runs the GL context
// and we don't want to risk shifting its COM apartment under glfw.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace pom1 {

namespace {

std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                   nullptr, 0);
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), wlen);
    return w;
}

std::string wideToUtf8(const wchar_t* w)
{
    if (!w || !*w) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 1) return std::string();
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// commdlg expects a double-NUL terminated buffer of "Label\0*.ext;*.ext2\0..."
// pairs. Build it from FileFilter list and append an "All files" trailer.
std::wstring buildFilterBuffer(const std::vector<FileFilter>& filters)
{
    std::wstring buf;
    auto append = [&](const std::wstring& label, const std::wstring& pattern) {
        buf.append(label);
        buf.push_back(L'\0');
        buf.append(pattern);
        buf.push_back(L'\0');
    };
    for (const auto& f : filters) {
        std::wstring pat;
        if (f.extensions.empty()) {
            pat = L"*.*";
        } else {
            for (size_t i = 0; i < f.extensions.size(); ++i) {
                if (i) pat.push_back(L';');
                pat.append(L"*.").append(utf8ToWide(f.extensions[i]));
            }
        }
        std::wstring label = utf8ToWide(f.description);
        if (label.empty()) label = pat;
        append(label, pat);
    }
    append(L"All files (*.*)", L"*.*");
    buf.push_back(L'\0'); // final double-NUL terminator
    return buf;
}

HWND parentHwnd(GLFWwindow* w)
{
    return w ? glfwGetWin32Window(w) : nullptr;
}

// Win32 paths can exceed MAX_PATH (260) — long OneDrive / AppData targets
// commonly do. OFN_EXPLORER + a large lpstrFile buffer up to 32 768 wchars
// is the documented way to make GetOpenFileNameW / GetSaveFileNameW return
// the full path. Using a stock MAX_PATH buffer makes GetSaveFileNameW
// return FALSE with CDERR_*/FNERR_BUFFERTOOSMALL, which we previously
// treated as a silent cancel — the user saw their Save click do nothing.
constexpr size_t kWin32PathBufWChars = 32768;

} // namespace

bool NativeFileDialog::platformAvailable() { return true; }

bool NativeFileDialog::openFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    std::vector<wchar_t> buf(kWin32PathBufWChars, L'\0');
    std::wstring wtitle  = utf8ToWide(title);
    std::wstring wdir    = utf8ToWide(defaultDir);
    std::wstring wfilter = buildFilterBuffer(filters);

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parentHwnd(parent);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = (DWORD)buf.size();
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER
              | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) return false;
    outPath = wideToUtf8(buf.data());
    return !outPath.empty();
}

bool NativeFileDialog::saveFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::string& defaultName,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    std::vector<wchar_t> buf(kWin32PathBufWChars, L'\0');
    std::wstring wname = utf8ToWide(defaultName);
    if (!wname.empty()) {
        size_t n = std::min<size_t>(wname.size(), buf.size() - 1);
        std::memcpy(buf.data(), wname.data(), n * sizeof(wchar_t));
        buf[n] = L'\0';
    }
    std::wstring wtitle  = utf8ToWide(title);
    std::wstring wdir    = utf8ToWide(defaultDir);
    std::wstring wfilter = buildFilterBuffer(filters);

    // Default extension drives Explorer's auto-append when the user types a
    // bare name. Take it from the first filter's first extension if available.
    std::wstring defExt;
    if (!filters.empty() && !filters.front().extensions.empty())
        defExt = utf8ToWide(filters.front().extensions.front());

    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = parentHwnd(parent);
    ofn.lpstrFilter = wfilter.c_str();
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = (DWORD)buf.size();
    ofn.lpstrTitle  = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrInitialDir = wdir.empty() ? nullptr : wdir.c_str();
    ofn.lpstrDefExt = defExt.empty() ? nullptr : defExt.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER
              | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn)) return false;
    outPath = wideToUtf8(buf.data());
    return !outPath.empty();
}

} // namespace pom1

#elif defined(__APPLE__)
// ── macOS: implementation lives in NativeFileDialog_Mac.mm so we can call
// AppKit. Nothing to do in this file.

#else
// ── Linux (and other Unixes): desktop portal, then zenity / kdialog. ────────
//
// We never link against GTK/Qt — that would drag a heavy compile-time
// dependency for what amounts to "show the desktop's picker". Three backends,
// tried in this order and probed ONCE per session:
//
//   1. the xdg-desktop-portal FileChooser, over D-Bus (PortalFileChooser.h
//      says why). libdbus-1 is dlopen'd, so POM1 takes no build dependency on
//      it and no runtime one either: no library, no session bus or no portal
//      simply means "no portal";
//   2. zenity (GTK), forked;
//   3. kdialog (KDE), forked.
//
// A backend that proves it cannot RUN is struck off for the session and the
// next one serves the SAME request, so a broken portal still ends in a dialog.
// A Cancel is not a failure and strikes nothing.
//
// POM1_FILE_DIALOG=portal|zenity|kdialog|builtin restricts the choice to one
// backend (builtin = none: POM1's own browser). The tests use it to put a fake
// zenity in front of a desktop that has a real portal; a user uses it when the
// desktop's portal misbehaves.

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <unistd.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string>

#include "PortalFileChooser.h"

namespace pom1 {

namespace {

// Outcome of one attempt. `Failed` means the backend could not run at all —
// not the user's Cancel, which is also "no path" but must not strike it off.
enum class Pick { Chosen, Cancelled, Failed };

enum class Backend { None = 0, Portal = 1, Zenity = 2, Kdialog = 3 };

std::string lowerAscii(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool onPath(const char* binary)
{
    // Walk $PATH ourselves rather than fork+exec a `which` probe. Avoids
    // depending on coreutils being on minimal containers.
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string dir;
    auto check = [&](const std::string& d) {
        std::string full = d.empty() ? std::string(binary)
                                     : d + "/" + binary;
        return access(full.c_str(), X_OK) == 0;
    };
    while (*path) {
        if (*path == ':') {
            if (check(dir)) return true;
            dir.clear();
        } else {
            dir.push_back(*path);
        }
        ++path;
    }
    return check(dir);
}

// ── libdbus-1, dlopen'd ────────────────────────────────────────────────────
//
// The two structs POM1 must allocate itself are declared here rather than
// taken from <dbus/dbus.h>, which is what keeps libdbus-dev out of the build.
// Both are the frozen libdbus-1 ABI: DBusError is {name, message, five 1-bit
// flags, padding} = 32 bytes on LP64 with the same offsets (checked against
// the 1.14 headers); DBusMessageIter is 72 bytes there and only ever passed by
// pointer, so it is over-allocated, which is always safe.
namespace dbus {

struct Error {
    const char* name;
    const char* message;
    unsigned int flags;
    void* padding;
};
struct Iter { alignas(void*) unsigned char opaque[128]; };
struct Conn;
struct Msg;
using Bool = uint32_t;

constexpr int kString = 's', kBool = 'b', kUint32 = 'u', kByte = 'y', kArray = 'a',
              kVariant = 'v', kStruct = 'r', kDictEntry = 'e', kObjectPath = 'o';

struct Api {
    bool ok = false;
    void  (*error_init)(Error*) = nullptr;
    void  (*error_free)(Error*) = nullptr;
    Bool  (*error_is_set)(const Error*) = nullptr;
    Conn* (*connection_open_private)(const char*, Error*) = nullptr;
    Bool  (*bus_register)(Conn*, Error*) = nullptr;
    void  (*connection_set_exit_on_disconnect)(Conn*, Bool) = nullptr;
    void  (*connection_close)(Conn*) = nullptr;
    void  (*connection_unref)(Conn*) = nullptr;
    Bool  (*connection_read_write)(Conn*, int) = nullptr;
    Msg*  (*connection_pop_message)(Conn*) = nullptr;
    Msg*  (*connection_send_with_reply_and_block)(Conn*, Msg*, int, Error*) = nullptr;
    const char* (*bus_get_unique_name)(Conn*) = nullptr;
    void  (*bus_add_match)(Conn*, const char*, Error*) = nullptr;
    Msg*  (*message_new_method_call)(const char*, const char*, const char*, const char*) = nullptr;
    void  (*message_unref)(Msg*) = nullptr;
    Bool  (*message_is_signal)(Msg*, const char*, const char*) = nullptr;
    const char* (*message_get_path)(Msg*) = nullptr;
    void  (*message_iter_init_append)(Msg*, Iter*) = nullptr;
    Bool  (*message_iter_append_basic)(Iter*, int, const void*) = nullptr;
    Bool  (*message_iter_open_container)(Iter*, int, const char*, Iter*) = nullptr;
    Bool  (*message_iter_close_container)(Iter*, Iter*) = nullptr;
    Bool  (*message_iter_init)(Msg*, Iter*) = nullptr;
    int   (*message_iter_get_arg_type)(Iter*) = nullptr;
    void  (*message_iter_get_basic)(Iter*, void*) = nullptr;
    void  (*message_iter_recurse)(Iter*, Iter*) = nullptr;
    Bool  (*message_iter_next)(Iter*) = nullptr;
};

template <typename Fn>
bool bind(void* lib, const char* name, Fn& fn)
{
    void* sym = dlsym(lib, name);
    if (!sym) return false;
    std::memcpy(&fn, &sym, sizeof fn);   // POSIX: a dlsym result may be a function
    return true;
}

const Api& api()
{
    static const Api loaded = []() {
        Api a;
        void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return a;                       // never dlclose'd: lives with POM1
        bool ok = true;
        ok &= bind(lib, "dbus_error_init", a.error_init);
        ok &= bind(lib, "dbus_error_free", a.error_free);
        ok &= bind(lib, "dbus_error_is_set", a.error_is_set);
        ok &= bind(lib, "dbus_connection_open_private", a.connection_open_private);
        ok &= bind(lib, "dbus_bus_register", a.bus_register);
        ok &= bind(lib, "dbus_connection_set_exit_on_disconnect",
                   a.connection_set_exit_on_disconnect);
        ok &= bind(lib, "dbus_connection_close", a.connection_close);
        ok &= bind(lib, "dbus_connection_unref", a.connection_unref);
        ok &= bind(lib, "dbus_connection_read_write", a.connection_read_write);
        ok &= bind(lib, "dbus_connection_pop_message", a.connection_pop_message);
        ok &= bind(lib, "dbus_connection_send_with_reply_and_block",
                   a.connection_send_with_reply_and_block);
        ok &= bind(lib, "dbus_bus_get_unique_name", a.bus_get_unique_name);
        ok &= bind(lib, "dbus_bus_add_match", a.bus_add_match);
        ok &= bind(lib, "dbus_message_new_method_call", a.message_new_method_call);
        ok &= bind(lib, "dbus_message_unref", a.message_unref);
        ok &= bind(lib, "dbus_message_is_signal", a.message_is_signal);
        ok &= bind(lib, "dbus_message_get_path", a.message_get_path);
        ok &= bind(lib, "dbus_message_iter_init_append", a.message_iter_init_append);
        ok &= bind(lib, "dbus_message_iter_append_basic", a.message_iter_append_basic);
        ok &= bind(lib, "dbus_message_iter_open_container", a.message_iter_open_container);
        ok &= bind(lib, "dbus_message_iter_close_container", a.message_iter_close_container);
        ok &= bind(lib, "dbus_message_iter_init", a.message_iter_init);
        ok &= bind(lib, "dbus_message_iter_get_arg_type", a.message_iter_get_arg_type);
        ok &= bind(lib, "dbus_message_iter_get_basic", a.message_iter_get_basic);
        ok &= bind(lib, "dbus_message_iter_recurse", a.message_iter_recurse);
        ok &= bind(lib, "dbus_message_iter_next", a.message_iter_next);
        // libdbus otherwise sets SIGPIPE to SIG_IGN for the whole process on its
        // first connection. It writes with MSG_NOSIGNAL anyway; leave the
        // process's signal dispositions to POM1.
        void (*changeSigpipe)(Bool) = nullptr;
        if (bind(lib, "dbus_connection_set_change_sigpipe", changeSigpipe)) changeSigpipe(0);
        a.ok = ok;
        return a;
    }();
    return loaded;
}

// A D-Bus address value escapes every byte outside [-0-9A-Za-z_/.\*].
std::string escapeAddressValue(const std::string& v)
{
    static const char* hexd = "0123456789abcdef";
    std::string out;
    for (unsigned char c : v) {
        const bool plain = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                        || c == '-' || c == '_' || c == '/' || c == '.' || c == '\\' || c == '*';
        if (plain) { out.push_back(static_cast<char>(c)); continue; }
        out.push_back('%');
        out.push_back(hexd[c >> 4]);
        out.push_back(hexd[c & 0xF]);
    }
    return out;
}

// The session bus, WITHOUT libdbus's autolaunch: asking it for "the session
// bus" on a box that has none can spawn a dbus-daemon behind the user's back.
// $DBUS_SESSION_BUS_ADDRESS, else systemd's per-user socket, else nothing.
std::string sessionBusAddress()
{
    if (const char* a = std::getenv("DBUS_SESSION_BUS_ADDRESS"); a && *a) return a;
    if (const char* rt = std::getenv("XDG_RUNTIME_DIR"); rt && *rt) {
        const std::string sock = std::string(rt) + "/bus";
        struct stat st{};
        if (stat(sock.c_str(), &st) == 0 && S_ISSOCK(st.st_mode))
            return "unix:path=" + escapeAddressValue(sock);
    }
    return {};
}

// A private connection per dialog, closed on scope exit. Private so closing it
// is ours to do; exit-on-disconnect off so a dying bus cannot _exit() POM1.
struct Connection {
    Conn* c = nullptr;
    Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection()
    {
        if (c) { api().connection_close(c); api().connection_unref(c); }
    }
    bool open()
    {
        const Api& d = api();
        if (!d.ok) return false;
        const std::string address = sessionBusAddress();
        if (address.empty()) return false;
        Error err{};
        d.error_init(&err);
        c = d.connection_open_private(address.c_str(), &err);
        if (!c) { d.error_free(&err); return false; }
        d.connection_set_exit_on_disconnect(c, 0);
        if (!d.bus_register(c, &err)) { d.error_free(&err); return false; }
        return true;
    }
};

// ── message building ──
void appendString(Iter& it, const std::string& s)
{
    const char* p = s.c_str();   // caller has checked isValidUtf8
    api().message_iter_append_basic(&it, kString, &p);
}

template <typename Writer>
void appendOption(Iter& dict, const char* key, const char* signature, Writer write)
{
    const Api& d = api();
    Iter entry{}, variant{};
    d.message_iter_open_container(&dict, kDictEntry, nullptr, &entry);
    d.message_iter_append_basic(&entry, kString, &key);
    d.message_iter_open_container(&entry, kVariant, signature, &variant);
    write(variant);
    d.message_iter_close_container(&entry, &variant);
    d.message_iter_close_container(&dict, &entry);
}

// `ay`, NUL-terminated: the portal's type for a path, which is bytes, not text.
void appendPathBytes(Iter& variant, const std::string& path)
{
    const Api& d = api();
    Iter bytes{};
    d.message_iter_open_container(&variant, kArray, "y", &bytes);
    for (char ch : path) {
        const unsigned char b = static_cast<unsigned char>(ch);
        d.message_iter_append_basic(&bytes, kByte, &b);
    }
    const unsigned char nul = 0;
    d.message_iter_append_basic(&bytes, kByte, &nul);
    d.message_iter_close_container(&variant, &bytes);
}

// `a(sa(us))`: each filter is a name and a list of (0 = glob, pattern).
void appendFilters(Iter& variant, const std::vector<FileFilter>& filters)
{
    const Api& d = api();
    std::vector<FileFilter> all = filters;
    all.push_back(FileFilter{ "All files", {} });
    Iter list{};
    d.message_iter_open_container(&variant, kArray, "(sa(us))", &list);
    for (const auto& f : all) {
        Iter entry{}, patterns{};
        d.message_iter_open_container(&list, kStruct, nullptr, &entry);
        appendString(entry, portal::isValidUtf8(f.description) && !f.description.empty()
                                ? f.description : std::string("Files"));
        d.message_iter_open_container(&entry, kArray, "(us)", &patterns);
        for (const auto& glob : portal::globPatterns(f.extensions)) {
            if (!portal::isValidUtf8(glob)) continue;
            Iter pair{};
            const uint32_t kind = 0;
            d.message_iter_open_container(&patterns, kStruct, nullptr, &pair);
            d.message_iter_append_basic(&pair, kUint32, &kind);
            appendString(pair, glob);
            d.message_iter_close_container(&patterns, &pair);
        }
        d.message_iter_close_container(&entry, &patterns);
        d.message_iter_close_container(&list, &entry);
    }
    d.message_iter_close_container(&variant, &list);
}

// Request.Response (u response, a{sv} results) -> the first of results["uris"].
Pick parseResponse(Msg* signal, std::string& outPath)
{
    const Api& d = api();
    Iter it{};
    if (!d.message_iter_init(signal, &it) || d.message_iter_get_arg_type(&it) != kUint32)
        return Pick::Cancelled;
    uint32_t response = 0;
    d.message_iter_get_basic(&it, &response);
    // Anything but Success is a Cancel, including 2 ("ended some other way"):
    // xdg-desktop-portal-gtk answers 2 when the dialog is closed by its window
    // button. Treating that as a failure would strike the portal and pop zenity
    // at a user who just closed a dialog.
    if (response != static_cast<uint32_t>(portal::Response::Success)) return Pick::Cancelled;
    if (!d.message_iter_next(&it) || d.message_iter_get_arg_type(&it) != kArray)
        return Pick::Cancelled;
    Iter dict{};
    d.message_iter_recurse(&it, &dict);
    while (d.message_iter_get_arg_type(&dict) == kDictEntry) {
        Iter entry{};
        d.message_iter_recurse(&dict, &entry);
        const char* key = nullptr;
        if (d.message_iter_get_arg_type(&entry) == kString) d.message_iter_get_basic(&entry, &key);
        if (key && std::strcmp(key, "uris") == 0 && d.message_iter_next(&entry)
            && d.message_iter_get_arg_type(&entry) == kVariant) {
            Iter variant{}, uris{};
            d.message_iter_recurse(&entry, &variant);
            if (d.message_iter_get_arg_type(&variant) == kArray) {
                d.message_iter_recurse(&variant, &uris);
                if (d.message_iter_get_arg_type(&uris) == kString) {
                    const char* uri = nullptr;
                    d.message_iter_get_basic(&uris, &uri);
                    if (uri) outPath = portal::fileUriToPath(uri);
                }
            }
        }
        d.message_iter_next(&dict);
    }
    // A choice that is not a local file (an sftp:// the file manager offered)
    // cannot be loaded; report no path rather than hand a URL to fopen().
    return outPath.empty() ? Pick::Cancelled : Pick::Chosen;
}

} // namespace dbus

// "x11:<xid>" for POM1's own window, so the portal can make its dialog modal
// to it -- which also keeps it from opening BEHIND a fullscreen POM1. Best
// effort, and resolved at run time so this module keeps no GLFW dependency (its
// test links it alone): glfwGetX11Window only exists in an X11-capable GLFW,
// and GLFW 3.4's glfwGetPlatform says whether X11 is the one running. Anything
// else -- Wayland, GLFW linked statically -- sends "", which the portal accepts
// as "no parent".
std::string parentWindowId(GLFWwindow* parent)
{
    if (!parent) return {};
    void* getX11 = dlsym(RTLD_DEFAULT, "glfwGetX11Window");
    if (!getX11) return {};
    if (void* getPlatform = dlsym(RTLD_DEFAULT, "glfwGetPlatform")) {
        int (*platform)() = nullptr;
        std::memcpy(&platform, &getPlatform, sizeof platform);
        constexpr int kGlfwPlatformX11 = 0x00060004;
        if (platform() != kGlfwPlatformX11) return {};
    }
    unsigned long (*x11Window)(GLFWwindow*) = nullptr;
    std::memcpy(&x11Window, &getX11, sizeof x11Window);
    const unsigned long xid = x11Window(parent);
    if (!xid) return {};
    char buf[32];
    std::snprintf(buf, sizeof buf, "x11:%lx", xid);
    return buf;
}

// Is there a FileChooser to talk to? Reading its `version` property also
// starts the portal if it is D-Bus-activatable and not running yet. A bus with
// no portal answers at once (ServiceUnknown); the timeout only bounds a portal
// that is slow to come up.
bool portalAvailable()
{
    dbus::Connection conn;
    if (!conn.open()) return false;
    const dbus::Api& d = dbus::api();
    dbus::Msg* m = d.message_new_method_call(portal::kService, portal::kObject,
                                             "org.freedesktop.DBus.Properties", "Get");
    if (!m) return false;
    dbus::Iter args{};
    d.message_iter_init_append(m, &args);
    const char* iface = portal::kChooser;
    const char* prop = "version";
    d.message_iter_append_basic(&args, dbus::kString, &iface);
    d.message_iter_append_basic(&args, dbus::kString, &prop);
    dbus::Error err{};
    d.error_init(&err);
    dbus::Msg* reply = d.connection_send_with_reply_and_block(conn.c, m, 3000, &err);
    d.message_unref(m);
    if (!reply) { d.error_free(&err); return false; }
    uint32_t version = 0;
    dbus::Iter it{};
    if (d.message_iter_init(reply, &it) && d.message_iter_get_arg_type(&it) == dbus::kVariant) {
        dbus::Iter v{};
        d.message_iter_recurse(&it, &v);
        if (d.message_iter_get_arg_type(&v) == dbus::kUint32) d.message_iter_get_basic(&v, &version);
    }
    d.message_unref(reply);
    return version >= 1;
}

// OpenFile / SaveFile, then wait for the Response signal while pumping the
// host's event loop -- the same render-thread wait as the forked helpers below.
Pick runPortal(bool save, GLFWwindow* parent, const std::string& title,
               const std::string& defaultDir, const std::string& defaultName,
               const std::vector<FileFilter>& filters, std::string& outPath)
{
    dbus::Connection conn;
    if (!conn.open()) return Pick::Failed;
    const dbus::Api& d = dbus::api();

    static unsigned counter = 0;
    const std::string token = portal::handleToken(static_cast<unsigned long>(getpid()), ++counter);
    const char* unique = d.bus_get_unique_name(conn.c);
    if (!unique) return Pick::Failed;
    std::string handle = portal::requestPath(unique, token);

    // Subscribe BEFORE asking: the answer is a signal, and a fast portal can
    // send it before the method call has even returned.
    dbus::Error err{};
    d.error_init(&err);
    d.bus_add_match(conn.c, portal::responseMatchRule(handle).c_str(), &err);
    if (d.error_is_set(&err)) { d.error_free(&err); return Pick::Failed; }

    dbus::Msg* m = d.message_new_method_call(portal::kService, portal::kObject, portal::kChooser,
                                             save ? "SaveFile" : "OpenFile");
    if (!m) return Pick::Failed;
    dbus::Iter args{};
    d.message_iter_init_append(m, &args);
    dbus::appendString(args, parentWindowId(parent));
    const std::string fallbackTitle = save ? "Save File" : "Open File";
    dbus::appendString(args, !title.empty() && portal::isValidUtf8(title) ? title : fallbackTitle);

    dbus::Iter options{};
    d.message_iter_open_container(&args, dbus::kArray, "{sv}", &options);
    dbus::appendOption(options, "handle_token", "s",
                       [&](dbus::Iter& v) { dbus::appendString(v, token); });
    dbus::appendOption(options, "modal", "b", [&](dbus::Iter& v) {
        const dbus::Bool yes = 1;
        d.message_iter_append_basic(&v, dbus::kBool, &yes);
    });
    dbus::appendOption(options, "filters", "a(sa(us))",
                       [&](dbus::Iter& v) { dbus::appendFilters(v, filters); });
    if (!defaultDir.empty()) {
        std::error_code ec;
        const std::string dir = std::filesystem::absolute(defaultDir, ec).string();
        if (!ec)
            dbus::appendOption(options, "current_folder", "ay",
                               [&](dbus::Iter& v) { dbus::appendPathBytes(v, dir); });
    }
    if (save && !defaultName.empty() && portal::isValidUtf8(defaultName)) {
        dbus::appendOption(options, "current_name", "s",
                           [&](dbus::Iter& v) { dbus::appendString(v, defaultName); });
    }
    d.message_iter_close_container(&args, &options);

    dbus::Msg* reply = d.connection_send_with_reply_and_block(conn.c, m, 10000, &err);
    d.message_unref(m);
    if (!reply) { d.error_free(&err); return Pick::Failed; }   // no FileChooser after all
    const char* returned = nullptr;
    dbus::Iter r{};
    if (d.message_iter_init(reply, &r) && d.message_iter_get_arg_type(&r) == dbus::kObjectPath)
        d.message_iter_get_basic(&r, &returned);
    const std::string requestObject = returned ? returned : "";
    d.message_unref(reply);
    if (requestObject.empty()) return Pick::Failed;
    if (requestObject != handle) {
        // Portals before 0.9 ignored handle_token and invented the path. Follow it.
        handle = requestObject;
        d.bus_add_match(conn.c, portal::responseMatchRule(handle).c_str(), &err);
        if (d.error_is_set(&err)) { d.error_free(&err); return Pick::Failed; }
    }

    auto& pump = waitPumpFn();
    for (;;) {
        if (!d.connection_read_write(conn.c, pump ? 8 : 250)) return Pick::Failed;   // bus gone
        while (dbus::Msg* msg = d.connection_pop_message(conn.c)) {
            const char* path = d.message_get_path(msg);
            if (d.message_is_signal(msg, portal::kRequest, "Response") && path && handle == path) {
                const Pick p = dbus::parseResponse(msg, outPath);
                d.message_unref(msg);
                return p;
            }
            d.message_unref(msg);
        }
        if (pump) pump();
    }
}

// ── Backend choice ─────────────────────────────────────────────────────────
struct Backends {
    std::vector<Backend> order;     // what this session found, best first
    bool struck[4] = {};            // proved unable to run
    bool builtinForced = false;     // POM1_FILE_DIALOG=builtin
};

Backends& backends()
{
    static Backends b;
    return b;
}

void probeBackends()
{
    static std::once_flag once;
    std::call_once(once, []() {
        Backends& b = backends();
        const char* env = std::getenv("POM1_FILE_DIALOG");
        const std::string forced = env ? lowerAscii(env) : std::string();
        if (forced == "builtin" || forced == "none") { b.builtinForced = true; return; }
        const bool any = forced != "portal" && forced != "zenity" && forced != "kdialog";
        if ((any || forced == "portal")  && portalAvailable())  b.order.push_back(Backend::Portal);
        if ((any || forced == "zenity")  && onPath("zenity"))   b.order.push_back(Backend::Zenity);
        if ((any || forced == "kdialog") && onPath("kdialog"))  b.order.push_back(Backend::Kdialog);
    });
}

Backend activeBackend()
{
    probeBackends();
    for (Backend b : backends().order)
        if (!backends().struck[static_cast<int>(b)]) return b;
    return Backend::None;
}

// Run argv[], capture stdout up to 64 KB into `out`. Chosen = exit 0 with a
// path; Cancelled = a plain non-zero exit (both zenity and kdialog exit 1 on
// Cancel); Failed = the child could not RUN. Newlines are stripped — these
// tools terminate the path with \n.
//
// Failed used to be indistinguishable from a Cancel — both ended as an empty
// string — so a zenity that died on launch made File ▸ Load Memory do *nothing
// at all*: no dialog, no fallback, no log line. It is now what strikes the
// backend off (see openFile), and the caller falls back.
Pick runChildCapture(const std::vector<std::string>& argv, std::string& out)
{
    out.clear();
    int fds[2];
    if (pipe(fds) != 0) return Pick::Failed;

    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return Pick::Failed; }
    if (pid == 0) {
        // Child: redirect stdout to the pipe, drop stderr to /dev/null so the
        // GTK/KDE plug-init chatter doesn't pollute POM1's console.
        dup2(fds[1], STDOUT_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        std::vector<char*> raw;
        raw.reserve(argv.size() + 1);
        for (auto& s : argv) raw.push_back(const_cast<char*>(s.c_str()));
        raw.push_back(nullptr);
        execvp(raw[0], raw.data());
        _exit(127); // exec failed
    }

    close(fds[1]);

    // The picker is a separate process and this wait runs on POM1's render
    // thread, so a plain blocking read()+waitpid() parks the GLFW event loop
    // for the whole time the dialog is up. Nothing then answers the
    // compositor's xdg_shell ping and GNOME declares POM1 "not responding"
    // over a window that is simply waiting for the user to pick a file. So
    // poll instead of blocking, and hand the host a tick in between (see
    // setWaitPump). With no pump installed the poll timeout is infinite and
    // the behaviour is exactly the old blocking wait.
    auto& pump = waitPumpFn();
    const int pollTimeoutMs = pump ? 8 : -1;

    char buf[1024];
    bool eof = false;
    while (!eof && out.size() < 64 * 1024) {
        struct pollfd pfd{};
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, pollTimeoutMs);
        if (pr < 0) {
            if (errno == EINTR) continue;
            eof = true;                 // poll is broken: stop reading
        } else if (pr > 0) {
            ssize_t n = read(fds[0], buf, sizeof(buf));
            if (n > 0)                 out.append(buf, buf + n);
            else if (n == 0)           eof = true;   // child closed stdout
            else if (errno != EINTR)   eof = true;
        }
        if (pump) pump();
    }
    close(fds[0]);

    // stdout is closed but the child may not have reaped yet; keep pumping
    // across that gap too rather than reintroducing the stall we just removed.
    int status = 0;
    bool exited = false;
    for (;;) {
        pid_t r = waitpid(pid, &status, pump ? WNOHANG : 0);
        if (r == pid)  { exited = true; break; }
        if (r < 0)     { if (errno == EINTR) continue; break; }
        pump();                          // r == 0: still running (WNOHANG)
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    // 127 is the _exit() above when execvp failed; a signal death (no WIFEXITED)
    // means it started and crashed. Either way the backend cannot be used.
    // Nothing is logged from here: this module stays free of POM1 services (no
    // Logger, no Memory — the standalone rule that keeps it reusable by the
    // portable editor hosts), and its test links it alone. The caller notices
    // through isAvailable() / unavailableHint() and says so.
    if (!exited || !WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        out.clear();
        return Pick::Failed;
    }
    if (WEXITSTATUS(status) != 0) { out.clear(); return Pick::Cancelled; }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out.empty() ? Pick::Cancelled : Pick::Chosen;
}

// "*.bin *.txt" — zenity's --file-filter pattern syntax (space-separated).
std::string zenityPatterns(const std::vector<std::string>& exts)
{
    std::string out;
    if (exts.empty()) return "*";
    for (size_t i = 0; i < exts.size(); ++i) {
        if (i) out.push_back(' ');
        out.append("*.").append(exts[i]);
    }
    return out;
}

// "*.bin|*.txt|" — kdialog's filter syntax (pipe-separated patterns, then
// a description after the leftmost '|' — see `kdialog --help-all`).
std::string kdialogFilter(const std::vector<FileFilter>& filters)
{
    if (filters.empty()) return "*|All files";
    std::string out;
    for (size_t i = 0; i < filters.size(); ++i) {
        if (i) out.push_back('\n');
        const auto& f = filters[i];
        if (f.extensions.empty()) {
            out.append("*");
        } else {
            for (size_t j = 0; j < f.extensions.size(); ++j) {
                if (j) out.push_back(' ');
                out.append("*.").append(f.extensions[j]);
            }
        }
        out.push_back('|');
        out.append(f.description.empty() ? std::string("Files")
                                         : f.description);
    }
    out.append("\n*|All files");
    return out;
}

// zenity 4 (GTK4) ONLY honours the initial folder when --filename has a
// non-empty basename that is NOT itself an existing directory — a bare directory
// path (with or without a trailing slash) is silently ignored (verified by
// strace against zenity 4.0.1). So to open INSIDE `dir` we must give a
// basename: prefer an existing regular file (nice — it pre-selects one), else a
// synthetic non-existent name (GTK still switches to the folder, nothing
// selected). Never return a subdirectory name — that would navigate into it.
std::string zenityInitialFile(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) return it->path().filename().string();
    }
    return ".pom1";   // non-existent placeholder → GTK opens in `dir`
}

Pick runZenityOpen(const std::string& title,
                   const std::string& defaultDir,
                   const std::vector<FileFilter>& filters,
                   std::string& outPath)
{
    std::vector<std::string> argv = {
        "zenity", "--file-selection",
        "--title=" + (title.empty() ? std::string("Open File") : title),
    };
    if (!defaultDir.empty()) {
        // Point at a file inside the dir so GTK4 zenity actually opens there
        // (a directory-only --filename is ignored — see zenityInitialFile).
        argv.push_back("--filename=" + defaultDir + "/" + zenityInitialFile(defaultDir));
    }
    for (const auto& f : filters) {
        std::string spec = "--file-filter=";
        spec += f.description.empty() ? "Files" : f.description;
        spec += " | ";
        spec += zenityPatterns(f.extensions);
        argv.push_back(spec);
    }
    argv.push_back("--file-filter=All files | *");
    return runChildCapture(argv, outPath);
}

Pick runZenitySave(const std::string& title,
                   const std::string& defaultDir,
                   const std::string& defaultName,
                   const std::vector<FileFilter>& filters,
                   std::string& outPath)
{
    std::vector<std::string> argv = {
        "zenity", "--file-selection", "--save", "--confirm-overwrite",
        "--title=" + (title.empty() ? std::string("Save File") : title),
    };
    std::string filenameArg = "--filename=";
    if (!defaultDir.empty()) filenameArg += defaultDir + "/";
    // A non-empty basename is required for GTK4 zenity to honour the initial
    // folder (see zenityInitialFile); fall back to a placeholder when the caller
    // gave no suggested name so the dialog still opens in defaultDir.
    filenameArg += (defaultName.empty() && !defaultDir.empty()) ? ".pom1" : defaultName;
    if (filenameArg != "--filename=") argv.push_back(filenameArg);
    for (const auto& f : filters) {
        std::string spec = "--file-filter=";
        spec += f.description.empty() ? "Files" : f.description;
        spec += " | ";
        spec += zenityPatterns(f.extensions);
        argv.push_back(spec);
    }
    argv.push_back("--file-filter=All files | *");
    return runChildCapture(argv, outPath);
}

Pick runKdialogOpen(const std::string& title,
                    const std::string& defaultDir,
                    const std::vector<FileFilter>& filters,
                    std::string& outPath)
{
    std::vector<std::string> argv = {
        "kdialog", "--getopenfilename",
        defaultDir.empty() ? std::string(":") : defaultDir,
        kdialogFilter(filters),
    };
    if (!title.empty()) {
        argv.push_back("--title");
        argv.push_back(title);
    }
    return runChildCapture(argv, outPath);
}

Pick runKdialogSave(const std::string& title,
                    const std::string& defaultDir,
                    const std::string& defaultName,
                    const std::vector<FileFilter>& filters,
                    std::string& outPath)
{
    std::string start = defaultDir.empty() ? std::string(":")
                                           : (defaultDir + "/" + defaultName);
    if (defaultDir.empty() && !defaultName.empty()) start = defaultName;
    std::vector<std::string> argv = {
        "kdialog", "--getsavefilename", start, kdialogFilter(filters),
    };
    if (!title.empty()) {
        argv.push_back("--title");
        argv.push_back(title);
    }
    return runChildCapture(argv, outPath);
}

// When the user types a bare basename in a save dialog, append the first
// configured extension so callers don't need to. Mirrors the Win32 lpstrDefExt
// behaviour for the GTK side, which doesn't auto-suffix.
std::string ensureExtension(std::string path,
                            const std::vector<FileFilter>& filters)
{
    if (path.empty() || filters.empty()) return path;
    auto slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path
                                                    : path.substr(slash + 1);
    if (base.find('.') != std::string::npos) return path;
    const auto& exts = filters.front().extensions;
    if (exts.empty()) return path;
    path.push_back('.');
    path.append(exts.front());
    return path;
}

// One request, served by the best backend still standing: a backend that fails
// to run is struck off and the next one gets the SAME request, so the user sees
// a dialog as long as any of the three works.
template <typename Attempt>
bool serve(std::string& outPath, Attempt attempt)
{
    for (;;) {
        const Backend b = activeBackend();
        if (b == Backend::None) { outPath.clear(); return false; }
        const Pick p = attempt(b);
        if (p == Pick::Failed) {
            backends().struck[static_cast<int>(b)] = true;
            continue;
        }
        if (p != Pick::Chosen) outPath.clear();
        return p == Pick::Chosen;
    }
}

} // namespace

bool NativeFileDialog::platformAvailable()
{
    return activeBackend() != Backend::None;
}

bool NativeFileDialog::openFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    return serve(outPath, [&](Backend b) {
        switch (b) {
        case Backend::Portal:
            return runPortal(false, parent, title, defaultDir, {}, filters, outPath);
        case Backend::Zenity:
            return runZenityOpen(title, defaultDir, filters, outPath);
        case Backend::Kdialog:
            return runKdialogOpen(title, defaultDir, filters, outPath);
        default:
            return Pick::Failed;
        }
    });
}

bool NativeFileDialog::saveFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::string& defaultName,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    const bool ok = serve(outPath, [&](Backend b) {
        switch (b) {
        case Backend::Portal:
            return runPortal(true, parent, title, defaultDir, defaultName, filters, outPath);
        case Backend::Zenity:
            return runZenitySave(title, defaultDir, defaultName, filters, outPath);
        case Backend::Kdialog:
            return runKdialogSave(title, defaultDir, defaultName, filters, outPath);
        default:
            return Pick::Failed;
        }
    });
    if (!ok) return false;
    outPath = ensureExtension(std::move(outPath), filters);
    return true;
}

namespace {

// Why no desktop picker is available, for unavailableHint() below. Empty when
// one is, or when the user asked for POM1's own browser (POM1_FILE_DIALOG=builtin).
std::string linuxUnavailableReason()
{
    if (activeBackend() != Backend::None || backends().builtinForced) return {};
    if (!backends().order.empty())
        return "Your desktop's file dialog could not start, so POM1's own browser is "
               "used instead.";
    return "No desktop file dialog found (xdg-desktop-portal, zenity or kdialog), so "
           "POM1's own browser is used instead. Install zenity (GNOME, Cinnamon, XFCE, "
           "MATE) or kdialog (KDE) to get your desktop's.";
}

} // namespace

} // namespace pom1

#endif

// ── Platform-independent convenience (delegates to the per-platform open/save
//    above; on macOS those live in NativeFileDialog_Mac.mm but link the same). ─
namespace pom1 {

// User preference (Settings → "Native OS file dialogs"), persisted in
// ini/ui.settings as `native_dialogs`. When false, isAvailable() reports false
// even where a native picker exists, so every Load/Save flow uses the
// in-process ImGui browser (instant, no NSOpenPanel/XPC cold-start).
// Plain global — only ever touched from the UI thread.
namespace {

#if defined(__linux__) && !POM1_IS_WASM
// A Raspberry Pi announces itself in the device tree, e.g.
// "Raspberry Pi 400 Rev 1.1". Read once — the file is a few dozen bytes.
bool runningOnRaspberryPi()
{
    static const bool pi = []() {
        std::ifstream f("/proc/device-tree/model", std::ios::binary);
        if (!f) return false;
        std::string model((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        return model.find("Raspberry Pi") != std::string::npos;
    }();
    return pi;
}
#endif

// Compiled default. Native pickers everywhere EXCEPT the Raspberry Pi, where
// the ImGui browser is the default: the kiosk session (packaging/raspberrypi)
// runs a bare matchbox WM with no GTK/KDE desktop behind it, so a forked
// zenity/kdialog pays a multi-second cold start on an SD card, can land behind
// the fullscreen window, and may not be installed at all. POM1's in-process
// browser is instant and always there. Both the native GLES build tier
// (-DPOM1_GLES=ON, documented as "Raspberry Pi & co") and a runtime device-tree
// probe select it, because the Pi kiosk installer builds with plain `cmake`.
// A saved `native_dialogs` in ini/ui.settings always wins over this.
bool defaultNativeEnabled()
{
#if POM1_IS_WASM
    return false;                       // no native backend at all
#elif POM1_GL_ES
    return false;                       // native GLES tier = Raspberry Pi & co
#elif defined(__linux__)
    return !runningOnRaspberryPi();
#else
    return true;
#endif
}

bool& nativeEnabledFlag()
{
    static bool v = defaultNativeEnabled();
    return v;
}

} // namespace

void NativeFileDialog::setEnabled(bool enabled) { nativeEnabledFlag() = enabled; }
bool NativeFileDialog::isEnabled() { return nativeEnabledFlag(); }
bool NativeFileDialog::defaultEnabled() { return defaultNativeEnabled(); }

bool NativeFileDialog::isAvailable()
{
    // On Linux a backend that proved it cannot run is struck off, so
    // platformAvailable() already goes false once none is left standing.
    return nativeEnabledFlag() && platformAvailable();
}

std::string NativeFileDialog::unavailableHint()
{
    if (!nativeEnabledFlag()) return {};     // the user chose POM1's browser
#if !POM1_IS_WASM && !defined(_WIN32) && !defined(__APPLE__)
    return linuxUnavailableReason();
#else
    return {};                               // WASM has none by design; Win32/Cocoa always do
#endif
}

bool NativeFileDialog::pickFiltered(GLFWwindow* parent,
                                    bool forSave,
                                    const std::string& title,
                                    const std::string& filterDesc,
                                    const std::string& extCsv,
                                    const std::string& defaultDir,
                                    const std::string& defaultName,
                                    std::string& outPath)
{
    if (!isAvailable()) return false;   // caller falls back to its ImGui browser

    // Split "png,jpg,bmp" → {"png","jpg","bmp"}, trimming dots/spaces so callers
    // can be sloppy. An empty list means "match everything".
    std::vector<std::string> exts;
    std::string cur;
    auto flush = [&]() {
        size_t a = cur.find_first_not_of(" .\t");
        size_t b = cur.find_last_not_of(" .\t");
        if (a != std::string::npos) exts.push_back(cur.substr(a, b - a + 1));
        cur.clear();
    };
    for (char c : extCsv) { if (c == ',') flush(); else cur += c; }
    flush();

    std::vector<FileFilter> filters;
    filters.push_back(FileFilter{ filterDesc, exts });
    // For loads, append an "All files" filter so the user is never boxed in. For
    // saves, leave it off: saveFile only auto-appends the extension when there is
    // exactly one filter with one extension, and we want that auto-append.
    if (!forSave)
        filters.push_back(FileFilter{ "All files (*.*)", {} });

    return forSave
        ? saveFile(parent, title, defaultDir, defaultName, filters, outPath)
        : openFile(parent, title, defaultDir, filters, outPath);
}

} // namespace pom1
