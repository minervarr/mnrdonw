#pragma once

#include <cstddef>
#include <string_view>
#include <utility>

size_t utf8Prev(std::string_view s, size_t pos);
size_t utf8Next(std::string_view s, size_t pos);
size_t prevWord(std::string_view s, size_t pos);
size_t nextWord(std::string_view s, size_t pos);
std::pair<size_t, size_t> wordAt(std::string_view s, size_t pos);
size_t codepointCount(std::string_view s);
