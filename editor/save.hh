#pragma once

#include <string>

struct SaveResult {
    bool ok;
    std::string error;
    std::string path;
};

SaveResult saveBuffer(const std::string& bytes, const std::string& path);
