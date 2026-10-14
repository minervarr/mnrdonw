#include "recent.hh"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

std::vector<std::string> parseRecent(std::string_view text) {
    std::vector<std::string> paths;
    size_t i = 0;
    while (i < text.size()) {
        size_t end = text.find('\n', i);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(i, end - i);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        i = end < text.size() ? end + 1 : text.size();
        if (line.empty()) continue;
        const std::string path(line);
        bool seen = false;
        for (const std::string& have : paths) {
            if (have == path) {
                seen = true;
                break;
            }
        }
        if (!seen) paths.push_back(path);
    }
    return paths;
}

std::string serializeRecent(const std::vector<std::string>& paths) {
    std::string out;
    for (const std::string& path : paths) {
        out += path;
        out += '\n';
    }
    return out;
}

std::vector<std::string> rememberPath(std::vector<std::string> paths, std::string path, int cap) {
    if (path.empty() || cap <= 0) return paths;
    for (auto it = paths.begin(); it != paths.end();) {
        if (*it == path) it = paths.erase(it);
        else ++it;
    }
    paths.insert(paths.begin(), std::move(path));
    if (static_cast<int>(paths.size()) > cap) paths.resize(static_cast<size_t>(cap));
    return paths;
}

std::vector<std::string> forgetPath(std::vector<std::string> paths, std::string_view path) {
    const std::string drop(path);
    for (auto it = paths.begin(); it != paths.end();) {
        if (*it == drop) it = paths.erase(it);
        else ++it;
    }
    return paths;
}

RecentLoad loadRecent(const std::string& file) {
    struct stat st;
    if (stat(file.c_str(), &st) != 0) {
        if (errno == ENOENT) return {true, {}};
        return {false, {}};
    }
    if (!S_ISREG(st.st_mode)) return {false, {}};

    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) return {false, {}};
    std::string text;
    char buf[4096];
    while (true) {
        const size_t n = std::fread(buf, 1, sizeof buf, f);
        text.append(buf, n);
        if (n < sizeof buf) break;
    }
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) return {false, {}};
    return {true, parseRecent(text)};
}

bool saveRecent(const std::string& file, const std::vector<std::string>& paths, std::string& error) {
    const std::string bytes = serializeRecent(paths);
    const std::string tmp = file + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        error = std::strerror(errno);
        return false;
    }
    const char* data = bytes.data();
    size_t left = bytes.size();
    while (left > 0) {
        const ssize_t n = write(fd, data, left);
        if (n <= 0) {
            const int err = n < 0 ? errno : EIO;
            close(fd);
            unlink(tmp.c_str());
            error = std::strerror(err);
            return false;
        }
        data += n;
        left -= static_cast<size_t>(n);
    }
    if (close(fd) != 0) {
        const int err = errno;
        unlink(tmp.c_str());
        error = std::strerror(err);
        return false;
    }
    if (rename(tmp.c_str(), file.c_str()) != 0) {
        const int err = errno;
        unlink(tmp.c_str());
        error = std::strerror(err);
        return false;
    }
    error.clear();
    return true;
}
