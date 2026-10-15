# Rebuild and verify S04, preserving historical S01-S03 evidence.
$ErrorActionPreference = 'Stop'
$env:PATH = 'C:\msys64\clang64\bin;C:\msys64\usr\bin;' + $env:PATH
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Set-Location -LiteralPath $projectRoot
$evidenceDir = Join-Path $projectRoot 'results/S04'
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

# Verify that the test reference retains the original arithmetic exactly.
$sourcePath = '../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.c'
$source = Get-Content -Raw -LiteralPath $sourcePath
$terminal = [regex]::Match($source, '(?ms)^sreal\s+C_Terminal_Data\([^\r\n]*\r?\n\{.*?^\}').Value
$exp = [regex]::Match($source, '(?ms)^sreal C_exp\([^\r\n]*\r?\n\{.*?^\}').Value
if (!$terminal -or !$exp) { throw 'Reference extraction failed' }
$expected = ($terminal + "`n" + $exp) -replace '//[^\r\n]*','' -replace '\bsreal\b','double' -replace '\bC_Terminal_Data\b','reference_terminal_data' -replace '\bC_exp\b','reference_exp' -replace '\bTmin\b','1e-6'
$expected = $expected -replace '^double\s+reference_terminal_data','static double reference_terminal_data'
$actual = Get-Content -Raw -LiteralPath 'tools/reference_exp.c'
$actual = $actual -replace '(?s)/\*.*?\*/','' -replace '#include[^\r\n]*',''
if (($expected -replace '\s','') -cne ($actual -replace '\s','')) { throw 'Reference arithmetic differs from source' }
$header = Get-Content -Raw -LiteralPath '../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.h'
if ($header -notmatch '#define\s+sreal\s+double\b' -or $header -notmatch '#define\s+Tmin\s+0\.000001\b') {
    throw 'Reference source type or Tmin changed'
}
"PASS reference arithmetic matches original after documented name/type/comment substitutions" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'reference-integrity.txt')
Get-FileHash -LiteralPath $sourcePath,'../EngineSim/MyFile/EngineSystem/Engine/C_Base/C_Base.h','tools/reference_exp.c' |
    Select-Object Path,Hash | Export-Csv -NoTypeInformation -Encoding utf8 -Path (Join-Path $evidenceDir 'reference.sha256.csv')

Run-Check '10-compiler' 'clang' @('--version')
Run-Check '11-clean' 'make' @('clean')
if (Test-Path build) { throw 'clean left build behind' }
Run-Check '12-all' 'make' @('all')
Run-Check '13-debug' 'make' @('debug')
Run-Check '14-gas' './build/char.exe' @('gas-check')
Run-Check '15-debug-gas' './build/char_debug.exe' @('gas-check')
Run-Check '16-numeric' './build/char.exe' @('numeric-check','results/S04/numeric.csv')
Run-Check '17-debug-numeric' './build/char_debug.exe' @('numeric-check','results/S04/numeric_debug.csv')
Run-Check '17a-csv-compare' './build/char.exe' @('numeric-compare','results/S04/numeric.csv','results/S04/numeric_debug.csv')
Run-Check '18-check-regression' 'make' @('check')
Run-Check '19-debug-regression' './build/char_debug.exe' @('check')
Run-Check '20-default-run' 'make' @('run')
Run-Check '21-missing-parent' './build/char.exe' @('numeric-check','build/no-such-s04-directory/output.csv') 1
Run-Check '22-invalid-cli' './build/char.exe' @('gas-check','unexpected') 1
Run-Check '23-help' './build/char.exe' @('--help')
Run-Check '24-make-help' 'make' @('help')
Run-Check '25-noop' 'make' @('-q','all','debug')
Check-Dependency 'include/component/Gas.h' 22 '26-gas-header-rebuild'
Check-Dependency 'include/utils/numeric_utils.h' 4 '27-numeric-header-rebuild'
Run-Check '28-noop' 'make' @('-q','all','debug')
Run-Check '29-clean-strict' 'make' @('clean')
Run-Check '30-strict-build' 'make' @('all','debug','EXTRA_CFLAGS=-pedantic-errors -Werror')
Run-Check '31-strict-check' 'make' @('check')
Run-Check '32-strict-debug-check' './build/char_debug.exe' @('check')
Run-Check '33-clean-final' 'make' @('clean')
Run-Check '34-final-build' 'make' @('all','debug')
Run-Check '35-noop-final' 'make' @('-q','all','debug')
$warnings = @(Get-ChildItem -LiteralPath $evidenceDir -Filter '*.log' | Where-Object {
    $_.Name -match '^(12|13|26|27|30|34)-'
} | Select-String -Pattern 'warning:|error:')
if ($warnings.Count) { throw 'Compiler warning/error in formal build evidence' }
$releaseHash = (Get-FileHash -LiteralPath 'results/S04/numeric.csv').Hash
$debugHash = (Get-FileHash -LiteralPath 'results/S04/numeric_debug.csv').Hash
"release SHA256=$releaseHash`ndebug SHA256=$debugHash`nNumerical comparison: see 17a-csv-compare.log; byte identity is not required." | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'csv.sha256.txt')
$blueprintFiles = @(Get-ChildItem -LiteralPath '..' -Filter 'ATRcore_*.md' -File)
if ($blueprintFiles.Count -ne 1) { throw 'Expected one frozen ATRcore blueprint' }
$blueprint = (Get-FileHash -LiteralPath $blueprintFiles[0].FullName).Hash
if ($blueprint -ne 'D92E8B4A1881A50F094507EDC1A861F3D76D8311194247B84009BFB02DE4E86A') {
    throw 'Frozen blueprint hash changed'
}
"SHA256=$blueprint" | Out-File -Encoding utf8 -LiteralPath (Join-Path $evidenceDir 'blueprint.sha256.txt')
Write-Output "PASS S04: $($script:records.Count) commands; Gas header 22/22 and numeric header 4/4 rebuilds; release/debug CSV C comparison passed."
