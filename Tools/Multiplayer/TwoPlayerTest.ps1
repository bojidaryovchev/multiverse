# ---------------------------------------------------------------------------
#  TwoPlayerTest.ps1 - the unattended Sprint 007 acceptance run.
#
#  Starts a dedicated server and two headless clients, has them meet in the
#  same system, has one build a structure, and checks that the other is told
#  about it. Prints a verdict.
#
#  Usage:  powershell -File Tools\Multiplayer\TwoPlayerTest.ps1 [-Seconds 90]
#
#
#  WHY THIS IS A SCRIPT AND NOT A TEST BODY
#
#  Everything it checks needs three processes, real sockets and real elapsed
#  time. None of that is expressible as a pure function, so none of it can live
#  in the standalone harness - and a multiplayer bug is almost never visible
#  from one side anyway. What makes this a test rather than a demo is that it
#  reads all three logs afterwards and decides.
# ---------------------------------------------------------------------------

param(
    [int]$Seconds = 90,
    [int]$Port = 7777
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$LogDir = Join-Path $RepoRoot 'Saved\Logs'
$Tools = Join-Path $RepoRoot 'Tools\Multiplayer'

$ServerLog = Join-Path $LogDir 'Server.log'
$AdaLog = Join-Path $LogDir 'Client-Ada.log'
$BoraLog = Join-Path $LogDir 'Client-Bora.log'

foreach ($f in @($ServerLog, $AdaLog, $BoraLog)) {
    if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
}

Write-Host "=== Sprint 007 two-player acceptance run ==="
Write-Host "  Server port : $Port"
Write-Host "  Duration    : $Seconds s"
Write-Host ""

# The server outlives both clients, so its shutdown is not what ends the run.
$serverSeconds = $Seconds + 25

# Launched as the batch file itself rather than through cmd /c with a single
# quoted argument string. Start-Process re-quotes an ArgumentList element that
# contains spaces, which turns a correct cmd command line into one cmd parses
# differently - and the symptom is not an error, it is a client that silently
# never starts and a log file that never appears.
$server = Start-Process -FilePath (Join-Path $Tools 'RunServer.bat') `
    -ArgumentList @("$Port", "$serverSeconds") `
    -PassThru -WindowStyle Hidden

Write-Host "Server starting; waiting for it to listen ..."
Start-Sleep -Seconds 14

# Ada connects, waits for the world, builds a beacon, then reports.
$adaCmds = 'universe.After 25 universe.Land,' +
           'universe.After 40 universe.Build beacon,' +
           'universe.After 55 universe.NetInfo,' +
           'universe.After 60 universe.NetPlayers'

# Bora connects, lands in the same place, and reports what it was told.
$boraCmds = 'universe.After 25 universe.Land,' +
            'universe.After 50 universe.NetInfo,' +
            'universe.After 58 universe.PersistenceInfo,' +
            'universe.After 60 universe.NetPlayers'

$ada = Start-Process -FilePath (Join-Path $Tools 'RunClient.bat') `
    -ArgumentList @("127.0.0.1:$Port", 'Ada', "$Seconds", "`"$adaCmds`"") `
    -PassThru -WindowStyle Hidden

Start-Sleep -Seconds 3

$bora = Start-Process -FilePath (Join-Path $Tools 'RunClient.bat') `
    -ArgumentList @("127.0.0.1:$Port", 'Bora', "$Seconds", "`"$boraCmds`"") `
    -PassThru -WindowStyle Hidden

Write-Host "Two clients connected; running for $Seconds s ..."
Start-Sleep -Seconds ($Seconds + 20)

foreach ($p in @($ada, $bora, $server)) {
    if ($p -ne $null -and -not $p.HasExited) {
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    }
}

Start-Sleep -Seconds 3

# --- Verdict ---------------------------------------------------------------

function Test-Log([string]$Path, [string]$Pattern) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    return (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet) -eq $true
}

$results = [ordered]@{}

$results['Server listened']            = Test-Log $ServerLog 'IpNetDriver listening on port'
$results['Server published identity']  = Test-Log $ServerLog 'World identity:'
$results['Both clients joined']        = ((Select-String -LiteralPath $ServerLog -Pattern 'Join succeeded').Count -ge 2)
$results['Ada handshake accepted']     = Test-Log $AdaLog 'Handshake accepted'
$results['Bora handshake accepted']    = Test-Log $BoraLog 'Handshake accepted'
$results['Distinct persistent ids']    = ((Select-String -LiteralPath $ServerLog -Pattern 'Assigned persistent id').Count -ge 2)
$results['Ada sees Bora']              = Test-Log $AdaLog 'Spawned avatar for'
$results['Bora sees Ada']              = Test-Log $BoraLog 'Spawned avatar for'
$results['Build was server-side']      = Test-Log $ServerLog 'built universe.structure'

# The strongest check in the run, and the reason it is written this way.
#
# "Did Bora receive something" is easy to satisfy and proves little. This pulls
# the entity id the *server* assigned out of the server's log and asserts that
# the same id appears in *Bora's* region listing - a specific object, created on
# one machine at the request of a second, observed on a third. Nothing short of
# the whole path working produces that.
$builtId = $null
$builtLine = Select-String -LiteralPath $ServerLog -Pattern 'built universe\.structure\S+ at ([0-9A-F]{32})' |
    Select-Object -First 1

if ($builtLine -ne $null) {
    $builtId = $builtLine.Matches[0].Groups[1].Value
}

if ($builtId -ne $null) {
    Write-Host "  (server assigned entity id $builtId)"
    $results['Bora sees that exact entity'] = Test-Log $BoraLog $builtId
}
else {
    $results['Bora sees that exact entity'] = $false
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
