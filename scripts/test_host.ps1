param(
    [string]$ClangPath = "C:\Program Files\LLVM\bin\clang.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot "test\build"
$testExecutable = Join-Path $buildDirectory "core_tests.exe"

if (-not (Test-Path -LiteralPath $ClangPath)) {
    $clangCommand = Get-Command clang -ErrorAction SilentlyContinue
    if ($null -eq $clangCommand) {
        throw "Clang bulunamadı. LLVM kurun veya -ClangPath ile yolu verin."
    }
    $ClangPath = $clangCommand.Source
}

New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

$arguments = @(
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-pedantic",
    "-Iprotocol",
    "-Icomponents/crc32/include",
    "-Icomponents/freshness/include",
    "-Icomponents/register_image/include",
    "-Icomponents/sbus_decoder/include",
    "-Icomponents/session_id/include",
    "components/crc32/crc32_iso_hdlc.c",
    "components/freshness/freshness.c",
    "components/register_image/register_endian.c",
    "components/register_image/register_image.c",
    "components/sbus_decoder/sbus_decoder.c",
    "components/session_id/session_id.c",
    "test/core_tests.c",
    "-o",
    $testExecutable
)

Push-Location $projectRoot
try {
    & $ClangPath @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Host test derlemesi başarısız: $LASTEXITCODE"
    }

    & $testExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "Host testleri başarısız: $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
