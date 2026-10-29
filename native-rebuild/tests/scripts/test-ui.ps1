# Runs the page tests (ui/tests/*.test.js) with jsdom, so the DOM regressions
# run instead of skipping. jsdom lives outside the repository in
# build-artifacts/ui-test-deps; install it once with -Install (needs network).
param([switch]$Install, [string]$Filter = '*.test.js')
$ErrorActionPreference = 'Stop'
$workspace = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$deps = Join-Path $workspace 'build-artifacts\ui-test-deps'
$api = Join-Path $deps 'node_modules\jsdom\lib\api.js'
if (-not (Test-Path $api)) {
    if (-not $Install) {
        Write-Error "jsdom not found at $api. Run with -Install (npm install jsdom@26.1.0 into build-artifacts\ui-test-deps)."
    }
    New-Item -ItemType Directory -Force $deps | Out-Null
    npm install --prefix $deps --no-save --no-audit --no-fund jsdom@26.1.0
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
$env:LUME_JSDOM_PATH = $api
Push-Location (Join-Path $workspace 'native-rebuild\ui')
try {
    node --test (Get-ChildItem tests -Filter $Filter | ForEach-Object { "tests/$($_.Name)" })
    exit $LASTEXITCODE
} finally { Pop-Location }
