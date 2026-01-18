@echo off
echo Building Limit Order Book...

REM Check for g++
where g++ >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Error: g++ not found. Please install MinGW or MSYS2.
    echo Download from: https://www.msys2.org/
    pause
    exit /b 1
)

REM Create build directory
if not exist "build" mkdir build

REM Compile
echo Compiling...
g++ -std=c++17 -O3 -Wall -Wextra -Iinclude src/OrderBook.cpp src/main.cpp -o build/orderbook.exe

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Executable: build\orderbook.exe
    echo.
    echo Run with: build\orderbook.exe
) else (
    echo Build failed!
)

pause
