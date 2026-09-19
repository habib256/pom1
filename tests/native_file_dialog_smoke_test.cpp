// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// native_file_dialog_smoke_test.cpp — the Linux picker's WAIT, not its UI.
//
// POM1 has no GTK/Qt dependency: on Linux it forks `zenity`/`kdialog` and reads
// the chosen path off a pipe. That wait runs on the render thread, and it used
// to be a blocking read() + waitpid() — so for as long as the picker was up,
// NOTHING drained the GLFW/Wayland queue, the compositor's xdg_shell ping went
// unanswered, and GNOME put up "POM1 is not responding" over a window that was
// merely waiting for the user to choose a file. Reported against the BBS flow
// (File > Load Memory > software/NET/bbs.*.txt), but every Load/Save shared it.
//
// The fix is NativeFileDialog::setWaitPump: the wait polls instead of blocking
// and hands the host a tick in between. That is what this pins — and it is
// testable without a desktop because the backend is just a child process, so a
// fake `zenity` on $PATH stands in for the real one.
//
// Sections:
//   1. a slow picker is pumped throughout, not just once at the end
//   2. the pumped path still returns the child's answer verbatim
//   3. a cancel (non-zero exit) is still a clean "false", pump or no pump
//   4. with no pump installed the old blocking wait still works

#include "NativeFileDialog.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// Write a fake `zenity` that sleeps `delayMs`, echoes `reply` and exits with
// `exitCode`, and PREPEND its directory to $PATH. Prepend rather than replace:
// the backend resolves the picker by walking $PATH in order, so ours still
// wins, while the script keeps a working /bin for `sleep` (replacing $PATH
// outright leaves the script with no sleep at all, and the "slow" child then
// answers instantly — which silently defeats the whole point of the test).
void installFakeZenity(const fs::path& dir, int delayMs, const std::string& reply,
                       int exitCode)
{
    static const std::string basePath = []() {
        const char* p = std::getenv("PATH");
        return std::string(p ? p : "/usr/bin:/bin");
    }();

    fs::create_directories(dir);
    const fs::path exe = dir / "zenity";
    {
        std::ofstream f(exe);
        f << "#!/bin/sh\n"
          << "sleep " << (delayMs / 1000.0) << "\n";
        if (!reply.empty())
            f << "echo '" << reply << "'\n";
        f << "exit " << exitCode << "\n";
    }
    fs::permissions(exe, fs::perms::owner_all | fs::perms::group_exec |
                             fs::perms::others_exec);
    setenv("PATH", (dir.string() + ":" + basePath).c_str(), 1);
}

} // namespace

