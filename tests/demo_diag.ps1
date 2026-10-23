param(
    [Parameter(Mandatory = $true)][string]$Demo,
    [Parameter(Mandatory = $true)][string]$Out,
    [switch]$Bot,
    [string]$BuildDir = 'build_win'
)

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $root (Join-Path $BuildDir 'ProjectMidas.exe')
if (-not (Test-Path -LiteralPath $exe)) { Write-Error "missing exe: $exe"; exit 2 }
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location) $Out }

$env:GE_DEMO = $Demo
$env:GE_DEMODIAG = '1'
$env:GE_TICKHZ = '60'
$env:GE_FRAMES = '2400'
$env:GE_NOINISAVE = '1'
if ($Bot) { $env:GE_BOT = '1' }

# cmd /c merges stderr at the shell level so PS 5.1 never sees native ErrorRecords.
$lines = @(cmd /c ('"{0}" 2>&1' -f $exe) | ForEach-Object { [string]$_ })
$ticks = @($lines | Where-Object { $_ -match '^ge: demo tick' } | ForEach-Object { $_.TrimEnd("`r") })
if ($ticks.Count -eq 0) { Write-Error "no 'ge: demo tick' lines captured (exe exit $LASTEXITCODE); $Out not written"; exit 3 }

# LF endings, no BOM, to match the git-normalized (eol=lf) baselines.
$text = ($ticks -join "`n") + "`n"
[System.IO.File]::WriteAllText($Out, $text, (New-Object System.Text.UTF8Encoding($false)))
