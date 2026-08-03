@echo off
REM ue427 launcher shim. Forwards to the CLI in Scripts/ with this repo as root.
setlocal
where python >nul 2>nul && (set "PY=python") || (set "PY=python3")
"%PY%" "%~dp0Scripts/ue427.py" %*
exit /b %ERRORLEVEL%
