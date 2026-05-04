param(
    [Parameter(Mandatory = $true)]
    [string]$CompilerPath,

    [Parameter(Mandatory = $true)]
    [string]$LinkerPath,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$SourceFiles,

    [Parameter(Mandatory = $true)]
    [string]$OutputObjects,

    [Parameter(Mandatory = $true)]
    [string]$OutputExe,

    [Parameter(Mandatory = $true)]
    [int]$ExpectedExit,

    [string]$Target = "x86_64-windows",

    [string]$CompilerArgs = "",

    [string]$LinkerArgs = "",

    [switch]$RunWithWsl,

    [switch]$SkipIfWslUnavailable
)

$ErrorActionPreference = "Stop"
if (Get-Variable PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
    $PSNativeCommandUseErrorActionPreference = $false
}
if (Test-Path env:MINIC_IMPORT_CATALOG) {
    Remove-Item env:MINIC_IMPORT_CATALOG
}

$sourceList = @($SourceFiles.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$objectList = @($OutputObjects.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$compilerArgList = @($CompilerArgs.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$linkerArgList = @($LinkerArgs.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })

if ($sourceList.Count -eq 0) {
    throw "Standalone linker regression requires at least one source file"
}

if ($sourceList.Count -ne $objectList.Count) {
    throw "SourceFiles and OutputObjects must contain the same number of entries"
}

$resolvedExe = Join-Path $RepoRoot $OutputExe

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

function Assert-ExitCode {
    param(
        [string]$ExePath,
        [int]$Expected
    )

    & $ExePath
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected linked executable to return $Expected but got $LASTEXITCODE"
    }
}

function Assert-WslExitCode {
    param(
        [string]$ExePath,
        [int]$Expected
    )

    $wsl = Find-Wsl
    if (-not $wsl) {
        throw "WSL is required to run Linux standalone linker regression cases"
    }

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $wsl (Convert-ToWslPath $ExePath) 2>&1 | Out-String | Out-Null
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
    }
    if ($LASTEXITCODE -ne $Expected) {
        throw "Expected linked WSL executable to return $Expected but got $LASTEXITCODE"
    }
}

if ($SkipIfWslUnavailable -and -not (Test-WslGccAvailable)) {
    Write-Output "Skipping standalone linker regression because WSL with gcc is not available on this host"
    exit 0
}

$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    Push-Location $RepoRoot
    try {
        $resolvedObjects = @()
        for ($i = 0; $i -lt $sourceList.Count; ++$i) {
            $resolvedSource = Join-Path $RepoRoot $sourceList[$i]
            $resolvedObject = Join-Path $RepoRoot $objectList[$i]
            $resolvedObjects += $resolvedObject

            $compileArgs = @($resolvedSource, "--target", $Target)
            $compileArgs += $compilerArgList
            $compileArgs += @("-c", "-o", $resolvedObject)
            & $CompilerPath @compileArgs 2>&1 | Out-String | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "Compiler failed to produce object for standalone linker regression: $resolvedSource"
            }
        }

        $resolvedLinkerArgs = @()
        $resolvedLinkerArgs += $resolvedObjects
        $resolvedLinkerArgs += "--target"
        $resolvedLinkerArgs += $Target
        $resolvedLinkerArgs += $linkerArgList
        $resolvedLinkerArgs += "-o"
        $resolvedLinkerArgs += $resolvedExe
        & $LinkerPath @resolvedLinkerArgs 2>&1 | Out-String | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Standalone linker failed to produce executable regression output"
        }

        if ($RunWithWsl) {
            Assert-WslExitCode $resolvedExe $ExpectedExit
        } else {
            Assert-ExitCode $resolvedExe $ExpectedExit
        }
    } finally {
        Pop-Location
    }
} finally {
    $ErrorActionPreference = $savedErrorActionPreference
}
