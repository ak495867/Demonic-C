@echo off
set ROOT=%~dp0..
if exist "%ROOT%\bin\dmc-native.exe" (
  "%ROOT%\bin\dmc-native.exe" %*
) else (
  "%ROOT%\bin\dmc.exe" %*
)
