@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0test.ps1" %*
exit /b %ERRORLEVEL%
