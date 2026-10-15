# Reproducible S01 checks. Output and exit status are retained under results/S01.
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $projectRoot
$evidenceDir = Join-Path $projectRoot 'results/S01'
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
$script:records = @()

function Run-Check {
    param([string]$Name, [string]$Program, [string[]]$Arguments, [int]$Expected = 0)
    $log = Join-Path $evidenceDir "$Name.log"
    $displayArgs = $Arguments | ForEach-Object { if ($_ -match '\s') { "'" + $_.Replace("'", "''") + "'" } else { $_ } }
    $command = $Program + ' ' + ($displayArgs -join ' ')
    Write-Output "> $command"
    # Windows PowerShell 5 treats native stderr as ErrorRecord objects, even
    # when testing an intentional nonzero exit. Judge by the native exit code.
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = & $Program @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $lines | ForEach-Object { $_.ToString() } | Out-File -LiteralPath $log -Encoding utf8
    $script:records += [pscustomobject]@{
        Test = $Name; Command = $command; ExitCode = $code; Expected = $Expected
    }
    $script:records | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'commands.csv')
    Write-Output "exit=$code expected=$Expected; log=$log"
    if ($code -ne $Expected) { throw "Unexpected exit: $Name (see $log)" }
}

Run-Check '01-clang-version' 'clang' @('--version')
Run-Check '02-make-version' 'make' @('--version')
Run-Check '03-clean' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '04-all' 'make' @('all')
Run-Check '05-debug' 'make' @('debug')
Run-Check '06-char' 'make' @('char')
Run-Check '07-check' 'make' @('check')
Run-Check '08-run' 'make' @('run')
Run-Check '09-help' 'make' @('help')
Run-Check '10-main-help' './build/main.exe' @('--help')
Run-Check '11-char-help' './build/char.exe' @('--help')
Run-Check '12-main-invalid' './build/main.exe' @('--invalid') 1
Run-Check '13-char-invalid' './build/char.exe' @('invalid') 1
Run-Check '14-char-missing' './build/char.exe' @() 1
Run-Check '15-noop' 'make' @('-q', 'all', 'debug', 'char')

$objects = @('build/src/main.o', 'build/debug/src/main.o', 'build/tools/tools/char_main.o')
$before = @{}
foreach ($obj in $objects) {
    $before[$obj] = (Get-Item $obj).LastWriteTimeUtc.Ticks
    $dep = [IO.Path]::ChangeExtension($obj, '.d')
    if ((Get-Content -Raw $dep) -notmatch 'include/build_info.h') {
        throw "Missing shared header in $dep"
    }
}
$header = Get-Item include/build_info.h
$originalTime = $header.LastWriteTimeUtc
try {
    # Timestamp-only edit: content and model contracts are unchanged.
    $header.LastWriteTimeUtc = [DateTime]::UtcNow
    Run-Check '16-header-rebuild' 'make' @('all', 'debug', 'char')
    $dependencyResults = foreach ($obj in $objects) {
        $after = (Get-Item $obj).LastWriteTimeUtc.Ticks
        if ($after -le $before[$obj]) { throw "Object not rebuilt: $obj" }
        [pscustomobject]@{ Object = $obj; BeforeTicks = $before[$obj]; AfterTicks = $after; Result = 'PASS' }
    }
    $dependencyResults | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'dependencies.csv')
} finally {
    (Get-Item include/build_info.h).LastWriteTimeUtc = $originalTime
}
Run-Check '17-noop-after-header' 'make' @('-q', 'all', 'debug', 'char')
Run-Check '18-clean-for-strict' 'make' @('clean')
Run-Check '19-strict' 'make' @('all', 'debug', 'EXTRA_CFLAGS=-pedantic-errors -Werror')
Run-Check '20-strict-check' 'make' @('check')
Run-Check '21-clean-after-strict' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '22-final-build' 'make' @('all', 'debug')
Run-Check '23-final-check' 'make' @('check')
Run-Check '24-debug-breakpoint' 'lldb' @('-b', '-o', 'breakpoint set --name main', '-o', 'run', '-o', 'frame info', '-o', 'continue', 'build/main_debug.exe')
if ((Get-Content -Raw (Join-Path $evidenceDir '24-debug-breakpoint.log')) -notmatch 'stop reason = breakpoint') {
    throw 'LLDB did not report a breakpoint stop'
}
Run-Check '25-clangd' 'clangd' @('--check=src/main.c')
Run-Check '25b-clangd-char' 'clangd' @('--check=tools/char_main.c')
Run-Check '26-plot-help' 'make' @('plot', 'PLOT_ARGS=--help')
Run-Check '27-plot-pending' 'make' @('plot') 2
Run-Check '28-run-plot-pending' 'make' @('-j4', 'run-plot') 2
Run-Check '29-char-plot-help' 'python' @('plotting/plot_char.py', '--help')
Run-Check '30-char-plot-pending' 'python' @('plotting/plot_char.py', '--csv', 'results/char_turbine.csv') 2
Run-Check '31-final-noop' 'make' @('-q', 'all', 'debug', 'char')

$buildLogs = @('04-all', '05-debug', '16-header-rebuild', '19-strict', '22-final-build')
foreach ($name in $buildLogs) {
    if ((Get-Content -Raw (Join-Path $evidenceDir "$name.log")) -match '(?m)\b(warning|error):') {
        throw "Compiler diagnostic found in $name"
    }
}
foreach ($file in @('.vscode/tasks.json', '.vscode/launch.json', '.vscode/extensions.json')) {
    Get-Content -Raw $file | ConvertFrom-Json | Out-Null
}
foreach ($font in @('TimesNewRoman.ttf', 'SimSun.ttf')) {
    if ((Get-FileHash "plotting/fonts/$font").Hash -ne (Get-FileHash "../atr_drive_sim/plotting/fonts/$font").Hash) {
        throw "Reference font differs: $font"
    }
}
Write-Output 'PASS S01: build, dependency, CLI, debugger and pending plotting behavior verified.'
