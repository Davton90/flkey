@echo off
REM Build FloatKeys.exe with MSVC (VS Build Tools / VS Community with C++ workload)
REM Run this from a "Developer Command Prompt" so cl.exe is on PATH.
cl /O2 keyboard.c keyboard.rc user32.lib gdi32.lib shcore.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /OUT:FloatKeys.exe
