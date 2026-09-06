@echo off
REM ===========================================================================
REM  RunClient.bat - connect a client to a Universe server.
REM
REM  Usage:  RunClient.bat [address] [name] [seconds] [extra ExecCmds]
REM
REM  Examples:
REM      RunClient.bat                              connect to 127.0.0.1:7777
REM      RunClient.bat 127.0.0.1:7777 Ada           named, so the log is readable
REM      RunClient.bat 127.0.0.1:7777 Ada 60        headless, quits after 60 s
REM      RunClient.bat 127.0.0.1:7777 Ada 60 "universe.After 20 universe.NetInfo"
REM
REM  Passing a duration also switches the client to -nullrhi, which is what
REM  makes a two-client test possible on one machine: two rendering clients
REM  contend for the GPU and make every timing meaningless.
REM
REM  Real time throughout, for the reason RunServer.bat explains at length.
REM ===========================================================================
setlocal

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe

set ADDRESS=%1
if "%ADDRESS%"=="" set ADDRESS=127.0.0.1:7777

set NAME=%2
if "%NAME%"=="" set NAME=Player

set SECONDS=%3
set MORE=%~4

set DISPLAY=-windowed -ResX=1280 -ResY=720
set EXEC=

if not "%SECONDS%"=="" (
    set DISPLAY=-nullrhi -unattended -nopause
    if "%MORE%"=="" (
        set EXEC=-ExecCmds="universe.After %SECONDS% quit"
    ) else (
        set EXEC=-ExecCmds="%MORE%,universe.After %SECONDS% quit"
    )
)

echo Connecting %NAME% to %ADDRESS% ...
echo   Log: %REPO_ROOT%\Saved\Logs\Client-%NAME%.log

"%UE%" "%REPO_ROOT%\Universe.uproject" %ADDRESS% ^
    -game ^
    -log ^
    -nosplash ^
    -PlayerName=%NAME% ^
    %DISPLAY% ^
    %EXEC% ^
    -ABSLOG="%REPO_ROOT%\Saved\Logs\Client-%NAME%.log"

endlocal
