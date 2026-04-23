#!/usr/bin/env bash
#
# HorizonEngine Build Environment Bootstrap (Linux / macOS)
# Downloads and configures CMake, Clang, and Ninja
# into a portable Tools/ directory so the engine can be built
# without manual setup.
#
# Usage:
#   ./tools/bootstrap.sh                  # auto-detect / install
#   ./tools/bootstrap.sh --compiler clang # prefer clang
#   ./tools/bootstrap.sh --compiler gcc   # prefer gcc
#   ./tools/bootstrap.sh --force          # re-download everything

set -euo pipefail

# ── Configuration ─────────────────────────────────────────────────────────
CMAKE_VERSION="3.31.4"
NINJA_VERSION="1.12.1"
COMPILER_PREF="auto"   # auto | clang | gcc
FORCE=false

# ── Parse arguments ───────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --compiler) COMPILER_PREF="$2"; shift 2 ;;
        --force)    FORCE=true; shift ;;
        --help|-h)
            echo "Usage: $0 [--compiler auto|clang|gcc] [--force]"
            exit 0 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# ── Paths ─────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ENGINE_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS_DIR="$ENGINE_ROOT/Tools"
TEMP_DIR="$TOOLS_DIR/_temp"
CMAKE_DIR="$TOOLS_DIR/cmake"
NINJA_DIR="$TOOLS_DIR/ninja"

# ── Detect OS ─────────────────────────────────────────────────────────────
OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS" in
    Linux)  PLATFORM="linux" ;;
    Darwin) PLATFORM="macos" ;;
    *)      echo "Unsupported OS: $OS"; exit 1 ;;
esac

# ── Helpers ───────────────────────────────────────────────────────────────
step()  { echo -e "\n\033[36m==> $1\033[0m"; }
ok()    { echo -e "    \033[32m[OK]\033[0m $1"; }
skip()  { echo -e "    \033[33m[SKIP]\033[0m $1"; }
err()   { echo -e "    \033[31m[ERROR]\033[0m $1"; }

ensure_dir() { mkdir -p "$1"; }

download() {
    local url="$1" dest="$2"
    echo "    Downloading: $url"
    if command -v curl &>/dev/null; then
        curl -fSL --progress-bar -o "$dest" "$url"
    elif command -v wget &>/dev/null; then
        wget -q --show-progress -O "$dest" "$url"
    else
        err "Neither curl nor wget found!"
        exit 1
    fi
}

command_exists() { command -v "$1" &>/dev/null; }

# ── Main ──────────────────────────────────────────────────────────────────
echo ""
echo "============================================="
echo "  HorizonEngine Build Environment Bootstrap"
echo "============================================="
echo "  Engine Root : $ENGINE_ROOT"
echo "  Tools Dir   : $TOOLS_DIR"
echo "  Platform    : $PLATFORM ($ARCH)"
echo "  Compiler    : $COMPILER_PREF"
echo ""

ensure_dir "$TOOLS_DIR"
ensure_dir "$TEMP_DIR"

# ── 1. CMake ──────────────────────────────────────────────────────────────
step "Checking CMake..."

CMAKE_EXE="$CMAKE_DIR/bin/cmake"
CMAKE_FOUND=false

if [[ "$FORCE" != true ]] && [[ -x "$CMAKE_EXE" ]]; then
    ver=$("$CMAKE_EXE" --version 2>/dev/null | head -1)
    ok "Bundled CMake: $ver"
    CMAKE_FOUND=true
elif [[ "$FORCE" != true ]] && command_exists cmake; then
    ver=$(cmake --version 2>/dev/null | head -1)
    CMAKE_EXE="$(command -v cmake)"
    ok "System CMake: $ver ($CMAKE_EXE)"
    CMAKE_FOUND=true
fi

