#pragma once

#include <string>
#include <string_view>
#include <vector>

struct RecentLoad {
    bool ok = false;
    std::vector<std::string> paths;
};

std::vector<std::string> parseRecent(std::string_view text);
std::string serializeRecent(const std::vector<std::string>& paths);
std::vector<std::string> rememberPath(std::vector<std::string> paths, std::string path, int cap);
std::vector<std::string> forgetPath(std::vector<std::string> paths, std::string_view path);
RecentLoad loadRecent(const std::string& file);
bool saveRecent(const std::string& file, const std::vector<std::string>& paths, std::string& error);
