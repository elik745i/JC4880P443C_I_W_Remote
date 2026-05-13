param(
    [string]$WorkspaceRoot = (Get-Location).Path,
    [switch]$ValidateOnly,
    [string]$Owner = 'elik745i',
    [string]$Repository = 'JC4880P443C_I_W_Remote',
    [string]$TargetCommitish = 'main'
)

$ErrorActionPreference = 'Stop'

function Get-ProjectVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CMakeListsPath
    )

    if (-not (Test-Path $CMakeListsPath)) {
        throw "CMakeLists.txt not found: $CMakeListsPath"
    }

    $content = Get-Content $CMakeListsPath -Raw
    $match = [regex]::Match(
        $content,
        'project\([^\r\n)]*VERSION\s+([0-9]+(?:\.[0-9]+)+)\)',
        [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
    )
    if (-not $match.Success) {
        throw 'Unable to resolve project version from CMakeLists.txt.'
    }

    return $match.Groups[1].Value
}

$workspacePath = (Resolve-Path $WorkspaceRoot).Path
$version = Get-ProjectVersion -CMakeListsPath (Join-Path $workspacePath 'CMakeLists.txt')
$tag = "v$version"
$releaseDir = Join-Path $workspacePath (Join-Path 'release' $tag)
$releaseNotesPath = Join-Path $releaseDir "RELEASE_NOTES_$tag.md"
$assetPaths = @(
    (Join-Path $releaseDir "JC4880P443C_I_W_Remote_${tag}_ota.bin"),
    (Join-Path $releaseDir "JC4880P443C_I_W_Remote_${tag}_full_flash.zip"),
    (Join-Path $releaseDir "JC4880P443C_I_W_Remote_C6_${tag}_merged.bin"),
    (Join-Path $releaseDir "JC4880P443C_I_W_Remote_C6_${tag}_full_flash.zip")
)

$missingPaths = @()
foreach ($path in @($releaseNotesPath) + $assetPaths) {
    if (-not (Test-Path $path)) {
        $missingPaths += $path
    }
}

if ($ValidateOnly) {
    [pscustomobject]@{
        Version = $version
        Tag = $tag
        ReleaseDirectory = $releaseDir
        ReleaseNotesPath = $releaseNotesPath
        AssetPaths = $assetPaths
        MissingPaths = $missingPaths
    } | Format-List

    if ($missingPaths.Count -gt 0) {
        throw "Current release metadata is incomplete for $tag."
    }

    return
}

if ($missingPaths.Count -gt 0) {
    throw ((@("Current release metadata is incomplete for ${tag}:") + $missingPaths) -join [Environment]::NewLine)
}

$publishScript = Join-Path $workspacePath 'tools\publish_github_release.ps1'
$publishParams = @{
    Tag = $tag
    ReleaseNotesPath = $releaseNotesPath
    AssetPaths = $assetPaths
    Owner = $Owner
    Repository = $Repository
    TargetCommitish = $TargetCommitish
}

& $publishScript @publishParams