@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -NoProfile -File "%~dp0idf-run.ps1" -C "%~dp0.." menuconfig %*
exit /b %ERRORLEVEL%
