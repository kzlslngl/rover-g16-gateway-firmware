param(
    [string]$ClangPath = "C:\Program Files\LLVM\bin\clang.exe",
    [switch]$SkipFirmwareBuild
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot

Push-Location $projectRoot
try {
    & "$PSScriptRoot\test_host.ps1" -ClangPath $ClangPath
    if ($LASTEXITCODE -ne 0) {
        throw "Host test/static warning gate failed: $LASTEXITCODE"
    }

    if (-not $SkipFirmwareBuild) {
        & pio run
        if ($LASTEXITCODE -ne 0) {
            throw "ESP32 firmware warning/build gate failed: $LASTEXITCODE"
        }
    }

    & git diff --check
    if ($LASTEXITCODE -ne 0) {
        throw "Git whitespace check failed: $LASTEXITCODE"
    }

    Write-Output "quality checks passed"
}
finally {
    Pop-Location
}
