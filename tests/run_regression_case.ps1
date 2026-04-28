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

    [string]$RequiredOutputMarkers = ""
)

$ErrorActionPreference = "Stop"

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

    $output = & $CompilerPath @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Compiler failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
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

Assert-ExitCode (Join-Path $RepoRoot $OutputExe) $ExpectedExit
