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
# Board define in config.h -> BOARD_ID and arduino-cli FQBN. Add 'rp2040-zero' when it's in use.
$boards = @(
    @{ define = 'BOARD_PROTO0'; id = 'kb2040-proto0'; fqbn = 'rp2040:rp2040:adafruit_kb2040' },
    @{ define = 'BOARD_KB2040'; id = 'kb2040';        fqbn = 'rp2040:rp2040:adafruit_kb2040' }
)
$selected = $boards | Where-Object { $config -match "(?m)^#define $($_.define)\s+1" } | Select-Object -First 1
if (-not $selected) { throw 'release.ps1 does not know the board selected in config.h; add it to $boards.' }
$board = $selected.id

$build = Join-Path $env:TEMP 'humanneeded-release'
& arduino-cli compile --fqbn $selected.fqbn --output-dir $build $sketch
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
