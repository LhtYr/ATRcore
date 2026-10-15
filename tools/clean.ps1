$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildPath = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
if ([IO.Path]::GetDirectoryName($buildPath) -ne $projectRoot) {
    throw "Refusing cleanup outside project: $buildPath"
}
if (Test-Path -LiteralPath $buildPath) {
    $item = Get-Item -Force -LiteralPath $buildPath
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Refusing cleanup of a reparse point: $buildPath"
    }
    Write-Output "Removing verified build directory: $buildPath"
    Remove-Item -LiteralPath $buildPath -Recurse -Force
}
