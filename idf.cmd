@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -ExecutionPolicy Bypass -NoProfile -File "%~dp0idf.ps1" %*
exit /b %ERRORLEVEL%
