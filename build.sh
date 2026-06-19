#!/bin/bash

BUILD_DIR="build"

print_usage() {
    echo "===================================================="
    echo "       HFT Cross-Platform Build Automation Suite"
    echo "===================================================="
    echo "Usage: ./build.sh [command]"
    echo ""
    echo "Commands:"
    echo "  init            Configure the build system using platform presets"
    echo "  all             Compile all libraries and applications"
    echo "  CmeDecoder      Compile and launch the CME market data handler"
    echo "  test            Compile and run the offline test suite"
    echo "  clean           Completely wipe the build cache folder"
    echo "===================================================="
}

cd "$(dirname "$0")"

ensure_init() {
    if [ ! -d "$BUILD_DIR" ]; then
        echo "--> Build folder missing. Initializing system configurations first..."
        if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
            cmake --preset windows-msvc
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            # Inject Homebrew GCC variables safely for specialized Mac development environments
            CC="/opt/homebrew/bin/gcc-15" CXX="/opt/homebrew/bin/g++-15" cmake --preset default-gcc
        else
            # Linux falls back seamlessly to standard system toolchains
            cmake --preset default-gcc
        fi
    fi
}

case "$1" in
    init)
        echo "--> Detecting system architecture and initializing build engine..."
        rm -rf "$BUILD_DIR"
        if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
            cmake --preset windows-msvc
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            CC="/opt/homebrew/bin/gcc-15" CXX="/opt/homebrew/bin/g++-15" cmake --preset default-gcc
        else
            cmake --preset default-gcc
        fi
        ;;
    all)
        ensure_init
        echo "--> Compiling all HFT targets simultaneously..."
        cmake --build "$BUILD_DIR" --target all
        ;;
    CmeDecoder)
        ensure_init
        cmake --build "$BUILD_DIR" --target CmeDecoder
        if [ $? -eq 0 ]; then
            if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
                ./"$BUILD_DIR"/apps/CmeDecoder/Release/CmeDecoder.exe
            else
                ./"$BUILD_DIR"/apps/CmeDecoder/CmeDecoder
            fi
        fi
        ;;
    test)
        ensure_init
        echo "--> Compiling and running unit tests..."
        # 1. Compile the test executable target defined in tests/CMakeLists.txt
        cmake --build "$BUILD_DIR" --target unit_tests
        
        # 2. Run the tests using CTest directly via --test-dir if compilation succeeded
        if [ $? -eq 0 ]; then
            ctest --test-dir "$BUILD_DIR" --output-on-failure
        fi
        ;;
    clean)
        echo "--> Purging build cache..."
        rm -rf "$BUILD_DIR"
        echo "Clean operation completed successfully."
        ;;
    *)
        print_usage
        exit 1
        ;;
esac
