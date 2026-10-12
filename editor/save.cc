#include "save.hh"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

SaveResult saveBuffer(const std::string& bytes, const std::string& path) {
    std::string dest = path.empty() ? std::string("untitled.md") : path;
    if (path.empty()) {
        struct stat st;
        if (stat(dest.c_str(), &st) == 0) {
            return {false, "untitled.md already exists", dest};
        }
    }

    std::string tmp = dest + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        int err = errno;
        return {false, std::strerror(err), dest};
    }

    const char* data = bytes.data();
    size_t left = bytes.size();
    while (left > 0) {
        ssize_t n = write(fd, data, left);
        if (n <= 0) {
            int err = n < 0 ? errno : EIO;
            close(fd);
            unlink(tmp.c_str());
            return {false, std::strerror(err), dest};
        }
        data += n;
        left -= static_cast<size_t>(n);
    }

    if (close(fd) != 0) {
        int err = errno;
        unlink(tmp.c_str());
        return {false, std::strerror(err), dest};
    }

    if (rename(tmp.c_str(), dest.c_str()) != 0) {
        int err = errno;
        unlink(tmp.c_str());
        return {false, std::strerror(err), dest};
    }

    return {true, "", dest};
}
