#include "linux_desktop.hh"

#include "editor/desktop_entry.hh"

#include <string>

#if defined(__linux__)

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace stdfs = std::filesystem;

std::string readWhole(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string text;
    char buf[4096];
    while (in) {
        in.read(buf, sizeof buf);
        text.append(buf, static_cast<size_t>(in.gcount()));
    }
    return text;
}

bool writeAtomically(const std::string& path, const std::string& bytes) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            out.close();
            std::error_code ec;
            stdfs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    stdfs::rename(tmp, path, ec);
    if (ec) {
        stdfs::remove(tmp, ec);
        return false;
    }
    return true;
}

void updateDesktopDatabase(const std::string& dir) {
    const pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        execlp("update-desktop-database", "update-desktop-database", dir.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

}  // namespace

void registerLinuxDesktopEntry() {
    char buf[8192];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof buf) return;
    const std::string exec(buf, static_cast<size_t>(n));
    const std::string text = desktopEntryText(exec);
    if (text.empty()) return;

    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') return;
    const std::string dir = std::string(home) + "/.local/share/applications";
    std::error_code ec;
    stdfs::create_directories(dir, ec);
    if (ec) return;

    const std::string dest = dir + "/io.nava.mnrdonw.desktop";
    if (readWhole(dest) == text) return;
    if (!writeAtomically(dest, text)) return;
    updateDesktopDatabase(dir);
}

#else

void registerLinuxDesktopEntry() {}

#endif
