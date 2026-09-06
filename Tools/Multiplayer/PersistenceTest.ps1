# ---------------------------------------------------------------------------
#  PersistenceTest.ps1 - reconnect, server restart, and authority.
#
#  Three claims, each checked across process boundaries:
#
#    1. A player who disconnects and reconnects comes back where they were.
#    2. A server that is stopped and started again still has the world.
#    3. A client cannot move impossibly, and the server says so.
#
#  Usage:  powershell -File Tools\Multiplayer\PersistenceTest.ps1
#
#  All three need real processes and real restarts, which is why none of them is
#  a test body in the standalone harness. What makes this a test rather than a
#  demonstration is that it compares specific values between runs: the entity id
#  the first server assigned, and the cell the first server remembered.
# ---------------------------------------------------------------------------

param(
    [int]$Port = 7787
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$LogDir = Join-Path $RepoRoot 'Saved\Logs'
$Tools = Join-Path $RepoRoot 'Tools\Multiplayer'

# Each server process writes its own log rather than rotating one file. The
# previous process can still hold the old file when the next one starts, and a
# harness that dies on a locked file has failed for a reason unrelated to
# anything it was testing.
function Start-Server([string]$Tag, [int]$Seconds) {
    $log = Join-Path $LogDir "Server-$Tag.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

    return Start-Process -FilePath (Join-Path $Tools 'RunServer.bat') `
        -ArgumentList @("$Port", "$Seconds", "Server-$Tag") -PassThru -WindowStyle Hidden
}

# Name and log tag are separate. The name is who the player is - the same across
# a reconnect, which is the whole point - and the tag is which file this run
# writes to. Sharing one lost the first session's log to the second.
function Start-Client([string]$Name, [string]$Tag, [int]$Seconds, [string]$Cmds) {
    $log = Join-Path $LogDir "Client-$Tag.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

    return Start-Process -FilePath (Join-Path $Tools 'RunClient.bat') `
        -ArgumentList @("127.0.0.1:$Port", $Name, "$Seconds", "`"$Cmds`"", $Tag) `
        -PassThru -WindowStyle Hidden
}

# Stopping the launcher does not stop the engine it launched, so processes are
# given a duration and allowed to shut themselves down. Waiting for that is also
# what makes the restart a genuine restart: the database is closed and the
# process is gone before the next one opens it.
function Wait-For-Engines([int]$MaxSeconds) {
    $waited = 0
    while ($waited -lt $MaxSeconds) {
        if ($null -eq (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue)) { return $true }
        Start-Sleep -Seconds 2
        $waited += 2
    }
    return $false
}

function Stop-Everything() {
    Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
}

function Test-Log([string]$Path, [string]$Pattern) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet) -eq $true
}

Write-Host "=== Sprint 007 persistence and authority run ==="
Write-Host ""

Stop-Everything

# ---------------------------------------------------------------------------
# Session one: connect, land, build, attempt an impossible move, leave.
# ---------------------------------------------------------------------------

Write-Host "Session 1: connect, land, build, disconnect ..."

$server1 = Start-Server 'session1' 80
Start-Sleep -Seconds 14

$cmds1 = 'universe.After 18 universe.Land,' +
         'universe.After 28 universe.Build beacon,' +
         'universe.After 34 universe.NetTeleportTest,' +
         'universe.After 40 universe.NetPlayers'

$client1 = Start-Client 'Ada' 'Run1' 45 $cmds1

Write-Host "  Running session 1 ..."
Start-Sleep -Seconds 85

Wait-For-Engines 60 | Out-Null
Stop-Everything

$server1Log = Join-Path $LogDir 'Server-session1.log'
$client1Log = Join-Path $LogDir 'Client-Run1.log'

$builtLine = Select-String -LiteralPath $server1Log -Pattern 'built universe\.structure\S+ at ([0-9A-F]{32})' |
    Select-Object -First 1
$builtId = if ($null -ne $builtLine) { $builtLine.Matches[0].Groups[1].Value } else { $null }

$rememberedLine = Select-String -LiteralPath $server1Log -Pattern 'left; remembered at Cell \[(-?\d+)' |
    Select-Object -First 1
$rememberedCell = if ($null -ne $rememberedLine) { $rememberedLine.Matches[0].Groups[1].Value } else { $null }

$rejected = Test-Log $server1Log 'Rejected move from'

Write-Host "  Built entity      : $builtId"
Write-Host "  Remembered cell X : $rememberedCell"
Write-Host "  Teleport rejected : $rejected"

# ---------------------------------------------------------------------------
# Session two: a NEW server process, and the same player reconnecting.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "Session 2: restart the server, reconnect the same player ..."

$server2 = Start-Server 'session2' 70
Start-Sleep -Seconds 14

# Lands first, so the client subscribes to the region the beacon is in. Without
# that it would be asking about a region nobody has sent it, and "not loaded" is
# not the same answer as "empty".
$cmds2 = 'universe.After 18 universe.Land,' +
         'universe.After 28 universe.PersistenceInfo,' +
         'universe.After 32 universe.NetPlayers'

# The same *name*, which is what makes it the same player.
$client2 = Start-Client 'Ada' 'Run2' 40 $cmds2

Write-Host "  Running session 2 ..."
Start-Sleep -Seconds 65

Wait-For-Engines 60 | Out-Null
Stop-Everything

$server2Log = Join-Path $LogDir 'Server-session2.log'
$client2Log = Join-Path $LogDir 'Client-Run2.log'

# ---------------------------------------------------------------------------
# Verdict
# ---------------------------------------------------------------------------

$results = [ordered]@{}

$results['Session 1 built a structure']    = ($null -ne $builtId)
$results['Server rejected a teleport']     = $rejected
$results['Client saw the correction']      = Test-Log $client1Log 'Server corrected our position'
$results['Server remembered the player']   = ($null -ne $rememberedCell)
$results['New server reopened the world']  = Test-Log $server2Log 'World state open'
$results['Player returned, not respawned'] = Test-Log $server2Log 'returned at'
$results['Structure survived restart']     = if ($null -ne $builtId) { Test-Log $client2Log $builtId } else { $false }

if ($null -ne $rememberedCell) {
    # The reconnecting player came back to the cell they left from, not to the
    # spawn point. Comparing the actual number is the point: "returned" in a log
    # is a claim, and this is the evidence.
    $results['Returned to the same cell'] = Test-Log $server2Log ("returned at Cell \[" + $rememberedCell)
}
else {
    $results['Returned to the same cell'] = $false
}

Write-Host ""
Write-Host "=== Results ==="

$failed = 0

foreach ($k in $results.Keys) {
    $ok = $results[$k]
    if (-not $ok) { $failed++ }
    $mark = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1}" -f $mark, $k)
}

Write-Host ""

if ($failed -eq 0) {
    Write-Host "ALL CHECKS PASSED"
    exit 0
}

Write-Host "$failed CHECK(S) FAILED - see $LogDir"
exit 1
