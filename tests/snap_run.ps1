param(
    [Parameter(Mandatory = $true)][int]$N,
    [Parameter(Mandatory = $true)][int]$K,
    [string]$BuildDir = 'build_win_dev',
    [string]$Demo = 'dam1',
    [string]$Tag = '',
    [switch]$Bot,            # GE_BOT=1 (co-op bot adds a chr + port_bot state to the sim)
    [string]$Excl = '',      # GE_SNAPEXCL override (comma list of object names)
    [string]$Cats = '',      # GE_SNAPCATS override (default GEP)
    [switch]$High,           # GE_SNAPHIGH=1: also snapshot the portAllocHigh arena
    [string]$Margin = '',    # GE_SNAPMARGIN bytes
    [switch]$Detail,         # print every snaptest line (mismatching objects / bytes), not just the summary
    [switch]$NoRef           # skip the reference run (reuse build_tests\snapref.<Demo><Tag>.txt)
)
# Rollback snapshot spike runner.
# 1. reference run: the demo with GE_DEMODIAG + GE_DEMOPOS and no snapshot test (what the oracle sees)
# 2. test run: same with GE_SNAPTEST=N,K
# The test run's fingerprint+position stream must equal reference[..tick N+K-1] + reference[pos N..end]:
# pass 1 up to the restore, then the replayed ticks N.. and the rest of the demo.
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir 'ProjectMidas.exe')
$log = Join-Path $root ("build_tests\snap.{0}.{1}.{2}{3}.log" -f $Demo, $N, $K, $Tag)
$refFile = Join-Path $root ("build_tests\snapref.{0}{1}.txt" -f $Demo, $Tag)
$env:GE_DEMO = $Demo
$env:GE_DEMODIAG = '1'
$env:GE_DEMOPOS = '1'
$env:GE_TICKHZ = '60'
$env:GE_FRAMES = '6000'   # the replayed K ticks cost extra pumps; 2400 would cut the demo short
$env:GE_NOINISAVE = '1'
if ($Bot) { $env:GE_BOT = '1' }
function Run-Game {
    @(cmd /c ('"{0}" 2>&1' -f $exe) | ForEach-Object { ([string]$_).TrimEnd("`r") })
}
function Sim-Lines($l) { @($l | Where-Object { $_ -match '^ge: demo (tick|pos:) ' }) }

if ($NoRef -and (Test-Path $refFile)) {
    $ref = @(Get-Content -LiteralPath $refFile)
} else {
    $ref = Sim-Lines (Run-Game)
    [System.IO.File]::WriteAllText($refFile, ($ref -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))
}
if ($Excl -ne '') { $env:GE_SNAPEXCL = $Excl }
if ($Cats -ne '') { $env:GE_SNAPCATS = $Cats }
if ($High) { $env:GE_SNAPHIGH = '1' }
if ($Margin -ne '') { $env:GE_SNAPMARGIN = $Margin }
$env:GE_SNAPTEST = "$N,$K"
$lines = Run-Game
Remove-Item Env:GE_SNAPTEST
[System.IO.File]::WriteAllText($log, ($lines -join "`n") + "`n", (New-Object System.Text.UTF8Encoding($false)))
$got = Sim-Lines $lines

# keep only the first demo of the reference (a second attract demo may follow before GE_FRAMES ends)
$p1 = @(0..($ref.Count - 1) | Where-Object { $ref[$_] -match '^ge: demo pos: 1 ' })
if ($p1.Count -gt 1) { $ref = @($ref[0..($p1[1] - 1)]) }
$a = [Array]::IndexOf($ref, $ref.Where({ $_ -match ("^ge: demo tick {0} " -f ($N + $K - 1)) })[0])
$b = [Array]::IndexOf($ref, $ref.Where({ $_ -match ("^ge: demo pos: {0} " -f $N) })[0])
$exp = @($ref[0..$a]) + @($ref[$b..($ref.Count - 1)])
$bad = 0
$first = -1
# lines past the expected end are the next attract demo starting (GE_FRAMES allows it); ignore them
for ($i = 0; $i -lt $exp.Count; $i++) {
    if ($i -ge $got.Count -or $got[$i] -ne $exp[$i]) {
        $bad++
        if ($first -lt 0) { $first = $i }
    }
}
$pat = if ($Detail) { '^ge: snaptest' } else { '^ge: snaptest: (layout|SAVE|RESTORE|dirty|STATE|sim step|high arena|\(full)' }
$lines | Where-Object { $_ -match $pat } | ForEach-Object { Write-Output $_ }
Write-Output ("SIM-STREAM {0} N={1} K={2}{3}: got {4} lines, expected {5}, differing {6}" -f $Demo, $N, $K, $(if ($Bot) { ' bot' } else { '' }), $got.Count, $exp.Count, $bad)
if ($first -ge 0) {
    Write-Output ("first diff at line {0}:" -f ($first + 1))
    if ($first -lt $got.Count) { Write-Output ("  got: " + $got[$first]) }
    if ($first -lt $exp.Count) { Write-Output ("  exp: " + $exp[$first]) }
}
