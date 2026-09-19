// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// portal_file_dialog_smoke_test.cpp — the Linux desktop-portal file picker,
// against a FAKE portal on a PRIVATE bus.
//
// NativeFileDialog's first Linux backend asks org.freedesktop.portal.FileChooser
// over D-Bus. The protocol has a trap that fails silently and forever: the
// answer is a Request.Response SIGNAL on an object path the client must
// predict and subscribe to BEFORE calling; predict it wrong and the wait never
// ends. A real portal cannot run in CI, so this test starts its own
// dbus-daemon — with a config that has NO service directories, so no real
// portal can be activated behind the test's back — and forks a fake portal
// that plays the real one's part: it answers Properties.Get(version), replies
// to OpenFile/SaveFile with the request path derived from the caller's unique
// name and handle_token, then emits Response on it, following a script.
//
// Sections:
//   1. a pick round-trips, percent-decoded, with the host pumped meanwhile
//   2. Cancel (1) and "closed some other way" (2) are Cancels: nothing struck
//   3. SaveFile carries title, folder, name and filters; the extension is added
//   4. a portal that errors is struck off and zenity serves the SAME request
//   5. every backend failing leaves POM1's own browser, which says so
//   6. nothing found at all: the hint names what to install (child process)
//   7. POM1_FILE_DIALOG=builtin: no picker, and no hint (child process)
//
// Skips (77) when dbus-daemon or libdbus-1 is not on the box.

#include "NativeFileDialog.h"
#include "PortalFileChooser.h"

#include <cassert>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ── the fake portal's own libdbus table (dlopen'd, like POM1's) ─────────────
struct Error { const char* name; const char* message; unsigned int flags; void* padding; };
struct Iter { alignas(void*) unsigned char opaque[128]; };
struct Conn;
struct Msg;
using Bool = uint32_t;

struct Dbus {
    void  (*error_init)(Error*);
    void  (*error_free)(Error*);
    Conn* (*connection_open_private)(const char*, Error*);
    Bool  (*bus_register)(Conn*, Error*);
    int   (*bus_request_name)(Conn*, const char*, unsigned int, Error*);
    Bool  (*connection_read_write)(Conn*, int);
    Msg*  (*connection_pop_message)(Conn*);
    Bool  (*connection_send)(Conn*, Msg*, uint32_t*);
    void  (*connection_flush)(Conn*);
    Bool  (*message_is_method_call)(Msg*, const char*, const char*);
    const char* (*message_get_sender)(Msg*);
    const char* (*message_get_signature)(Msg*);
    Msg*  (*message_new_method_return)(Msg*);
    Msg*  (*message_new_signal)(const char*, const char*, const char*);
    Msg*  (*message_new_error)(Msg*, const char*, const char*);
    void  (*message_unref)(Msg*);
    void  (*message_iter_init_append)(Msg*, Iter*);
    Bool  (*message_iter_append_basic)(Iter*, int, const void*);
    Bool  (*message_iter_open_container)(Iter*, int, const char*, Iter*);
    Bool  (*message_iter_close_container)(Iter*, Iter*);
    Bool  (*message_iter_init)(Msg*, Iter*);
    int   (*message_iter_get_arg_type)(Iter*);
    void  (*message_iter_get_basic)(Iter*, void*);
    void  (*message_iter_recurse)(Iter*, Iter*);
    Bool  (*message_iter_next)(Iter*);
};

template <typename Fn>
bool bind(void* lib, const char* name, Fn& fn)
{
    void* sym = dlsym(lib, name);
    if (!sym) return false;
    std::memcpy(&fn, &sym, sizeof fn);
    return true;
}

