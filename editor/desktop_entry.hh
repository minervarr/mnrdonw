#pragma once

#include <string>
#include <string_view>

// The .desktop file body for this executable, or empty when the path cannot
// be quoted safely.
std::string desktopEntryText(std::string_view execPath);
