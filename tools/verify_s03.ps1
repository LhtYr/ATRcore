# Rebuild and verify S03, preserving historical S01/S02 evidence.
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $projectRoot
$evidenceDir = Join-Path $projectRoot 'results/S03'
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
$script:records = @()
function Run-Check {
    param([string]$Name, [string]$Program, [string[]]$Arguments, [int]$Expected = 0)
    $log = Join-Path $evidenceDir "$Name.log"
    $displayArgs = $Arguments | ForEach-Object { if ($_ -match '\s') { "'" + $_.Replace("'", "''") + "'" } else { $_ } }
    $command = $Program + ' ' + ($displayArgs -join ' ')
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = & $Program @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $lines | ForEach-Object { $_.ToString() } | Out-File -LiteralPath $log -Encoding utf8
    $script:records += [pscustomobject]@{Test=$Name; Command=$command; ExitCode=$code; Expected=$Expected}
    $script:records | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'commands.csv')
    Write-Output "$command -> exit=$code expected=$Expected"
    if ($code -ne $Expected) { throw "Unexpected exit: $Name (see $log)" }
}
$script:dependencyRecords = @()
function Check-Dependency {
    param([string]$Header, [int]$ExpectedCount, [string]$LogName)
    $objects = @(Get-ChildItem -LiteralPath build -Filter '*.d' -Recurse | Where-Object {
        (Get-Content -Raw -LiteralPath $_.FullName).Contains($Header)
    } | ForEach-Object { [IO.Path]::ChangeExtension($_.FullName, '.o') })
    if ($objects.Count -ne $ExpectedCount) { throw "Expected $ExpectedCount dependents of $Header, got $($objects.Count)" }
    $before = @{}
    foreach ($obj in $objects) { $before[$obj] = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks }
    $originalTime = (Get-Item -LiteralPath $Header).LastWriteTimeUtc
    try {
        (Get-Item -LiteralPath $Header).LastWriteTimeUtc = [DateTime]::UtcNow
        Run-Check $LogName 'make' @('all','debug')
        foreach ($obj in $objects) {
            $after = (Get-Item -LiteralPath $obj).LastWriteTimeUtc.Ticks
            if ($after -le $before[$obj]) { throw "Object not rebuilt: $obj" }
            $script:dependencyRecords += [pscustomobject]@{Header=$Header; Object=$obj; BeforeTicks=$before[$obj]; AfterTicks=$after; Result='PASS'}
        }
        $script:dependencyRecords | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'dependencies.csv')
    } finally { (Get-Item -LiteralPath $Header).LastWriteTimeUtc = $originalTime }
}
Run-Check '10-compiler' 'clang' @('--version')
Run-Check '11-clean' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '12-all' 'make' @('all')
Run-Check '13-debug' 'make' @('debug')
Run-Check '14-logger' './build/char.exe' @('logger-check','results/S03/logger_fixture.csv')
Run-Check '15-debug-logger' './build/char_debug.exe' @('logger-check','results/S03/logger_fixture_debug.csv')
Run-Check '16-check-regression' 'make' @('check')
Run-Check '17-debug-regression' './build/char_debug.exe' @('check')
Run-Check '18-default-run' 'make' @('run')
Run-Check '19-boundary-run' './build/main.exe' @('--config','config/delayed_ramp.cfg','--sample','2')
Run-Check '20-missing-parent' './build/char.exe' @('logger-check','build/no-such-s03-directory/output.csv') 1
Run-Check '21-invalid-cli' './build/char.exe' @('logger-check','results/unwritten.csv','unexpected') 1
Run-Check '22-help' './build/char.exe' @('--help')
Run-Check '23-make-help' 'make' @('help')
Run-Check '24-noop' 'make' @('-q','all','debug')
Check-Dependency 'include/Logger.h' 4 '25-logger-header-rebuild'
Check-Dependency 'src/logger_columns.def' 2 '26-column-table-rebuild'
Run-Check '27-noop' 'make' @('-q','all','debug')
Run-Check '28-clean-strict' 'make' @('clean')
Run-Check '29-strict-build' 'make' @('all','debug','EXTRA_CFLAGS=-pedantic-errors -Werror')
Run-Check '30-strict-check' 'make' @('check')
Run-Check '31-strict-debug-check' './build/char_debug.exe' @('check')
Run-Check '32-clean-final' 'make' @('clean')
Run-Check '33-final-build' 'make' @('all','debug')
Run-Check '34-clangd-main' 'clangd' @('--check=src/main.c')
Run-Check '35-noop-final' 'make' @('-q','all','debug')
$warnings = @(Get-ChildItem -LiteralPath $evidenceDir -Filter '*.log' | Where-Object {
    $_.Name -match '^(12|13|25|26|29|33)-'
} | Select-String -Pattern 'warning:|error:')
if ($warnings.Count) { throw 'Compiler warning/error in build evidence' }
$releaseHash = (Get-FileHash -LiteralPath 'results/S03/logger_fixture.csv').Hash
$debugHash = (Get-FileHash -LiteralPath 'results/S03/logger_fixture_debug.csv').Hash
if ($releaseHash -ne $debugHash) { throw 'Release/debug CSV bytes differ' }
"fixture SHA256=$releaseHash; release/debug identical" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'csv.sha256.txt')
$blueprintFiles = @(Get-ChildItem -LiteralPath '..' -Filter 'ATRcore_*.md' -File)
if ($blueprintFiles.Count -ne 1) { throw 'Expected one frozen ATRcore blueprint' }
$blueprint = (Get-FileHash -LiteralPath $blueprintFiles[0].FullName).Hash
if ($blueprint -ne 'D92E8B4A1881A50F094507EDC1A861F3D76D8311194247B84009BFB02DE4E86A') {
    throw 'Frozen blueprint hash changed'
}
"SHA256=$blueprint" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'blueprint.sha256.txt')
Write-Output "PASS S03: $($script:records.Count) commands; header 4/4 and table 2/2 rebuilds; release/debug CSV identical."
