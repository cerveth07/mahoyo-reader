@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall.ps1" %*
set "study_exit=%ERRORLEVEL%"
pause
exit /b %study_exit%
