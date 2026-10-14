# mnrdonw

A one-document markdown editor. The caret's block shows the source. The rest of the page is styled prose.

## Build

Debug, from this directory:

    scripts/linux/build.sh --debug

That writes `build/linux_debug/`. Release is `scripts/linux/build.sh --release` and writes `build/linux/`. Pass `--clean` to wipe that directory first. Extra arguments are forwarded to CMake.

The same Debug build by hand:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build

## Test

    ./build/linux_debug/editor_test

A hand build into `build/` leaves the same binary at `./build/editor_test`.

## Run

    ./build/linux_debug/mnrdonw fixtures/sample.md
