# Installation Guide for Windows

## Prerequisites

You need a C++ compiler and CMake. Here are the easiest ways to install them:

### Method 1: Using Winget (Windows 10+)

Open PowerShell and run:

```powershell
# Install CMake
winget install Kitware.CMake

# Install MSYS2 (includes MinGW/g++)
winget install MSYS2.MSYS2
```

After MSYS2 installs:
1. Open "MSYS2 MSYS" from Start Menu
2. Run: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake`
3. Add `C:\msys64\mingw64\bin` to your system PATH

### Method 2: Download Installers

1. **CMake**: Download from https://cmake.org/download/
2. **MinGW**: Download MSYS2 from https://www.msys2.org/

### Method 3: Visual Studio

Install "Visual Studio Community" with "Desktop development with C++" workload:
https://visualstudio.microsoft.com/downloads/

## Building the Project

### With CMake:

```bash
mkdir build
cd build
cmake ..
cmake --build .
.\orderbook.exe
```

### Without CMake (Direct Compilation):

```bash
# Just run the build script
build.bat
```

Or manually:
```bash
g++ -std=c++17 -O3 -Iinclude src/OrderBook.cpp src/main.cpp -o orderbook.exe
```

## Verifying Installation

Check if tools are installed:

```powershell
cmake --version
g++ --version
```

Both should return version information if installed correctly.
