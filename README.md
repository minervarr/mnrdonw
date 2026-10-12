# mnrdonw

A one-document markdown editor. The caret's block shows the source. The rest of the page is styled prose.

## Build

Debug, from this directory:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build

## Test

    ./build/editor_test

## Run

    ./build/mnrdonw fixtures/sample.md
