#!/usr/bin/env bash
# Desktop Linux build -> <repo>/build/linux (Release) or build/linux_debug (Debug)
# Prereqs: cmake >= 3.22, ninja, a C++17 compiler, wayland-client /
# wayland-cursor / xkbcommon dev headers, wayland-protocols (wayland-scanner),
# a Vulkan loader + headers, and the Slang shader compiler (slangc).
#
# Usage: scripts/linux/build.sh [--debug|--release] [--clean] [cmake args...]
# Passing a mode flag explicitly (scripts, CI) always skips straight to the
# build — same for non-interactive stdin (defaults to Release).
#
# Run with no mode flag on an interactive terminal and one prompt runs:
#   1) Release (default) -- mnrdonw
#   2) Debug               -- mnrdonw, editor_test, and the filesystem tests
#
# Release: the editor only. editor_test is added by the root CMakeLists only
#   when CMAKE_BUILD_TYPE is Debug, and the archive_engine filesystem tests
#   follow the same rule.
# Debug: the editor plus those tests, with debug info and no optimization.
#
# Each type gets its own directory. Ninja is single-config, so one directory
# cannot hold both. --clean removes only that directory. The Makefile tree
# at ./build, from the README's cmake -S . -B build line, is left alone.
set -euo pipefail
cd "$(dirname "$0")/../.."

BUILD_TYPE=Release
CLEAN=0
MODE_SET=0
CMAKE_ARGS=()

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE=Debug; MODE_SET=1 ;;
        --release) BUILD_TYPE=Release; MODE_SET=1 ;;
        --clean)   CLEAN=1 ;;
        *)         CMAKE_ARGS+=("$arg") ;;
    esac
done

# No mode flag given: ask, if there's actually someone at the keyboard to
# answer (stdin a tty) — a non-interactive caller (CI, a pipe) falls through
# to the Release default above instead of hanging on `read`.
if [[ "$MODE_SET" -eq 0 && -t 0 ]]; then
    echo "Select build type:"
    echo "  1) Release (default) -- mnrdonw"
    echo "  2) Debug -- mnrdonw, editor_test, and the filesystem tests"
    read -r -p "Enter choice [1-2, default 1]: " type_choice
    case "$type_choice" in
        ""|1) BUILD_TYPE=Release ;;
        2)    BUILD_TYPE=Debug ;;
        *)    echo "error: invalid choice '$type_choice'" >&2; exit 2 ;;
    esac
fi

# vk_canvas resolves slangc from $VULKAN_SDK/bin/slangc, falling back to a
# hardcoded Windows path if VULKAN_SDK is unset. Point at it explicitly
# unless the caller already passed -DVCE_SLANGC or has VULKAN_SDK set.
SLANGC_ARG=()
if [[ "${CMAKE_ARGS[*]:-}" != *"VCE_SLANGC"* && -z "${VULKAN_SDK:-}" ]]; then
    # Neither Arch package puts slangc on PATH, and they install to DIFFERENT
    # prefixes named after themselves: shader-slang-bin (the upstream
    # precompiled release) lands in /opt/shader-slang-bin, while shader-slang
    # / shader-slang-git (built from source) lands in /opt/shader-slang. Only
    # the first was probed, so a source-built Slang — which is the one you get
    # if you build it yourself — failed the whole build at the first shader
    # with a bare exit 127.
    if command -v slangc >/dev/null 2>&1; then
        SLANGC_ARG=(-DVCE_SLANGC="$(command -v slangc)")
    else
        for _slangc in /opt/shader-slang-bin/bin/slangc \
                       /opt/shader-slang/bin/slangc; do
            if [[ -x "$_slangc" ]]; then
                SLANGC_ARG=(-DVCE_SLANGC="$_slangc")
                break
            fi
        done
    fi
    if [[ ${#SLANGC_ARG[@]} -eq 0 ]]; then
        echo "error: slangc not found. Install shader-slang, set VULKAN_SDK, or pass -DVCE_SLANGC=/path/to/slangc." >&2
        exit 2
    fi
fi

if [[ "$BUILD_TYPE" == "Debug" ]]; then
    BUILD_DIR=build/linux_debug
else
    BUILD_DIR=build/linux
fi

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "Cleaning $BUILD_DIR..."
    rm -rf "$BUILD_DIR"
fi

echo "Configuring CMake (Ninja, $BUILD_TYPE) -> $BUILD_DIR..."
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    "${SLANGC_ARG[@]}" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR"
echo
echo "Binaries in $BUILD_DIR/:"
echo "  mnrdonw -- the editor"
if [[ "$BUILD_TYPE" == "Debug" ]]; then
    echo "  editor_test -- document behavior"
    echo "  archive_engine/fs/paths_test, list_test -- filesystem tests"
fi
