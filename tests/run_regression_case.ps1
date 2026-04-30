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

    [string]$CheckFile = "",

    [string]$RequiredFileMarkers = "",

    [string]$CatalogFixture = "",

    [string]$RelocProbe = "",

    [string]$RelocProbeArgs = "",

    [switch]$ExpectCompilerFailure,

    [switch]$RunWithWsl,

    [switch]$SkipIfWslUnavailable,

    [switch]$SkipRun,

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

function Find-Nasm {
    $candidates = @(
        "D:/software/nasm/nasm.exe",
        "C:/Program Files/NASM/nasm.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $command = Get-Command nasm.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "Could not find nasm.exe for assembly-backed regression case"
}

function Convert-AsmToObjectPath {
    param(
        [string]$AsmPath
    )

    $directory = Join-Path $RepoRoot "build/test-objects"
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    return Join-Path $directory ([IO.Path]::GetFileNameWithoutExtension($AsmPath) + ".obj")
}

function Assemble-InputObject {
    param(
        [string]$AsmPath
    )

    $nasm = Find-Nasm
    $objPath = Convert-AsmToObjectPath $AsmPath
    & $nasm -f win64 -o $objPath $AsmPath 2>&1 | Out-String | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to assemble regression object: $AsmPath"
    }
    return $objPath
}

function Find-Wsl {
    $candidates = @(
        "C:/Windows/System32/wsl.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $command = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

function Convert-ToWslPath {
    param(
        [string]$WindowsPath
    )

    $resolved = [IO.Path]::GetFullPath($WindowsPath).Replace('\', '/')
    if ($resolved.Length -lt 2 -or $resolved[1] -ne ':') {
        throw "Cannot convert path to WSL form: $WindowsPath"
    }
    $drive = [char]::ToLowerInvariant($resolved[0])
    return "/mnt/$drive" + $resolved.Substring(2)
}

function Test-WslGccAvailable {
    $wsl = Find-Wsl
    if (-not $wsl) {
        return $false
    }

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $wsl sh -lc "command -v gcc >/dev/null 2>&1" 2>$null | Out-Null
        return $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
}

function Invoke-Compiler {
    param(
        [string[]]$Arguments
    )

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        Push-Location $RepoRoot
        try {
            $output = & $CompilerPath @Arguments 2>&1
        } finally {
            Pop-Location
        }
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

    Push-Location $RepoRoot
    try {
        & $ExePath
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected $ExePath to return $Expected but got $LASTEXITCODE"
    }
}

function Assert-WslExitCode {
    param(
        [string]$ExePath,
        [int]$Expected
    )

    $wsl = Find-Wsl
    if (-not $wsl) {
        throw "WSL is required to run Linux executable regression cases"
    }

    $wslPath = Convert-ToWslPath (Join-Path $RepoRoot $ExePath)
    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $wsl $wslPath 2>&1 | Out-String | Out-Null
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected WSL executable $ExePath to return $Expected but got $LASTEXITCODE"
    }
}

function Invoke-RelocProbe {
    param(
        [string]$ProbePath,
        [string]$ExePath,
        [string]$Arguments
    )

    if ([string]::IsNullOrWhiteSpace($ProbePath)) {
        return
    }

    $resolvedProbe = Join-Path $RepoRoot $ProbePath
    if (-not (Test-Path $resolvedProbe)) {
        throw "Relocation probe does not exist: $resolvedProbe"
    }

    $resolvedExe = Join-Path $RepoRoot $ExePath
    if (-not (Test-Path $resolvedExe)) {
        throw "Executable does not exist for relocation probe: $resolvedExe"
    }

    $probeArgs = @($resolvedExe)
    foreach ($argument in (Split-List $Arguments)) {
        $probeArgs += $argument
    }

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $resolvedProbe @probeArgs 2>&1 | Out-String | Out-Null
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Relocation probe failed for $ExePath"
    }
}

function Enter-CatalogFixture {
    param(
        [string]$FixturePath
    )

    if ([string]::IsNullOrWhiteSpace($FixturePath)) {
        return $null
    }

    $targetPath = Join-Path $RepoRoot "config/import_catalog.txt"
    $resolvedFixture = Join-Path $RepoRoot $FixturePath
    if (-not (Test-Path $resolvedFixture)) {
        throw "Catalog fixture does not exist: $resolvedFixture"
    }

    $state = @{
        TargetPath = $targetPath
        BackupExists = Test-Path $targetPath
        BackupText = $null
    }
    if ($state.BackupExists) {
        $state.BackupText = Get-Content -Path $targetPath -Raw
    }

    $targetDirectory = Split-Path -Parent $targetPath
    if (-not (Test-Path $targetDirectory)) {
        New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    }
    Copy-Item -LiteralPath $resolvedFixture -Destination $targetPath -Force
    return $state
}

function Exit-CatalogFixture {
    param(
        $State
    )

    if ($null -eq $State) {
        return
    }

    if ($State.BackupExists) {
        Set-Content -Path $State.TargetPath -Value $State.BackupText -NoNewline
        return
    }

    if (Test-Path $State.TargetPath) {
        Remove-Item -LiteralPath $State.TargetPath -Force
    }
}

if ($SkipIfWslUnavailable -and -not (Test-WslGccAvailable)) {
    Write-Output "Skipping regression case because WSL with gcc is not available on this host"
    exit 0
}

$catalogState = Enter-CatalogFixture $CatalogFixture
try {
    $compilerInvocation = @()
    foreach ($source in (Split-List $SourceFiles)) {
        $resolvedPath = Join-Path $RepoRoot $source
        if ([IO.Path]::GetExtension($resolvedPath) -eq ".asm") {
            $compilerInvocation += (Assemble-InputObject $resolvedPath)
        } else {
            $compilerInvocation += $resolvedPath
        }
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

    if (-not [string]::IsNullOrWhiteSpace($CheckFile)) {
        $resolvedCheckFile = Join-Path $RepoRoot $CheckFile
        if (-not (Test-Path $resolvedCheckFile)) {
            throw "Expected file to exist for marker checks: $resolvedCheckFile"
        }
        $fileContents = Get-Content -Path $resolvedCheckFile -Raw
        foreach ($marker in (Split-List $RequiredFileMarkers)) {
            if (-not $fileContents.Contains($marker)) {
                throw "Missing required file marker '$marker' in $resolvedCheckFile"
            }
        }
    }

    if ($ExpectCompilerFailure) {
        exit 0
    }

    Invoke-RelocProbe $RelocProbe $OutputExe $RelocProbeArgs

    if ($SkipRun) {
        exit 0
    }

    if ($RunWithWsl) {
        Assert-WslExitCode $OutputExe $ExpectedExit
    } else {
        Assert-ExitCode (Join-Path $RepoRoot $OutputExe) $ExpectedExit
    }
} finally {
    Exit-CatalogFixture $catalogState
}
