param(
    [Parameter(Mandatory = $true)]
    [string]$CompilerPath,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceFiles,

    [string]$CompilerArgs = "",

    [Parameter(Mandatory = $true)]
    [string]$OutputExe,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedExit,

    [string]$RequiredTraceMarkers = "",

    [string]$RequiredOutputMarkers = "",

    [switch]$ExpectCompilerFailure,

    [string]$RequiredErrorMarkers = ""
)

$ErrorActionPreference = "Stop"
if (Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}

function Split-List {
    param(
        [string]$Value
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return @()
    }

    return @($Value.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Invoke-Compiler {
    param(
        [string[]]$Arguments
    )

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & $CompilerPath @Arguments 2>&1
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if (-not $ExpectCompilerFailure -and $LASTEXITCODE -ne 0) {
        throw "Compiler failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
    if ($ExpectCompilerFailure -and $LASTEXITCODE -eq 0) {
        throw "Compiler unexpectedly succeeded: $($Arguments -join ' ')"
    }
    return ($output | Out-String)
}

function Assert-ExitCode {
    param(
        [string]$ExePath,
        [int]$Expected
    )

    & $ExePath
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected $ExePath to return $Expected but got $LASTEXITCODE"
    }
}

$compilerInvocation = @()
foreach ($source in (Split-List $SourceFiles)) {
    $compilerInvocation += (Join-Path $RepoRoot $source)
}
foreach ($argument in (Split-List $CompilerArgs)) {
    if ($argument.StartsWith("REPO:")) {
        $compilerInvocation += (Join-Path $RepoRoot $argument.Substring(5))
    } else {
        $compilerInvocation += $argument
    }
}

$compilerOutput = Invoke-Compiler $compilerInvocation

foreach ($marker in (Split-List $RequiredTraceMarkers)) {
    if (-not $compilerOutput.Contains($marker)) {
        throw "Missing required trace marker '$marker'"
    }
}

foreach ($marker in (Split-List $RequiredOutputMarkers)) {
    if (-not $compilerOutput.Contains($marker)) {
        throw "Missing required compiler output marker '$marker'"
    }
}

foreach ($marker in (Split-List $RequiredErrorMarkers)) {
    if (-not $compilerOutput.Contains($marker)) {
        throw "Missing required compiler error marker '$marker'"
    }
}

if ($ExpectCompilerFailure) {
    exit 0
}

Assert-ExitCode (Join-Path $RepoRoot $OutputExe) $ExpectedExit
