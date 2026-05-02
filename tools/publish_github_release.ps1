param(
    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [Parameter(Mandatory = $true)]
    [string]$ReleaseNotesPath,

    [Parameter(Mandatory = $true)]
    [string[]]$AssetPaths,

    [string]$Owner = 'elik745i',
    [string]$Repository = 'JC4880P443C_I_W_Remote',
    [string]$TargetCommitish = 'main',
    [string]$ReleaseName
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ReleaseName)) {
    $ReleaseName = $Tag
}

function Get-GitHubHeaders {
    $query = "protocol=https`nhost=github.com`n`n"
    $result = $query | git credential fill 2>$null
    if (-not $result) {
        throw 'Git credential manager did not return a cached GitHub credential.'
    }

    $map = @{}
    foreach ($line in ($result -split "`r?`n")) {
        if ($line -notmatch '=') {
            continue
        }

        $parts = $line -split '=', 2
        if ($parts.Length -eq 2) {
            $map[$parts[0]] = $parts[1]
        }
    }

    if ([string]::IsNullOrWhiteSpace($map['password'])) {
        throw 'Cached GitHub credential is missing a token/password.'
    }

    return @{
        Authorization = "Bearer $($map['password'])"
        Accept = 'application/vnd.github+json'
        'X-GitHub-Api-Version' = '2022-11-28'
        'User-Agent' = 'JC4880-Release-Script'
    }
}

function Invoke-GitHubApi {
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Headers,

        [Parameter(Mandatory = $true)]
        [string]$Uri,

        [Parameter(Mandatory = $true)]
        [string]$Method,

        [string]$Body,
        [string]$ContentType,
        [string]$InFile
    )

    $invokeParams = @{
        Headers = $Headers
        Uri = $Uri
        Method = $Method
    }

    if ($PSBoundParameters.ContainsKey('Body')) {
        $invokeParams['Body'] = $Body
    }
    if ($PSBoundParameters.ContainsKey('ContentType')) {
        $invokeParams['ContentType'] = $ContentType
    }
    if ($PSBoundParameters.ContainsKey('InFile')) {
        $invokeParams['InFile'] = $InFile
    }

    return Invoke-RestMethod @invokeParams
}

function Convert-ReleaseNotesBody {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Markdown
    )

    $normalized = $Markdown -replace "`r`n", "`n"
    $normalized = $normalized -replace '`', ''
    $normalized = [System.Text.RegularExpressions.Regex]::Replace($normalized, '(?m)^#{1,6}\s*', '')
    $normalized = [System.Text.RegularExpressions.Regex]::Replace($normalized, "`n{3,}", "`n`n")
    return $normalized.Trim()
}

$headers = Get-GitHubHeaders
$notesBody = Convert-ReleaseNotesBody -Markdown (Get-Content $ReleaseNotesPath -Raw)
$releaseUri = "https://api.github.com/repos/$Owner/$Repository/releases"
$tagUri = "$releaseUri/tags/$Tag"

$release = $null
try {
    $release = Invoke-GitHubApi -Headers $headers -Uri $tagUri -Method 'Get'
} catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 404) {
        throw
    }
}

$payload = @{
    tag_name = $Tag
    target_commitish = $TargetCommitish
    name = $ReleaseName
    body = $notesBody
    draft = $false
    prerelease = $false
} | ConvertTo-Json -Depth 5

if ($null -eq $release) {
    $release = Invoke-GitHubApi -Headers $headers -Uri $releaseUri -Method 'Post' -Body $payload -ContentType 'application/json'
} else {
    $release = Invoke-GitHubApi -Headers $headers -Uri "$releaseUri/$($release.id)" -Method 'Patch' -Body $payload -ContentType 'application/json'
}

$currentAssets = Invoke-GitHubApi -Headers $headers -Uri "$releaseUri/$($release.id)/assets" -Method 'Get'
foreach ($assetPath in $AssetPaths) {
    if (-not (Test-Path $assetPath)) {
        throw "Asset not found: $assetPath"
    }

    $name = [IO.Path]::GetFileName($assetPath)
    $existing = $currentAssets | Where-Object { $_.name -eq $name } | Select-Object -First 1
    if ($existing) {
        Invoke-GitHubApi -Headers $headers -Uri "$releaseUri/assets/$($existing.id)" -Method 'Delete' | Out-Null
    }

    $uploadUrl = ($release.upload_url -replace '\{\?name,label\}$', '') + '?name=' + [uri]::EscapeDataString($name)
    $contentType = if ($name.EndsWith('.zip')) { 'application/zip' } else { 'application/octet-stream' }
    Invoke-GitHubApi -Headers $headers -Uri $uploadUrl -Method 'Post' -InFile $assetPath -ContentType $contentType | Out-Null
}

$published = Invoke-GitHubApi -Headers $headers -Uri $tagUri -Method 'Get'
$published.assets | Select-Object name, size | Format-Table -AutoSize
Write-Output "Release published: $($published.html_url)"
