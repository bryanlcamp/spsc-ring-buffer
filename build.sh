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
    echo "  CmeExample      Compile and launch the CME market data handler"
    echo "  DropCopyLogger  Compile and launch the asynchronous execution logger"
    echo "  clean           Completely wipe the build cache folder"
    echo "===================================================="
}

cd "$(dirname "$0")"

ensure_init() {
    if [ ! -d "$BUILD_DIR" ]; then
        echo "--> Build folder missing. Initializing system configurations first..."
        if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
            cmake --preset windows-msvc
        else
            cmake --preset default-gcc -DCMAKE_CXX_FLAGS="-Wno-interference-size"
        fi
    fi
}

case "$1" in
    init)
        echo "--> Detecting system architecture and initializing build engine..."
        rm -rf "$BUILD_DIR"
        if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
            cmake --preset windows-msvc
        else
            cmake --preset default-gcc -DCMAKE_CXX_FLAGS="-Wno-interference-size"
        fi
        ;;
    all)
        ensure_init
        echo "--> Compiling all HFT targets simultaneously..."
        cmake --build "$BUILD_DIR" --target all
        ;;
    CmeExample)
        ensure_init
        cmake --build "$BUILD_DIR" --target CmeExample
        if [ $? -eq 0 ]; then
            if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
                ./"$BUILD_DIR"/apps/CmeExample/Release/CmeExample.exe
            else
                ./"$BUILD_DIR"/apps/CmeExample/CmeExample
            fi
        fi
        ;;
    DropCopyLogger)
        ensure_init
        cmake --build "$BUILD_DIR" --target DropCopyLogger
        if [ $? -eq 0 ]; then
            if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
                ./"$BUILD_DIR"/apps/Release/DropCopyLogger.exe
            else
                ./"$BUILD_DIR"/apps/DropCopyLogger/DropCopyLogger
            fi
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