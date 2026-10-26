param(
    [string]$BuildDir = '',          # folder with ProjectMidas.exe and Data\ (default: <repo>\build_win)
    [int]$Frames = 900,              # GE_FRAMES: the run exits by itself after this many frames
    [int]$ShotFrame = 0,             # GE_SHOT rendered frame for the screenshot (default: Frames - 60)
    [string]$Stage = 'levelgen',
    [string]$Mode = 'heist',
    [string]$OutDir = '',            # log + screenshot folder (default: <BuildDir>\smoke_hidden)
    [int]$TimeoutSec = 180,
    [switch]$NoSleep                 # GE_NOSLEEP=1: no real-time pacer. Faster, but far fewer frames get rendered, so
                                     # GE_SHOT (which counts rendered frames) may never fire: pass a small -ShotFrame
)
# Windows smoke test in hidden mode (port/README-headless.md): boots ProjectMidas.exe with GE_HIDDEN=1 into
# GE_STAGE/GE_MODE (default: the v3 bank heist), runs GE_FRAMES frames at a fixed tick, takes one screenshot, and checks:
#   - no visible window ever appears (polled every 100 ms: Process.MainWindowHandle and every top-level window of the pid);
#     if one does, the process is killed at once and the test fails
#   - the exe exits by itself with code 0 ("ge: frame limit reached")
#   - the log has the hidden-mode line and no Lua error, fatal error, crash or GL error lines
#   - the screenshot exists and is not blank (rendering works in a never-shown window)
# Exit code 0 = pass, 1 = fail. Usage:
#   powershell -ExecutionPolicy Bypass -File tests\win\smoke_hidden.ps1 [-BuildDir build_win] [-Frames 900]
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if ($BuildDir -eq '') { $BuildDir = Join-Path $root 'build_win' }
elseif (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $root $BuildDir }
$exe = Join-Path $BuildDir 'ProjectMidas.exe'
if ($OutDir -eq '') { $OutDir = Join-Path $BuildDir 'smoke_hidden' }
if ($ShotFrame -le 0) { $ShotFrame = [Math]::Max(1, $Frames - 60) }
$fails = New-Object System.Collections.Generic.List[string]