bool loadDbus(Dbus& d)
{
    void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return false;
    bool ok = true;
    ok &= bind(lib, "dbus_error_init", d.error_init);
    ok &= bind(lib, "dbus_error_free", d.error_free);
    ok &= bind(lib, "dbus_connection_open_private", d.connection_open_private);
    ok &= bind(lib, "dbus_bus_register", d.bus_register);
    ok &= bind(lib, "dbus_bus_request_name", d.bus_request_name);
    ok &= bind(lib, "dbus_connection_read_write", d.connection_read_write);
    ok &= bind(lib, "dbus_connection_pop_message", d.connection_pop_message);
    ok &= bind(lib, "dbus_connection_send", d.connection_send);
    ok &= bind(lib, "dbus_connection_flush", d.connection_flush);
    ok &= bind(lib, "dbus_message_is_method_call", d.message_is_method_call);
    ok &= bind(lib, "dbus_message_get_sender", d.message_get_sender);
    ok &= bind(lib, "dbus_message_get_signature", d.message_get_signature);
    ok &= bind(lib, "dbus_message_new_method_return", d.message_new_method_return);
    ok &= bind(lib, "dbus_message_new_signal", d.message_new_signal);
    ok &= bind(lib, "dbus_message_new_error", d.message_new_error);
    ok &= bind(lib, "dbus_message_unref", d.message_unref);
    ok &= bind(lib, "dbus_message_iter_init_append", d.message_iter_init_append);
    ok &= bind(lib, "dbus_message_iter_append_basic", d.message_iter_append_basic);
    ok &= bind(lib, "dbus_message_iter_open_container", d.message_iter_open_container);
    ok &= bind(lib, "dbus_message_iter_close_container", d.message_iter_close_container);
    ok &= bind(lib, "dbus_message_iter_init", d.message_iter_init);
    ok &= bind(lib, "dbus_message_iter_get_arg_type", d.message_iter_get_arg_type);
    ok &= bind(lib, "dbus_message_iter_get_basic", d.message_iter_get_basic);
    ok &= bind(lib, "dbus_message_iter_recurse", d.message_iter_recurse);
    ok &= bind(lib, "dbus_message_iter_next", d.message_iter_next);
    return ok;
}

bool onPath(const char* binary)
{
    const char* path = std::getenv("PATH");
    std::stringstream ss(path ? path : "");
    for (std::string dir; std::getline(ss, dir, ':');)
        if (!dir.empty() && access((dir + "/" + binary).c_str(), X_OK) == 0) return true;
    return false;
}

// A child of this test must not outlive it: the fake portal and the bus would
// keep ctest's output pipe open after a failed assert, and ctest would wait on
// them until its timeout. Linux says so with PDEATHSIG (it survives exec).
void dieWithParent(pid_t parent)
{
#if defined(__linux__)
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    if (getppid() != parent) _exit(0);     // the parent died before prctl ran
}

// ── the fake portal ─────────────────────────────────────────────────────────
//
// One line per FileChooser call goes to `logPath`:
//   method|title|handle_token|current_folder|current_name|filters
// where filters is "name=pat,pat;name=pat". The script says how to answer the
// n-th call: "ok:<uri>", "cancel" (1), "closed" (2) or "error" (a D-Bus error
// instead of a Request).
std::string readString(const Dbus& d, Iter& it)
{
    const char* s = nullptr;
    if (d.message_iter_get_arg_type(&it) == 's' || d.message_iter_get_arg_type(&it) == 'o')
        d.message_iter_get_basic(&it, &s);
    return s ? s : "";
}

