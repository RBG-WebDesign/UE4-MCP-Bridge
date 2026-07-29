@echo off
setlocal

set "PROJECT=%~dp0..\..\Sinfeld_Demo.uproject"
set "UE_ROOT="

for /f "tokens=2,*" %%A in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\4.27" /v InstalledDirectory 2^>nul') do set "UE_ROOT=%%B"

if not defined UE_ROOT if defined UE_4_27_ROOT set "UE_ROOT=%UE_4_27_ROOT%"

if not defined UE_ROOT (
    echo Unreal Engine 4.27 was not found in the registry.
    echo Set UE_4_27_ROOT to the engine installation directory and run again.
    exit /b 1
)

if not exist "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
    echo Build.bat was not found under "%UE_ROOT%".
    exit /b 1
)

echo Building MCPBridge through the Sinfeld_DemoEditor target...
call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" Sinfeld_DemoEditor Win64 Development -Project="%PROJECT%" -WaitMutex
exit /b %ERRORLEVEL%
