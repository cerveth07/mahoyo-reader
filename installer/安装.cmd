@echo off
setlocal
if /I "%~x1"==".zip" goto dictionary
if /I "%~x1"==".tsv" goto dictionary
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
goto done
:dictionary
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" -DictionaryPack "%~f1"
:done
set "study_exit=%ERRORLEVEL%"
pause
exit /b %study_exit%
