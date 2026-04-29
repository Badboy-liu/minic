param(
    [Parameter(Mandatory = $true)]
    [string]$CompilerPath,

    [Parameter(Mandatory = $true)]
    [string]$LinkerPath,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceFile,

    [Parameter(Mandatory = $true)]
    [string]$OutputObject,

    [Parameter(Mandatory = $true)]
    [string]$OutputExe,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedExit
)

$ErrorActionPreference = "Stop"
if (Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

$resolvedSource = Join-Path $RepoRoot $SourceFile
$resolvedObject = Join-Path $RepoRoot $OutputObject
$resolvedExe = Join-Path $RepoRoot $OutputExe

$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    Push-Location $RepoRoot
    try {
        & $CompilerPath $resolvedSource -c -o $resolvedObject 2>&1 | Out-String | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Compiler failed to produce object for standalone linker regression"
        }

        & $LinkerPath $resolvedObject -o $resolvedExe 2>&1 | Out-String | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Standalone linker failed to produce executable regression output"
        }

        & $resolvedExe
        if ($LASTEXITCODE -ne $ExpectedExit) {
            throw "Expected linked executable to return $ExpectedExit but got $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
