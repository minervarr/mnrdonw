#pragma once

// Writes the Linux desktop entry for this process when it is missing or stale.
// Does nothing on other platforms, and never prevents the editor from starting.
void registerLinuxDesktopEntry();