int main()
{
    // Put the fake zenity in charge whatever this desktop has: without this, a
    // box with a live xdg-desktop-portal would answer every openFile() below
    // with a REAL dialog (portal_file_dialog_smoke tests that backend, against
    // a fake portal on a private bus).
    setenv("POM1_FILE_DIALOG", "zenity", 1);

    const fs::path sandbox =
        fs::temp_directory_path() / ("pom1_nfd_smoke_" + std::to_string(::getpid()));
    fs::create_directories(sandbox);

    // ---- 1 + 2: a slow picker is pumped for its whole life ------------------
    //
    // 300 ms is far longer than the 8 ms poll tick, so a wait that pumps gets
    // called tens of times. The old blocking wait would call it zero times —
    // which is exactly the shape of the bug: one long silence, then the answer.
    {
        installFakeZenity(sandbox / "bin1", 300, "/tmp/picked.txt", 0);

        int pumped = 0;
        pom1::NativeFileDialog::setWaitPump([&pumped]() { ++pumped; });

        std::string out;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = pom1::NativeFileDialog::openFile(
            nullptr, "probe", "", {}, out);
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        assert(ok && "fake zenity exited 0, so the pick must succeed");
        assert(out == "/tmp/picked.txt" && "the child's stdout is the answer");
        assert(elapsedMs >= 250 && "we really did wait for the slow child");

        // The floor is deliberately loose (a loaded CI box schedules the poll
        // loop unevenly); the assertion that matters is "many, not one".
        assert(pumped > 10 &&
               "the host must be pumped THROUGHOUT the wait, not once at the end");
        std::cout << "[1+2] slow picker: pumped " << pumped << "x over "
                  << elapsedMs << " ms, answer round-tripped\n";
    }

    // ---- 3: cancel is still a clean false ----------------------------------
    //
    // zenity exits 1 on Cancel and prints nothing. The pumped loop must not
    // turn that into a bogus empty-string "success".
    {
        installFakeZenity(sandbox / "bin2", 60, "", 1);
        int pumped = 0;
        pom1::NativeFileDialog::setWaitPump([&pumped]() { ++pumped; });

        std::string out = "untouched";
        const bool ok = pom1::NativeFileDialog::openFile(
            nullptr, "probe", "", {}, out);
        assert(!ok && "a cancelled picker is not a pick");
        assert(pumped > 0 && "even a fast child gets at least one tick");
        std::cout << "[3] cancel: false, pumped " << pumped << "x\n";
    }

    // ---- 4: no pump installed = the old blocking wait, still correct --------
    //
    // The pump is optional. Unset, poll() gets an infinite timeout and waitpid()
    // blocks, which is precisely the pre-fix behaviour — headless callers (and
    // any platform that never installs one) must keep working.
    {
        installFakeZenity(sandbox / "bin3", 60, "/tmp/blocking.txt", 0);
        pom1::NativeFileDialog::setWaitPump({});

        std::string out;
        const bool ok = pom1::NativeFileDialog::openFile(
            nullptr, "probe", "", {}, out);
        assert(ok && out == "/tmp/blocking.txt" &&
               "the un-pumped wait must still return the child's answer");
        std::cout << "[4] no pump: blocking wait still returns " << out << "\n";
    }

    // ---- 5: a picker that cannot RUN is not a Cancel -----------------------
    //
    // Exit 127 is what the forked child's _exit() reports when execvp failed,
    // and a signal death means it started and crashed. Both used to be
    // indistinguishable from a Cancel — every path ended as an empty string —
    // so File ▸ Load Memory did NOTHING: no dialog, no fallback, no log line.
    // Uncle Bernie reported exactly that shape on a Mint 17 box (an AppImage
    // exports its own LD_LIBRARY_PATH, the forked system zenity inherits it,
    // loads POM1's bundled GTK instead of the distribution's, and dies).
    //
    // The contract is that the backend disarms ITSELF, so every caller can ask
    // one question — isAvailable() — and fall back to POM1's own browser. This
    // section must run LAST: the flag is deliberately sticky for the session.
    {
        assert(pom1::NativeFileDialog::isAvailable() &&
               "precondition: the fake zenity above is still on $PATH");

        installFakeZenity(sandbox / "bin5", 0, "", 127);   // exec-failure shape
        std::string out;
        const bool ok = pom1::NativeFileDialog::openFile(
            nullptr, "probe", "", {}, out);
        assert(!ok && out.empty() && "a picker that cannot run reports no path");
        assert(!pom1::NativeFileDialog::isAvailable() &&
               "a picker that cannot run must disarm itself so callers fall back");

        // And it STAYS disarmed even though $PATH now holds a perfectly good
        // one: re-probing per call would put the dead picker back in front of
        // the user on their next Load, which is the loop this replaces.
        installFakeZenity(sandbox / "bin6", 0, "/tmp/works.txt", 0);
        assert(!pom1::NativeFileDialog::isAvailable() &&
               "the disarm is sticky for the session");
        std::cout << "[5] unusable backend disarms itself, stays disarmed\n";
    }

    fs::remove_all(sandbox);
    std::cout << "native_file_dialog_smoke: OK\n";
    return 0;
}
