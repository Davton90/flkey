@echo off
setlocal
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%"
zig dlltool %*
