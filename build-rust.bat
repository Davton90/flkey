@echo off
setlocal
set "ROOT=%~dp0"
set "RUST=%ROOT%rust"
set "PATH=%RUST%;%LOCALAPPDATA%\Microsoft\WinGet\Links;%USERPROFILE%\.rustup\toolchains\stable-x86_64-pc-windows-gnu\lib\rustlib\x86_64-pc-windows-gnu\bin;%PATH%"
set "ZIG_EXE=%LOCALAPPDATA%\Microsoft\WinGet\Links\zig.exe"
cd /d "%RUST%"
cargo build --release
if errorlevel 1 exit /b 1
copy /y "%RUST%\target\x86_64-pc-windows-gnu\release\FloatKeyrusts.exe" "%ROOT%FloatKeyrusts.exe" >nul
echo Built: %ROOT%FloatKeyrusts.exe
