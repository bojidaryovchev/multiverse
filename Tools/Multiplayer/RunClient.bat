@echo off
REM ===========================================================================
REM  RunClient.bat - connect a client to a Universe server.
REM
REM  Usage:  RunClient.bat [address] [name] [seconds] [extra ExecCmds] [logtag]
REM
REM  The log tag is separate from the name because they answer different
REM  questions. The name is *who the player is* and has to be the same across a
REM  reconnect for the server to recognise them; the log tag is which file this
REM  particular run writes to, and two runs by the same player must not
REM  overwrite each other - which is how the first reconnect test lost the very
REM  evidence it was gathering.
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

set LOGTAG=%5
if "%LOGTAG%"=="" set LOGTAG=%NAME%

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
echo   Log: %REPO_ROOT%\Saved\Logs\Client-%LOGTAG%.log

REM  The name is passed as a URL option, not as -PlayerName.
REM
REM  -PlayerName is not an Unreal command-line switch. It is silently ignored,
REM  and the player ends up with a machine-generated name like
REM  "blizz-A8C9B75B425288" that differs on every run - so every reconnect was a
REM  different person, and the first reconnect test failed for a reason that had
REM  nothing to do with reconnecting.
REM
REM  "?Name=" alone is not enough either: the engine rewrites it with the local
REM  player nickname on the way out, so the server receives the generated name
REM  rather than the chosen one. "?PlayerId=" is a key nothing in the engine
REM  claims, so it arrives exactly as sent. Both are passed - Name for display,
REM  PlayerId for identity - because they genuinely are different things.
"%UE%" "%REPO_ROOT%\Universe.uproject" %ADDRESS%?Name=%NAME%?PlayerId=%NAME% ^
    -game ^
    -log ^
    -nosplash ^
    %DISPLAY% ^
    %EXEC% ^
    -ABSLOG="%REPO_ROOT%\Saved\Logs\Client-%LOGTAG%.log"

endlocal