void logCall(const Dbus& d, Msg* call, const char* method, std::string& token,
             const std::string& logPath)
{
    Iter args{};
    d.message_iter_init(call, &args);
    d.message_iter_next(&args);                         // parent_window
    const std::string title = readString(d, args);
    d.message_iter_next(&args);
    std::string folder, name, filters;
    Iter dict{};
    d.message_iter_recurse(&args, &dict);
    while (d.message_iter_get_arg_type(&dict) == 'e') {
        Iter entry{}, value{};
        d.message_iter_recurse(&dict, &entry);
        const std::string key = readString(d, entry);
        d.message_iter_next(&entry);
        d.message_iter_recurse(&entry, &value);
        if (key == "handle_token") token = readString(d, value);
        else if (key == "current_name") name = readString(d, value);
        else if (key == "current_folder") {
            Iter bytes{};
            d.message_iter_recurse(&value, &bytes);
            while (d.message_iter_get_arg_type(&bytes) == 'y') {
                unsigned char b = 0;
                d.message_iter_get_basic(&bytes, &b);
                if (b) folder.push_back(static_cast<char>(b));
                d.message_iter_next(&bytes);
            }
        } else if (key == "filters") {
            Iter list{};
            d.message_iter_recurse(&value, &list);
            while (d.message_iter_get_arg_type(&list) == 'r') {
                Iter f{}, pats{};
                d.message_iter_recurse(&list, &f);
                if (!filters.empty()) filters += ";";
                filters += readString(d, f) + "=";
                d.message_iter_next(&f);
                d.message_iter_recurse(&f, &pats);
                bool first = true;
                while (d.message_iter_get_arg_type(&pats) == 'r') {
                    Iter pair{};
                    d.message_iter_recurse(&pats, &pair);
                    d.message_iter_next(&pair);         // (u) kind = 0 (glob)
                    filters += (first ? "" : ",") + readString(d, pair);
                    first = false;
                    d.message_iter_next(&pats);
                }
                d.message_iter_next(&list);
            }
        }
        d.message_iter_next(&dict);
    }
    std::ofstream(logPath, std::ios::app) << method << "|" << title << "|" << token << "|"
                                          << folder << "|" << name << "|" << filters << "\n";
}

[[noreturn]] void runFakePortal(const std::string& address, const std::vector<std::string>& script,
                                const std::string& logPath, int readyFd)
{
    Dbus d{};
    if (!loadDbus(d)) _exit(2);
    Error err{};
    d.error_init(&err);
    Conn* c = d.connection_open_private(address.c_str(), &err);
    if (!c || !d.bus_register(c, &err)) _exit(3);
    if (d.bus_request_name(c, pom1::portal::kService, 4 /*DO_NOT_QUEUE*/, &err) != 1) _exit(4);
    (void)!write(readyFd, "R", 1);
    close(readyFd);

    std::size_t n = 0;
    for (;;) {
        if (!d.connection_read_write(c, 100)) _exit(0);        // bus gone: test over
        while (Msg* m = d.connection_pop_message(c)) {
            if (d.message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")) {
                Msg* reply = d.message_new_method_return(m);
                Iter it{}, v{};
                d.message_iter_init_append(reply, &it);
                d.message_iter_open_container(&it, 'v', "u", &v);
                const uint32_t version = 3;
                d.message_iter_append_basic(&v, 'u', &version);
                d.message_iter_close_container(&it, &v);
                d.connection_send(c, reply, nullptr);
                d.message_unref(reply);
            } else if (d.message_is_method_call(m, pom1::portal::kChooser, "OpenFile")
                       || d.message_is_method_call(m, pom1::portal::kChooser, "SaveFile")) {
                const bool save = d.message_is_method_call(m, pom1::portal::kChooser, "SaveFile");
                const std::string step = n < script.size() ? script[n] : "cancel";
                ++n;
                if (std::strcmp(d.message_get_signature(m), "ssa{sv}") != 0) _exit(5);
                std::string token;
                logCall(d, m, save ? "SaveFile" : "OpenFile", token, logPath);
                if (step == "error") {
                    Msg* e = d.message_new_error(m, "org.freedesktop.DBus.Error.Failed", "scripted");
                    d.connection_send(c, e, nullptr);
                    d.message_unref(e);
                    d.message_unref(m);
                    continue;
                }
                // As the real portal: the Request lives at the path derived from
                // the CALLER's unique name and the token it chose.
                const std::string path = pom1::portal::requestPath(d.message_get_sender(m), token);
                Msg* reply = d.message_new_method_return(m);
                Iter it{};
                d.message_iter_init_append(reply, &it);
                const char* p = path.c_str();
                d.message_iter_append_basic(&it, 'o', &p);
                d.connection_send(c, reply, nullptr);
                d.message_unref(reply);
                d.connection_flush(c);

                // The user "thinks" for a moment -- long enough that a waiter
                // which does not pump the host is visible as zero ticks.
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                Msg* sig = d.message_new_signal(p, pom1::portal::kRequest, "Response");
                Iter s{}, results{};
                d.message_iter_init_append(sig, &s);
                const uint32_t code = step.rfind("ok:", 0) == 0 ? 0 : step == "closed" ? 2 : 1;
                d.message_iter_append_basic(&s, 'u', &code);
                d.message_iter_open_container(&s, 'a', "{sv}", &results);
                if (code == 0) {
                    const std::string uri = step.substr(3);
                    Iter entry{}, v{}, uris{};
                    const char* key = "uris";
                    const char* u = uri.c_str();
                    d.message_iter_open_container(&results, 'e', nullptr, &entry);
                    d.message_iter_append_basic(&entry, 's', &key);
                    d.message_iter_open_container(&entry, 'v', "as", &v);
                    d.message_iter_open_container(&v, 'a', "s", &uris);
                    d.message_iter_append_basic(&uris, 's', &u);
                    d.message_iter_close_container(&v, &uris);
                    d.message_iter_close_container(&entry, &v);
                    d.message_iter_close_container(&results, &entry);
                }
                d.message_iter_close_container(&s, &results);
                d.connection_send(c, sig, nullptr);
                d.message_unref(sig);
                d.connection_flush(c);
            }
            d.message_unref(m);
        }
    }
}