if [[ "$CMAKE_FOUND" != true ]] || [[ "$FORCE" == true ]]; then
    if [[ "$PLATFORM" == "linux" ]]; then
        CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-${ARCH}.tar.gz"
        ARCHIVE="$TEMP_DIR/cmake.tar.gz"
    else
        CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-macos-universal.tar.gz"
        ARCHIVE="$TEMP_DIR/cmake.tar.gz"
    fi
    
    download "$CMAKE_URL" "$ARCHIVE"
    
    rm -rf "$CMAKE_DIR"
    ensure_dir "$CMAKE_DIR"
    echo "    Extracting..."
    tar -xzf "$ARCHIVE" -C "$TEMP_DIR"
    
    # Move contents from extracted dir
    EXTRACTED=$(find "$TEMP_DIR" -maxdepth 1 -name "cmake-${CMAKE_VERSION}*" -type d | head -1)
    if [[ "$PLATFORM" == "macos" ]]; then
        # macOS tar.gz contains CMake.app/Contents/
        APP_DIR="$EXTRACTED/CMake.app/Contents"
        if [[ -d "$APP_DIR" ]]; then
            cp -r "$APP_DIR/bin" "$CMAKE_DIR/"
            cp -r "$APP_DIR/share" "$CMAKE_DIR/"
        else
            cp -r "$EXTRACTED"/* "$CMAKE_DIR/"
        fi
    else
        cp -r "$EXTRACTED"/* "$CMAKE_DIR/"
    fi
    
    CMAKE_EXE="$CMAKE_DIR/bin/cmake"
    if [[ -x "$CMAKE_EXE" ]]; then
        ok "CMake installed: $("$CMAKE_EXE" --version 2>/dev/null | head -1)"
    else
        err "CMake installation failed!"
        exit 1
    fi
fi

# ── 2. Ninja ──────────────────────────────────────────────────────────────
step "Checking Ninja..."

NINJA_EXE="$NINJA_DIR/ninja"
NINJA_FOUND=false

if [[ "$FORCE" != true ]] && [[ -x "$NINJA_EXE" ]]; then
    ok "Bundled Ninja: $("$NINJA_EXE" --version 2>/dev/null)"
    NINJA_FOUND=true
elif [[ "$FORCE" != true ]] && command_exists ninja; then
    ok "System Ninja: $(ninja --version 2>/dev/null)"
    NINJA_EXE="$(command -v ninja)"
    NINJA_FOUND=true
fi

if [[ "$NINJA_FOUND" != true ]] || [[ "$FORCE" == true ]]; then
    if [[ "$PLATFORM" == "linux" ]]; then
        NINJA_URL="https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip"
    else
        NINJA_URL="https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-mac.zip"
    fi
    
    ARCHIVE="$TEMP_DIR/ninja.zip"
    download "$NINJA_URL" "$ARCHIVE"
    
    rm -rf "$NINJA_DIR"
    ensure_dir "$NINJA_DIR"
    unzip -o "$ARCHIVE" -d "$NINJA_DIR" > /dev/null
    chmod +x "$NINJA_DIR/ninja"
    
    NINJA_EXE="$NINJA_DIR/ninja"
    if [[ -x "$NINJA_EXE" ]]; then
        ok "Ninja installed: $("$NINJA_EXE" --version 2>/dev/null)"
    else
        err "Ninja installation failed!"
        exit 1
    fi
fi

# ── 3. C++ Compiler ──────────────────────────────────────────────────────
step "Checking C++ Compiler..."

COMPILER_CMD=""
COMPILER_NAME=""

if [[ "$COMPILER_PREF" == "auto" ]] || [[ "$COMPILER_PREF" == "clang" ]]; then
    if command_exists clang++; then
        COMPILER_CMD="$(command -v clang++)"
        COMPILER_NAME="Clang"
        ver=$(clang++ --version 2>/dev/null | head -1)
        ok "Clang found: $ver"
    fi
fi

if [[ -z "$COMPILER_CMD" ]] && { [[ "$COMPILER_PREF" == "auto" ]] || [[ "$COMPILER_PREF" == "gcc" ]]; }; then
    if command_exists g++; then
        COMPILER_CMD="$(command -v g++)"
        COMPILER_NAME="GCC"
        ver=$(g++ --version 2>/dev/null | head -1)
        ok "GCC found: $ver"
    fi
fi

if [[ -z "$COMPILER_CMD" ]]; then
    err "No C++ compiler found!"
    echo ""
    if [[ "$PLATFORM" == "linux" ]]; then
        echo "    Install Clang:  sudo apt install clang"
        echo "    Install GCC:    sudo apt install g++"
    else
        echo "    Install Xcode Command Line Tools:  xcode-select --install"
    fi
    echo ""
    exit 1
fi

# ── 4. OpenGL / Development Libraries
step "Checking OpenGL development libraries..."

if [[ "$PLATFORM" == "linux" ]]; then
    if pkg-config --exists gl 2>/dev/null || [[ -f /usr/include/GL/gl.h ]]; then
        ok "OpenGL headers found."
    else
        echo "    [WARNING] OpenGL headers not found!"
        echo "    Install:  sudo apt install libgl-dev mesa-common-dev"
    fi
    
    if pkg-config --exists x11 2>/dev/null || [[ -f /usr/include/X11/Xlib.h ]]; then
        ok "X11 headers found."
    else
        echo "    [WARNING] X11 headers may be needed."
        echo "    Install:  sudo apt install libx11-dev"
    fi
else
    ok "macOS includes OpenGL framework."
fi

# ── 5. Clean up
step "Cleaning up..."
rm -rf "$TEMP_DIR"
ok "Temp files removed."

# ── 6. Generate env script
step "Generating environment setup..."

cat > "$TOOLS_DIR/env.sh" << 'ENVEOF'
#!/usr/bin/env bash
# HorizonEngine Build Environment
# Source this script: source ./Tools/env.sh

TOOLS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

[[ -d "$TOOLS_ROOT/cmake/bin" ]] && export PATH="$TOOLS_ROOT/cmake/bin:$PATH"
[[ -d "$TOOLS_ROOT/ninja" ]]     && export PATH="$TOOLS_ROOT/ninja:$PATH"

echo "HorizonEngine build environment loaded."
ENVEOF
chmod +x "$TOOLS_DIR/env.sh"
ok "Generated Tools/env.sh"

# ── 7. Summary
echo ""
echo -e "\033[32m=============================================\033[0m"
echo -e "\033[32m  Bootstrap Complete!\033[0m"
echo -e "\033[32m=============================================\033[0m"
echo ""
echo "  Tools directory: $TOOLS_DIR"
[[ -x "$CMAKE_DIR/bin/cmake" ]] && echo "  CMake:    $("$CMAKE_DIR/bin/cmake" --version 2>/dev/null | head -1)"
[[ -x "$NINJA_DIR/ninja" ]]     && echo "  Ninja:    $("$NINJA_DIR/ninja" --version 2>/dev/null)"
echo "  Compiler: $COMPILER_NAME ($COMPILER_CMD)"
echo ""
echo "  To build the engine:"
echo "    ./build.sh"
echo ""
echo "  Or manually:"
echo "    source ./Tools/env.sh"

if [[ "$COMPILER_NAME" == "Clang" ]]; then
    echo "    cmake -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -B build -S ."
else
    echo "    cmake -G Ninja -B build -S ."
fi
echo "    cmake --build build --config Release"
echo ""