if (-not (Test-Path -LiteralPath $exe)) { Write-Output "SMOKE FAIL: no $exe"; exit 1 }
if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'Data\baserom.u.z64'))) { Write-Output "SMOKE FAIL: no Data\baserom.u.z64 in $BuildDir"; exit 1 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Get-ChildItem -LiteralPath $OutDir -Filter 'shot_*.bmp' | Remove-Item -Force
$logOut = Join-Path $OutDir 'stdout.log'
$logErr = Join-Path $OutDir 'stderr.log'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SmokeWin {
    delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    // visible top-level windows owned by process `pid`, and all of them
    public static int[] Count(uint pid) {
        int all = 0, vis = 0;
        EnumWindows(delegate (IntPtr h, IntPtr p) {
            uint wp; GetWindowThreadProcessId(h, out wp);
            if (wp == pid) { all++; if (IsWindowVisible(h)) vis++; }
            return true;
        }, IntPtr.Zero);
        return new int[] { vis, all };
    }
}
'@

# the run's environment (inherited by the child; this PowerShell process exits right after)
$env:GE_HIDDEN = '1'
$env:GE_STAGE = $Stage
$env:GE_MODE = $Mode
$env:GE_SKIPINTRO = '1'
$env:GE_SEED = '7'
$env:GE_FIXEDTICK = '1'
$env:GE_TICKHZ = '60'
$env:GE_FRAMES = "$Frames"
$env:GE_SHOT = "$ShotFrame"
$env:GE_FULLHEIGHT = '0'
$env:GE_EDGEINSET = '1'
$env:GE_NOINISAVE = '1'
$env:GE_NODIRECTLOG = '1'
if ($NoSleep) { $env:GE_NOSLEEP = '1' }

$t0 = Get-Date
$p = Start-Process -FilePath $exe -WorkingDirectory $OutDir -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $logOut -RedirectStandardError $logErr
$null = $p.Handle   # keep the process handle open, or ExitCode reads back empty after the exit
try { $p.PriorityClass = 'BelowNormal' } catch { }
$maxAll = 0
$visible = 0
while (-not $p.HasExited) {
    $p.Refresh()
    $c = [SmokeWin]::Count([uint32]$p.Id)
    if ($c[1] -gt $maxAll) { $maxAll = $c[1] }
    if ($c[0] -gt 0 -or $p.MainWindowHandle -ne [IntPtr]::Zero) {
        $visible = 1
        try { $p.Kill() } catch { }
        $fails.Add("a VISIBLE window appeared (visible=$($c[0]) MainWindowHandle=$($p.MainWindowHandle)); process killed")
        break
    }
    if (((Get-Date) - $t0).TotalSeconds -gt $TimeoutSec) {
        try { $p.Kill() } catch { }
        $fails.Add("timed out after $TimeoutSec s; process killed")
        break
    }
    Start-Sleep -Milliseconds 100
}
$p.WaitForExit()
$secs = [int]((Get-Date) - $t0).TotalSeconds
$code = $p.ExitCode
Write-Output ("SMOKE run: {0} s, exit code {1}, top-level windows of the pid (all hidden): up to {2}, visible: {3}" -f $secs, $code, $maxAll, $visible)

$log = @()
if (Test-Path -LiteralPath $logOut) { $log += Get-Content -LiteralPath $logOut }
if (Test-Path -LiteralPath $logErr) { $log += Get-Content -LiteralPath $logErr }
if ($code -ne 0) { $fails.Add("exit code $code") }
if (-not ($log -match 'ge: frame limit reached')) { $fails.Add('no "ge: frame limit reached" line') }
if (-not ($log -match 'SDL: hidden mode: the window is never shown')) { $fails.Add('no hidden-mode line: is this build older than hidden mode?') }
$pads = @($log | Where-Object { $_ -match 'ge: hidden: ([0-9]+) joysticks visible' })
if ($pads.Count -eq 0) { $fails.Add('no "ge: hidden: N joysticks visible" line') }
elseif ($pads[0] -notmatch 'ge: hidden: 0 joysticks') { $fails.Add("controllers were opened: $($pads[0])") }
$bad = @($log | Where-Object { $_ -cmatch 'ge: lua: .*error|FATAL|[Ff]atal error|CRASH|crash dump|GL error|Could not' })
foreach ($l in ($bad | Select-Object -First 5)) { $fails.Add("log: $l") }
foreach ($l in @($log | Where-Object { $_ -match '^GL: renderer:|^ge: hidden:|^ge: audio: opened|^ge: frame limit' })) {
    Write-Output "SMOKE log: $l"
}

# the screenshot: BMP -> PNG, then a blank check (a hidden window whose default framebuffer never renders reads back as one colour)
$bmp = Get-ChildItem -LiteralPath $OutDir -Filter 'shot_*.bmp' | Sort-Object Name | Select-Object -Last 1
if ($null -eq $bmp) {
    $fails.Add("no screenshot (GE_SHOT=$ShotFrame)")
} else {
    Add-Type -AssemblyName System.Drawing
    $png = [System.IO.Path]::ChangeExtension($bmp.FullName, '.png')
    $img = [System.Drawing.Bitmap]::FromFile($bmp.FullName)
    $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $sum = 0.0; $sum2 = 0.0; $n = 0
    for ($y = 0; $y -lt $img.Height; $y += 8) {
        for ($x = 0; $x -lt $img.Width; $x += 8) {
            $c = $img.GetPixel($x, $y)
            $v = 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
            $sum += $v; $sum2 += $v * $v; $n++
        }
    }
    $w = $img.Width; $h = $img.Height
    $img.Dispose()
    Remove-Item -LiteralPath $bmp.FullName -Force
    $mean = $sum / $n
    $sd = [Math]::Sqrt([Math]::Max(0.0, $sum2 / $n - $mean * $mean))
    Write-Output ("SMOKE shot: {0} ({1}x{2}) mean {3:N1} sd {4:N1}" -f $png, $w, $h, $mean, $sd)
    if ($sd -lt 4.0) { $fails.Add(("screenshot is blank (sd {0:N1})" -f $sd)) }
}

if ($fails.Count -eq 0) {
    Write-Output "SMOKE PASS ($Stage/$Mode, $Frames frames, hidden)"
    exit 0
}
foreach ($f in $fails) { Write-Output "SMOKE FAIL: $f" }
exit 1
