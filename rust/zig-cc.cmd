@echo off
setlocal
set "PATH=%~dp0;%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
"%~dp0zigcc_filter.exe" cc -target x86_64-windows-gnu -L "%USERPROFILE%\.rustup\toolchains\stable-x86_64-pc-windows-gnu\lib\rustlib\x86_64-pc-windows-gnu\lib\self-contained" -L "%SystemRoot%\System32" %*
exit /b %ERRORLEVEL%