// A private dbus-daemon with NO service directories: nothing can be activated,
// so the only portal on this bus is the fake one.
pid_t startBus(const fs::path& sandbox, std::string& address)
{
    const fs::path conf = sandbox / "bus.conf";
    std::ofstream(conf)
        << "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
        << " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
        << "<busconfig><type>session</type><listen>unix:dir=" << sandbox.string() << "</listen>"
        << "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
        << "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n";
    int fds[2];
    if (pipe(fds) != 0) return -1;
    const pid_t self = getpid();
    const pid_t pid = fork();
    if (pid == 0) {
        dieWithParent(self);
        // Close the read end FIRST: if pipe() handed out fd 3 for it, a dup2
        // onto 3 would replace it and the close would then shut the very write
        // end the daemon prints to ("Writing to pipe: Invalid argument").
        close(fds[0]);
        if (fds[1] != 3) { dup2(fds[1], 3); close(fds[1]); }
        const std::string confArg = "--config-file=" + conf.string();
        execlp("dbus-daemon", "dbus-daemon", confArg.c_str(), "--nofork", "--print-address=3",
               static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);
    char buf[512];
    std::string line;
    ssize_t r;
    while (line.find('\n') == std::string::npos && (r = read(fds[0], buf, sizeof buf)) > 0)
        line.append(buf, buf + r);
    close(fds[0]);
    address = line.substr(0, line.find('\n'));
    return address.empty() ? -1 : pid;
}

void installFakeZenity(const fs::path& dir, const std::string& reply, int exitCode)
{
    fs::create_directories(dir);
    const fs::path exe = dir / "zenity";
    {
        std::ofstream f(exe);
        f << "#!/bin/sh\n";
        if (!reply.empty()) f << "echo '" << reply << "'\n";
        f << "exit " << exitCode << "\n";
    }
    fs::permissions(exe, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
}

std::vector<std::string> readLines(const fs::path& p)
{
    std::vector<std::string> out;
    std::ifstream f(p);
    for (std::string l; std::getline(f, l);) out.push_back(l);
    return out;
}

// Sections 6 and 7 need a FRESH probe, which is once per process: re-run this
// binary in a child with a controlled environment and let it answer.
int childMode(const std::string& mode)
{
    pom1::NativeFileDialog::setEnabled(true);     // the Pi's default is off
    const bool avail = pom1::NativeFileDialog::isAvailable();
    const std::string hint = pom1::NativeFileDialog::unavailableHint();
    if (mode == "nothing")
        return (!avail && hint.find("Install zenity") != std::string::npos
                && hint.find("kdialog") != std::string::npos) ? 0 : 1;
    if (mode == "builtin")
        return (!avail && hint.empty()) ? 0 : 1;
    return 2;
}

int runChild(const char* self, const char* mode, const fs::path& emptyBin, const fs::path& runtime)
{
    const pid_t pid = fork();
    if (pid == 0) {
        unsetenv("DBUS_SESSION_BUS_ADDRESS");
        setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);    // no bus socket there
        setenv("PATH", emptyBin.c_str(), 1);              // no zenity, no kdialog
        if (std::string(mode) == "builtin") setenv("POM1_FILE_DIALOG", "builtin", 1);
        else unsetenv("POM1_FILE_DIALOG");
        execl(self, self, "--child", mode, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 99;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--child") return childMode(argv[2]);

    Dbus probe{};
    if (!onPath("dbus-daemon") || !loadDbus(probe)) {
        std::cout << "SKIP: dbus-daemon or libdbus-1 not available\n";
        return 77;
    }

    const fs::path sandbox =
        fs::temp_directory_path() / ("pom1_portal_smoke_" + std::to_string(::getpid()));
    fs::create_directories(sandbox);
    const fs::path logPath = sandbox / "portal.log";
    const fs::path picks = sandbox / "picks";
    fs::create_directories(picks);

    std::string address;
    const pid_t bus = startBus(sandbox, address);
    assert(bus > 0 && "private dbus-daemon must start");

    const std::vector<std::string> script = {
        "ok:file:///tmp/My%20Prog.apl",   // 1
        "cancel",                         // 2
        "closed",                         // 2 (window closed)
        "ok:file:///tmp/out/song",        // 3
        "error",                          // 4
    };
    int ready[2];
    assert(pipe(ready) == 0);
    const pid_t self = getpid();
    const pid_t fake = fork();
    if (fake == 0) {
        dieWithParent(self);
        close(ready[0]);
        runFakePortal(address, script, logPath.string(), ready[1]);
    }
    close(ready[1]);
    char r = 0;
    assert(read(ready[0], &r, 1) == 1 && r == 'R' && "fake portal owns its name");
    close(ready[0]);

    // This process talks to the private bus only, and finds a (fake) zenity as
    // the second backend.
    setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
    unsetenv("POM1_FILE_DIALOG");
    installFakeZenity(sandbox / "bin", "/tmp/from-zenity.txt", 0);
    setenv("PATH", ((sandbox / "bin").string() + ":/usr/bin:/bin").c_str(), 1);

    pom1::NativeFileDialog::setEnabled(true);     // the Pi's default is off
    // The pump doubles as a watchdog: a Response the client is not subscribed
    // to never arrives, and the wait would otherwise run into ctest's timeout
    // without a word. ~8 ms per tick: 1500 ticks is ~12 s of silence.
    int pumped = 0;
    pom1::NativeFileDialog::setWaitPump([&pumped]() {
        if (++pumped > 1500) {
            std::cout << "FAIL: no Response after ~12 s -- the client is not listening on "
                         "the path the portal answers on (requestPath / match rule)"
                      << std::endl;
            std::_Exit(1);
        }
    });

    // ---- 1: a pick round-trips ----------------------------------------------
    assert(pom1::NativeFileDialog::isAvailable() && "the fake portal must be found");
    assert(pom1::NativeFileDialog::unavailableHint().empty());
    {
        std::string out;
        const bool ok = pom1::NativeFileDialog::openFile(
            nullptr, "Load Program", picks.string(),
            {{"Apple-1 memory dumps", {"apl", "bin"}}}, out);
        assert(ok && out == "/tmp/My Prog.apl" && "the URI is percent-decoded to a path");
        assert(pumped > 5 && "the host is pumped while the portal thinks");
        std::cout << "[1] pick: " << out << " (pumped " << pumped << "x)\n";
    }

    // ---- 2: both kinds of "no" are Cancels ------------------------------------
    {
        std::string out = "untouched";
        assert(!pom1::NativeFileDialog::openFile(nullptr, "t", "", {}, out) && out.empty());
        assert(pom1::NativeFileDialog::isAvailable() && "a Cancel strikes nothing");
        // xdg-desktop-portal-gtk answers 2 when the window is closed by its
        // button. That is the user saying no, not the portal failing.
        assert(!pom1::NativeFileDialog::openFile(nullptr, "t", "", {}, out) && out.empty());
        assert(pom1::NativeFileDialog::isAvailable() && "response 2 strikes nothing either");
        std::cout << "[2] cancel (1) and closed (2): no path, portal kept\n";
    }

    // ---- 3: SaveFile, and what the portal was told ----------------------------
    {
        std::string out;
        const bool ok = pom1::NativeFileDialog::saveFile(
            nullptr, "Export SID song", picks.string(), "song",
            {{"ca65 include", {"inc"}}}, out);
        assert(ok && out == "/tmp/out/song.inc" && "a bare name gets the filter's extension");

        const auto calls = readLines(logPath);
        assert(calls.size() == 4);
        const std::string& open = calls[0];
        const std::string& save = calls[3];
        const std::string folder = fs::absolute(picks).string();
        assert(open.rfind("OpenFile|Load Program|pom1_", 0) == 0 && "title + a handle_token");
        assert(open.find("|" + folder + "||") != std::string::npos && "current_folder, no name");
        assert(open.find("Apple-1 memory dumps=*.apl,*.bin;All files=*") != std::string::npos
               && "filters, plus All files");
        assert(save.rfind("SaveFile|Export SID song|pom1_", 0) == 0);
        assert(save.find("|" + folder + "|song|ca65 include=*.inc;All files=*") != std::string::npos
               && "SaveFile carries folder, suggested name and filters");
        std::cout << "[3] save: " << out << "; options as sent: " << save << "\n";
    }

    // ---- 4: a failing portal hands the SAME request to zenity -----------------
    {
        std::string out;
        const bool ok = pom1::NativeFileDialog::openFile(nullptr, "t", "", {}, out);
        assert(ok && out == "/tmp/from-zenity.txt" && "zenity answered the request the portal failed");
        assert(pom1::NativeFileDialog::isAvailable() && "zenity still stands");
        assert(pom1::NativeFileDialog::unavailableHint().empty());
        assert(readLines(logPath).size() == 5 && "the portal was asked once, then struck off");
        // Struck for the session: the next request goes straight to zenity.
        out.clear();
        assert(pom1::NativeFileDialog::openFile(nullptr, "t", "", {}, out) && out == "/tmp/from-zenity.txt");
        assert(readLines(logPath).size() == 5 && "a struck portal is not asked again");
        std::cout << "[4] portal error -> zenity served the same request\n";
    }

    // ---- 5: everything failing leaves POM1's browser, which says why ----------
    {
        installFakeZenity(sandbox / "bin", "", 127);        // exec-failure shape
        std::string out;
        assert(!pom1::NativeFileDialog::openFile(nullptr, "t", "", {}, out));
        assert(!pom1::NativeFileDialog::isAvailable() && "no backend left: callers fall back");
        const std::string hint = pom1::NativeFileDialog::unavailableHint();
        assert(hint.find("could not start") != std::string::npos);
        std::cout << "[5] all failed -> hint: " << hint << "\n";
    }

    // ---- 6 + 7: fresh processes ---------------------------------------------
    {
        const fs::path emptyBin = sandbox / "empty-bin";
        const fs::path runtime = sandbox / "runtime";
        fs::create_directories(emptyBin);
        fs::create_directories(runtime);
        assert(runChild(argv[0], "nothing", emptyBin, runtime) == 0
               && "nothing found: no picker, and a hint naming zenity and kdialog");
        assert(runChild(argv[0], "builtin", emptyBin, runtime) == 0
               && "POM1_FILE_DIALOG=builtin: no picker and no hint");
        std::cout << "[6] nothing found -> install hint; [7] builtin -> silent\n";
    }

    kill(fake, SIGTERM);
    kill(bus, SIGTERM);
    waitpid(fake, nullptr, 0);
    waitpid(bus, nullptr, 0);
    fs::remove_all(sandbox);
    std::cout << "portal_file_dialog_smoke: OK\n";
    return 0;
}
