# Builds the firmware, publishes it as a GitHub release, and points the settings page's
# "Update firmware" button at it:
#   GitHub release v<version> on human-needed/firmware with humanneeded-<board>-<version>.uf2
#   <web repo>/public/firmware/latest.json   { "<board>": { "version", "file", "url" } }
# Then commit and push the web repo; Cloudflare Pages deploys it.
# Needs arduino-cli and gh (signed in with `gh auth login`).
#
#   powershell -ExecutionPolicy Bypass -File release.ps1 [-Web <path to human-needed-web clone>]

param([string]$Web = (Join-Path $PSScriptRoot '..\..\web'))

$ErrorActionPreference = 'Stop'
$repo = 'human-needed/firmware'
$sketch = Join-Path $PSScriptRoot 'humanneeded'
$config = Get-Content (Join-Path $sketch 'config.h') -Raw

$version = [regex]::Match($config, '#define FW_VERSION "([^"]+)"').Groups[1].Value
$boards = @{ 'kb2040' = 'rp2040:rp2040:adafruit_kb2040' }  # add 'rp2040-zero' when it's in use
$board = [regex]::Match($config, '(?m)^#define BOARD_KB2040\s+1') | ForEach-Object { if ($_.Success) { 'kb2040' } }
if (-not $board) { throw 'release.ps1 only knows the KB2040 build so far; add the board to $boards.' }

$build = Join-Path $env:TEMP 'humanneeded-release'
& arduino-cli compile --fqbn $boards[$board] --output-dir $build $sketch
if ($LASTEXITCODE) { throw 'Build failed' }

$file = "humanneeded-$board-$version.uf2"
$uf2 = Join-Path $build $file
Copy-Item (Join-Path $build 'humanneeded.ino.uf2') $uf2 -Force

& gh release view "v$version" --repo $repo *> $null
if ($LASTEXITCODE) {
    & gh release create "v$version" $uf2 --repo $repo --title "Firmware $version" --notes "Install it from https://human-needed.com/settings/ (Update firmware)."
} else {
    & gh release upload "v$version" $uf2 --repo $repo --clobber
}
if ($LASTEXITCODE) { throw 'GitHub release failed' }

$latestPath = Join-Path $Web 'public\firmware\latest.json'
$latest = @{}
if (Test-Path $latestPath) {
    (Get-Content $latestPath -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $latest[$_.Name] = $_.Value }
}
$latest[$board] = [ordered]@{
    version = $version
    file    = $file
    url     = "https://github.com/$repo/releases/download/v$version/$file"
}
$latest | ConvertTo-Json | Set-Content $latestPath -Encoding ascii

"Released $file on GitHub and updated latest.json. Commit and push the web repo to deploy."
