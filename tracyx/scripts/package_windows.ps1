param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.]+)?$')][string]$Version = '1.0.0',
    [Parameter(Mandatory)][string]$AgentBuildDir,
    [Parameter(Mandatory)][string]$ViewerBuildDir,
    [Parameter(Mandatory)][string]$CaptureBuildDir,
    [Parameter(Mandatory)][string]$DxTimingRoot,
    [Parameter(Mandatory)][string]$VendorCache,
    [string]$WinPixRoot,
    [string]$PresentMonLicense,
    [string]$OutputDirectory = 'build/dist'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$output = [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutputDirectory))
$suffix = if ($Configuration -eq 'Debug') { '-debug' } else { '' }
$name = "TracyX-$Version-win-x64$suffix"
$package = Join-Path $output $name
$archive = Join-Path $output "$name.zip"
if ((Test-Path -LiteralPath $package) -or (Test-Path -LiteralPath $archive)) {
    throw "Package already exists: $package"
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
New-Item -ItemType Directory -Path $package | Out-Null
try {
$bin = New-Item -ItemType Directory -Path (Join-Path $package 'bin')
$licenses = New-Item -ItemType Directory -Path (Join-Path $package 'licenses')
$docs = New-Item -ItemType Directory -Path (Join-Path $package 'docs')
$sdk = New-Item -ItemType Directory -Path (Join-Path $package 'sdk')

function Copy-Required([string]$source, [string]$destination) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required file missing: $source" }
    Copy-Item -LiteralPath $source -Destination $destination
}

foreach ($entry in @(
    @{ Source = (Join-Path $AgentBuildDir "$Configuration/TracyXAgent.exe"); Name = 'TracyXAgent.exe' },
    @{ Source = (Join-Path $ViewerBuildDir "$Configuration/tracy-profiler.exe"); Name = 'tracy-profiler.exe' },
    @{ Source = (Join-Path $CaptureBuildDir "$Configuration/tracy-capture.exe"); Name = 'tracy-capture.exe' }
)) {
    Copy-Required $entry.Source (Join-Path $bin $entry.Name)
}
foreach ($nameInBin in @('TracyXProbeCpp.exe', 'TracyXProbePresentD3D12.exe', 'WinPixEventRuntime.dll', 'PresentMonAPI2Loader.dll')) {
    $source = Join-Path $AgentBuildDir "$Configuration/$nameInBin"
    if (Test-Path -LiteralPath $source -PathType Leaf) { Copy-Item -LiteralPath $source -Destination $bin }
}
Copy-Required (Join-Path $repo 'LICENSE') (Join-Path $package 'LICENSE')
Copy-Required (Join-Path $repo 'public/libbacktrace/LICENSE') (Join-Path $licenses 'libbacktrace-LICENSE')
Copy-Required (Join-Path $repo 'TRACYX.md') (Join-Path $docs 'TRACYX.md')
Copy-Required (Join-Path $repo 'tracyx/INSTALL_WINDOWS.md') (Join-Path $docs 'INSTALL_WINDOWS.md')
Copy-Required (Join-Path $repo 'tracyx/THIRD_PARTY_NOTICES.md') (Join-Path $package 'THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath (Join-Path $repo 'tracyx/README.md') -Destination $docs
Copy-Item -LiteralPath (Join-Path $repo 'public') -Destination $sdk -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'tracyx') -Destination $sdk -Recurse

$dxLicenses = New-Item -ItemType Directory -Path (Join-Path $licenses 'DxTimingCaptureLibrary')
Copy-Required (Join-Path $DxTimingRoot 'LICENSE') (Join-Path $dxLicenses 'LICENSE')
Copy-Required (Join-Path $DxTimingRoot 'THIRD-PARTY-NOTICES.md') (Join-Path $dxLicenses 'THIRD-PARTY-NOTICES.md')
if (Test-Path -LiteralPath (Join-Path $bin 'WinPixEventRuntime.dll')) {
    if (-not $WinPixRoot) { throw 'WinPixRoot is required when WinPixEventRuntime.dll is packaged' }
    $pixLicenses = New-Item -ItemType Directory -Path (Join-Path $licenses 'WinPixEventRuntime')
    Copy-Required (Join-Path $WinPixRoot 'license.txt') (Join-Path $pixLicenses 'license.txt')
    Copy-Required (Join-Path $WinPixRoot 'ThirdPartyNotices.txt') (Join-Path $pixLicenses 'ThirdPartyNotices.txt')
}
if (Test-Path -LiteralPath (Join-Path $bin 'PresentMonAPI2Loader.dll')) {
    if (-not $PresentMonLicense) { throw 'PresentMonLicense is required when PresentMonAPI2Loader.dll is packaged' }
    Copy-Required $PresentMonLicense (Join-Path $licenses 'PresentMon-LICENSE.txt')
}

$vendorPackages = @('base64', 'capstone', 'freetype', 'glfw', 'imgui', 'json', 'libcurl',
    'md4c', 'nfd', 'ppqsort', 'pugixml', 'tidy', 'usearch', 'zstd')
foreach ($vendor in $vendorPackages) {
    $folder = Join-Path $VendorCache $vendor
    if (-not (Test-Path -LiteralPath $folder -PathType Container)) { throw "Viewer vendor source missing: $vendor" }
    $licenseFiles = @(Get-ChildItem -LiteralPath $folder -Recurse -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)(\.|$)' })
    if ($licenseFiles.Count -eq 0) { throw "Viewer vendor license missing: $vendor" }
    $vendorOutput = New-Item -ItemType Directory -Path (Join-Path $licenses $vendor)
    $index = 0
    foreach ($file in $licenseFiles) {
        $index++
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $vendorOutput "$index-$($file.Name)")
    }
}

$hashes = Get-ChildItem -LiteralPath $package -Recurse -File | Sort-Object FullName | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $relative = [IO.Path]::GetRelativePath($package, $_.FullName).Replace('\', '/')
    "$hash  $relative"
}
$hashes | Set-Content -LiteralPath (Join-Path $package 'SHA256SUMS.txt') -Encoding utf8
Compress-Archive -LiteralPath $package -DestinationPath $archive -CompressionLevel Optimal
Write-Output $archive
} catch {
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
    if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
    throw
}
