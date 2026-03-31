#!/usr/bin/env bash
#
# HorizonEngine Build Script (Linux / macOS)
#
# Usage:
#   ./build.sh                     Build editor (RelWithDebInfo)
#   ./build.sh release             Build editor (Release)
#   ./build.sh debug               Build editor (Debug)
#   ./build.sh runtime             Build runtime only (Release)
#   ./build.sh runtime debug       Build runtime only (Debug)
#   ./build.sh configure           Configure only (no build)
#   ./build.sh clean               Remove build directory
#   ./build.sh bootstrap           Run bootstrap to install tools
#
# The script automatically detects tools from:
#   1. Tools/ directory (portable, from bootstrap.sh)
#   2. System PATH

set -euo pipefail

ENGINE_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ENGINE_ROOT/build"
TOOLS_DIR="$ENGINE_ROOT/Tools"
BUILD_CONFIG="RelWithDebInfo"
BUILD_TARGET="HorizonEngine"
CONFIGURE_ONLY=false
CLEAN_BUILD=false
RUN_BOOTSTRAP=false

# ── Parse arguments ───────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        release|Release)         BUILD_CONFIG="Release" ;;
        debug|Debug)             BUILD_CONFIG="Debug" ;;
        relwithdebinfo)          BUILD_CONFIG="RelWithDebInfo" ;;
        runtime)                 BUILD_TARGET="HorizonEngineRuntime" ;;
        editor)                  BUILD_TARGET="HorizonEngine" ;;
        configure)               CONFIGURE_ONLY=true ;;
        clean)                   CLEAN_BUILD=true ;;
        bootstrap)               RUN_BOOTSTRAP=true ;;
        -h|--help)
            head -18 "$0" | tail -14
            exit 0 ;;
        *)
            echo "Unknown argument: $arg" ;;
    esac
done

# ── Bootstrap ─────────────────────────────────────────────────────────────
if [[ "$RUN_BOOTSTRAP" == true ]]; then
    exec bash "$ENGINE_ROOT/tools/bootstrap.sh" "$@"
fi

# ── Clean ─────────────────────────────────────────────────────────────────
if [[ "$CLEAN_BUILD" == true ]]; then
    echo "Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    echo "Done."
    exit 0
fi

# ── Load portable tools environment ───────────────────────────────────────
if [[ -f "$TOOLS_DIR/env.sh" ]]; then
    source "$TOOLS_DIR/env.sh"
fi

# ── Detect CMake ──────────────────────────────────────────────────────────
CMAKE_EXE=""

if [[ -x "$TOOLS_DIR/cmake/bin/cmake" ]]; then
    CMAKE_EXE="$TOOLS_DIR/cmake/bin/cmake"
elif command -v cmake &>/dev/null; then
    CMAKE_EXE="$(command -v cmake)"
fi

if [[ -z "$CMAKE_EXE" ]]; then
    echo ""
    echo "[ERROR] CMake not found!"
    echo "  Run:  ./build.sh bootstrap"
    echo "  Or install CMake from https://cmake.org/download/"
    exit 1
fi

echo "CMake: $CMAKE_EXE"

# ── Detect Generator and Compiler ─────────────────────────────────────────
CMAKE_EXTRA_ARGS=()

# Check for Ninja
NINJA_EXE=""
if [[ -x "$TOOLS_DIR/ninja/ninja" ]]; then
    NINJA_EXE="$TOOLS_DIR/ninja/ninja"
elif command -v ninja &>/dev/null; then
    NINJA_EXE="$(command -v ninja)"
fi

# Check for compilers
CLANG_FOUND=false
GCC_FOUND=false

if command -v clang++ &>/dev/null; then
    CLANG_FOUND=true
fi
if command -v g++ &>/dev/null; then
    GCC_FOUND=true
fi

# Strategy: Ninja if available, prefer Clang over GCC
if [[ -n "$NINJA_EXE" ]]; then
    CMAKE_EXTRA_ARGS+=("-G" "Ninja")
fi

if [[ "$CLANG_FOUND" == true ]]; then
    CMAKE_EXTRA_ARGS+=("-DCMAKE_C_COMPILER=clang" "-DCMAKE_CXX_COMPILER=clang++")
    echo "Compiler: Clang"
elif [[ "$GCC_FOUND" == true ]]; then
    echo "Compiler: GCC"
else
    echo "[WARNING] No C++ compiler found! CMake may fail."
    echo "  Run:  ./build.sh bootstrap"
fi

if [[ -n "$NINJA_EXE" ]]; then
    echo "Generator: Ninja"
fi

# ── Configure ─────────────────────────────────────────────────────────────
echo ""
echo "── CMake Configure ($BUILD_CONFIG) ──"
echo ""

CONFIGURE_CMD=("$CMAKE_EXE" -S "$ENGINE_ROOT" -B "$BUILD_DIR" "${CMAKE_EXTRA_ARGS[@]}")
echo "> ${CONFIGURE_CMD[*]}"
"${CONFIGURE_CMD[@]}"

if [[ "$CONFIGURE_ONLY" == true ]]; then
    echo ""
    echo "Configure complete. Build directory: $BUILD_DIR"
    exit 0
fi

# ── Build ─────────────────────────────────────────────────────────────────
echo ""
echo "── CMake Build ($BUILD_TARGET, $BUILD_CONFIG) ──"
echo ""

BUILD_CMD=("$CMAKE_EXE" --build "$BUILD_DIR" --target "$BUILD_TARGET" --config "$BUILD_CONFIG")
echo "> ${BUILD_CMD[*]}"
"${BUILD_CMD[@]}"

echo ""
echo "══════════════════════════════════════════════════"
echo "  Build successful: $BUILD_TARGET ($BUILD_CONFIG)"
echo "══════════════════════════════════════════════════"
echo ""
