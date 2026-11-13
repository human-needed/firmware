# Builds the firmware for every board, publishes it as a GitHub release, and points the
# settings page's "Update firmware" button at it:
#   GitHub release v<version> on human-needed/firmware with humanneeded-<board>-<version>.uf2
#   <web repo>/public/firmware/latest.json   { "<board>": { "version", "file", "url" } }
# Then commit and push the web repo; Cloudflare Pages deploys it.
# The version is FW_VERSION in humanneeded/config.h. Needs arduino-cli and gh (`gh auth login`).
#
#   powershell -ExecutionPolicy Bypass -File release.ps1 [-Web <path to human-needed-web clone>]

param([string]$Web = (Join-Path $PSScriptRoot '..\..\web'))

$ErrorActionPreference = 'Stop'
$repo = 'human-needed/firmware'
$sketch = Join-Path $PSScriptRoot 'humanneeded'
$config = Get-Content (Join-Path $sketch 'config.h') -Raw
$version = [regex]::Match($config, '#define FW_VERSION "([^"]+)"').Groups[1].Value

# Boards (BOARD_ID in config.h) and how to build each. A plain build is the HN board.
$boards = @(
    # HN board: 2 MB flash read with the generic boot stage. freq=125 is required: at the core's
    # default 200 MHz the board cannot run from its flash and never comes up on USB.
    @{ id = 'hn-v1';         fqbn = 'rp2040:rp2040:generic:flash=2097152_0,freq=125'; flags = '' },
    # Prototype 0 (KB2040 + ring + MAX98357A), kept for development.
    @{ id = 'kb2040-proto0'; fqbn = 'rp2040:rp2040:adafruit_kb2040';                  flags = '-DBOARD_PROTO0' }
)
# Boards no longer built. Their entries are removed from latest.json, so the settings page
# stops offering them updates (they keep the firmware they have).
$retired = @('kb2040', 'rp2040-zero')

$build = Join-Path $env:TEMP 'humanneeded-release'
$files = @()
foreach ($b in $boards) {
    $out = Join-Path $build $b.id
    & arduino-cli compile --fqbn $b.fqbn --build-property "compiler.cpp.extra_flags=$($b.flags)" --output-dir $out $sketch
    if ($LASTEXITCODE) { throw "Build failed for $($b.id)" }
    $file = Join-Path $build "humanneeded-$($b.id)-$version.uf2"
    Copy-Item (Join-Path $out 'humanneeded.ino.uf2') $file -Force
    $files += $file
}

& gh release view "v$version" --repo $repo *> $null
if ($LASTEXITCODE) {
    & gh release create "v$version" @files --repo $repo --title "Firmware $version" --notes "Install it from https://human-needed.com/settings/ (Update firmware)."
} else {
    & gh release upload "v$version" @files --repo $repo --clobber
}
if ($LASTEXITCODE) { throw 'GitHub release failed' }

$latestPath = Join-Path $Web 'public\firmware\latest.json'
$latest = [ordered]@{}
if (Test-Path $latestPath) {
    (Get-Content $latestPath -Raw | ConvertFrom-Json).PSObject.Properties |
        Where-Object { $retired -notcontains $_.Name } |
        ForEach-Object { $latest[$_.Name] = $_.Value }
}
foreach ($b in $boards) {
    $file = "humanneeded-$($b.id)-$version.uf2"
    $latest[$b.id] = [ordered]@{
        version = $version
        file    = $file
        url     = "https://github.com/$repo/releases/download/v$version/$file"
    }
}
$latest | ConvertTo-Json | Set-Content $latestPath -Encoding ascii

"Released firmware $version for $(($boards | ForEach-Object { $_.id }) -join ', ') and updated latest.json."
'Commit and push the web repo to deploy.'
