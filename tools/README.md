# HorizonEngine – Build Tools

Dieses Verzeichnis enthält Skripte zum automatischen Einrichten der Build-Umgebung.

## Schnellstart

### Windows (PowerShell)
```powershell
# Build-Tools automatisch installieren (CMake, Ninja, Compiler, Python)
.\tools\bootstrap.ps1

# Engine bauen
.\build.bat
```

### Windows (CMD)
```cmd
build.bat bootstrap
build.bat
```

### Linux / macOS
```bash
./tools/bootstrap.sh
./build.sh
```

## Bootstrap-Skript

Das Bootstrap-Skript erkennt vorhandene Tools und installiert nur fehlende:

| Tool | Windows | Linux/macOS |
|------|---------|-------------|
| **CMake** | Portables ZIP nach `Tools/cmake/` | Portables tar.gz nach `Tools/cmake/` |
| **Ninja** | Portables ZIP nach `Tools/ninja/` | Portables ZIP nach `Tools/ninja/` |
| **Compiler** | MSVC (wenn VS vorhanden) oder LLVM/Clang (silent install nach `Tools/llvm/`) | System-Clang oder GCC |
| **Python** | Installer nach `Tools/python/` (mit Dev-Headers) | System-Python (manuell installieren) |

### Parameter (Windows – bootstrap.ps1)

| Parameter | Beschreibung | Standard |
|-----------|-------------|----------|
| `-Compiler auto` | MSVC wenn vorhanden, sonst Clang | `auto` |
| `-Compiler clang` | Immer Clang installieren | – |
| `-Compiler msvc` | MSVC Build Tools installieren wenn nicht vorhanden | – |
| `-SkipPython` | Python-Installation überspringen | – |
| `-Force` | Alle Tools neu herunterladen | – |

### Parameter (Linux/macOS – bootstrap.sh)

| Parameter | Beschreibung | Standard |
|-----------|-------------|----------|
| `--compiler auto` | Clang bevorzugt, dann GCC | `auto` |
| `--compiler clang` | Nur Clang akzeptieren | – |
| `--compiler gcc` | Nur GCC akzeptieren | – |
| `--skip-python` | Python-Check überspringen | – |
| `--force` | Alle Tools neu herunterladen | – |

## Build-Skript

### build.bat / build.sh

| Befehl | Beschreibung |
|--------|-------------|
| `build.bat` | Editor bauen (RelWithDebInfo) |
| `build.bat release` | Editor bauen (Release) |
| `build.bat debug` | Editor bauen (Debug) |
| `build.bat runtime` | Runtime bauen (Release) |
| `build.bat runtime debug` | Runtime bauen (Debug) |
| `build.bat configure` | Nur CMake Configure |
| `build.bat clean` | Build-Verzeichnis löschen |
| `build.bat bootstrap` | Bootstrap ausführen |

Die gleichen Befehle funktionieren mit `./build.sh` auf Linux/macOS.

## Verzeichnisstruktur nach Bootstrap

```
Tools/
├── cmake/          CMake (portabel)
│   └── bin/
│       └── cmake.exe
├── ninja/          Ninja Build-System
│   └── ninja.exe
├── llvm/           LLVM/Clang (nur wenn kein MSVC)
│   └── bin/
│       ├── clang.exe
│       └── clang++.exe
├── python/         Python mit Dev-Headers
│   ├── python.exe
│   ├── include/
│   └── libs/
├── env.bat         Umgebungsvariablen (CMD)
├── env.ps1         Umgebungsvariablen (PowerShell)
└── env.sh          Umgebungsvariablen (Bash)
```

## Abhängigkeiten

Die Engine benötigt zum Bauen:

| Abhängigkeit | Mindestversion | Bootstrap? | Anmerkung |
|-------------|---------------|------------|-----------|
| C++20-Compiler | MSVC 19.30+ / Clang 16+ / GCC 13+ | ✅ | MSVC oder Clang auf Windows |
| CMake | 3.12 | ✅ | Portables ZIP |
| Python 3 (+ Dev-Headers) | 3.10+ | ✅ | Braucht `include/` + `libs/` |
| Ninja (optional) | 1.10+ | ✅ | Beschleunigt Build deutlich |
| Windows SDK | 10.0.19041+ | ✅ | Via winget, SDK-Installer oder VS |
| OpenGL | – | ❌ | GPU-Treiber muss installiert sein |

Alle anderen Abhängigkeiten (SDL3, Assimp, FreeType, OpenAL, Jolt, PhysX) sind als Git-Submodules eingebunden und werden automatisch von CMake mitgebaut.

## Was das Bootstrap genau herunterlädt (Windows)

| Datei | Quelle | Größe | Silent-Install |
|-------|--------|-------|---------------|
| `cmake-3.31.4-windows-x86_64.zip` | github.com/Kitware/CMake | ~50 MB | ZIP entpacken |
| `ninja-win.zip` | github.com/ninja-build/ninja | ~300 KB | ZIP entpacken |
| `LLVM-19.1.6-win64.exe` | github.com/llvm/llvm-project | ~400 MB | `/S /D=Tools\llvm` |
| `python-3.13.1-amd64.exe` | python.org | ~25 MB | `/quiet TargetDir=Tools\python` |
| `winsdksetup.exe` (nur bei fehlendem SDK) | go.microsoft.com | ~2 MB (+1.5 GB Download) | `/quiet /features OptionId.DesktopCPPx64` |
| `vs_BuildTools.exe` (nur `-Compiler msvc`) | aka.ms/vs/17/release | ~2 MB (+3-5 GB Download) | `--quiet --wait --add VCTools` |

### Drei Szenarien

**Szenario A: Visual Studio ist schon installiert** (~0 Downloads)
```
bootstrap.ps1 erkennt MSVC → installiert nur CMake + Ninja + Python (falls fehlend)
```

**Szenario B: Kein Visual Studio, Clang gewünscht** (~500 MB Downloads)
```
bootstrap.ps1 → CMake (50 MB) + Ninja (0.3 MB) + Clang (400 MB) + Python (25 MB) + Windows SDK (1.5 GB)
```

**Szenario C: Kein Visual Studio, MSVC gewünscht** (~3-5 GB Download)
```
bootstrap.ps1 -Compiler msvc → VS Build Tools mit C++ + Windows SDK (alles in einem)
```

## Compiler-Empfehlung

| Szenario | Empfehlung |
|----------|-----------|
| **Entwicklung (Windows)** | MSVC (Visual Studio) – bester Debugger |
| **CI / Automatisierung** | Clang + Ninja – schnellste Kompilierung |
| **Portabel (ohne VS)** | Clang + Ninja via Bootstrap |
| **Linux** | Clang oder GCC – beide voll unterstützt |
| **macOS** | Clang (Xcode Command Line Tools) |
