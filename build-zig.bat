@echo off
REM Build FloatKeys.exe with Zig (no admin, no Visual Studio needed)
REM Install Zig once:  winget install -e --id zig.zig
setlocal
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
where zig >nul 2>nul || (echo Zig not found. Run: winget install -e --id zig.zig & exit /b 1)
zig cc -O2 -o FloatKeys.exe keyboard.c keyboard.rc -luser32 -lgdi32 -lshcore -ladvapi32 "-Wl,--subsystem,windows"
if exist FloatKeys.exe (echo OK: FloatKeys.exe) else (echo BUILD FAILED & exit /b 1)
