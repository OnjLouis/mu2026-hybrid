@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-VoiceBanks.ps1" %*
set "result=%errorlevel%"
exit /b %result%
