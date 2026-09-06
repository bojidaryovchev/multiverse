@echo off
REM ===========================================================================
REM  RunServer.bat - start the Universe dedicated server.
REM
REM  Usage:  RunServer.bat [port] [seconds] [logtag]
REM
REM  The log tag exists so that two server *processes* in one test run write to
REM  two files. Rotating one file between them does not work: the previous
REM  process may still hold it, and a harness that fails on a locked file is a
REM  harness that fails for a reason having nothing to do with what it tests.
REM
REM  Runs headless: no rendering, no viewport, no window worth looking at. The
REM  log is the interface. With a duration it shuts itself down afterwards,
REM  which is what makes an unattended two-client test possible.
REM
REM
REM  WHY REAL TIME AND NOT -benchmark
REM
REM  Every other automated run in this project uses -benchmark -fps=30, which
REM  advances a fixed 1/30 s per frame as fast as the machine will go - two
REM  minutes of game time in four seconds of wall time. That is exactly right
REM  for a single-process test and exactly wrong here: the network runs in real
REM  time regardless, so an accelerated server and a real-time socket disagree
REM  about how long a second is, and every timeout in the stack starts firing.
REM
REM  So the duration below is wall-clock, delivered by universe.After, which
REM  counts game seconds - and in real time those are the same thing.
REM
REM
REM  WHY THE EDITOR BINARY AND NOT A SERVER TARGET
REM
REM  Source/UniverseServer.Target.cs is a correct dedicated-server target and it
REM  is what a source-built engine should use. It cannot be compiled here:
REM  UnrealBuildTool refuses with "Server targets are not currently supported
REM  from this engine distribution" because the Epic Games Launcher build of
REM  UE 5.8 ships no server-configuration engine libraries.
REM
REM  UnrealEditor.exe -server runs a genuine dedicated-server world - net mode
REM  NM_DedicatedServer, no local player, clients connecting over the net driver
REM  - from a binary this installation does have. Same code, same authority
REM  model, same net mode; only the executable differs.
REM
REM  On a source engine, replace the launch line with:
REM      Binaries\Win64\UniverseServer.exe Universe.uproject -log -Port=%PORT%
REM ===========================================================================
setlocal

set SCRIPT_DIR=%~dp0
set REPO_ROOT=%SCRIPT_DIR%..\..
set UE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe

set PORT=%1
if "%PORT%"=="" set PORT=7777

set SECONDS=%2

set LOGTAG=%3
if "%LOGTAG%"=="" set LOGTAG=Server

set EXEC=
if not "%SECONDS%"=="" set EXEC=-ExecCmds="universe.After %SECONDS% quit"

echo Starting the Universe dedicated server on port %PORT% ...
echo   Log: %REPO_ROOT%\Saved\Logs\%LOGTAG%.log

"%UE%" "%REPO_ROOT%\Universe.uproject" /Engine/Maps/Entry ^
    -server ^
    -log ^
    -unattended ^
    -nopause ^
    -nosplash ^
    -nullrhi ^
    -Port=%PORT% ^
    %EXEC% ^
    -ABSLOG="%REPO_ROOT%\Saved\Logs\%LOGTAG%.log"

endlocal
