# 회귀 테스트 한 번에: 서버 + 헤드리스 크롬 띄우고 regress.mjs 돌리고 정리.
#
#   .\tools\regress.ps1            전부
#   .\tools\regress.ps1 sketch     이름 필터
#
# build\ 가 있어야 한다 (.\build.ps1 먼저). 크롬은 Program Files 의 Chrome 또는 Edge 를 찾는다.
# node 는 PATH 의 것, 없으면 ..\emsdk\node\*\bin\node.exe.
param([string]$Filter = "")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
if (-not (Test-Path (Join-Path $build "WebGPUApp.html"))) {
    Write-Host "build\WebGPUApp.html 이 없습니다 - .\build.ps1 을 먼저 돌리세요" -ForegroundColor Red
    exit 1
}

# node
$node = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $node) {
    $node = Get-ChildItem (Join-Path $root "..\emsdk\node\*\bin\node.exe") -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
}
if (-not $node) { Write-Host "node 를 찾지 못했습니다" -ForegroundColor Red; exit 1 }

# python (http.server). 스토어 스텁이 아닌 것을 고른다.
$python = $null
foreach ($cand in @((Get-Command python -ErrorAction SilentlyContinue).Source,
                    (Get-ChildItem (Join-Path $root "..\emsdk\python\*\python.exe") -ErrorAction SilentlyContinue |
                     Select-Object -First 1 -ExpandProperty FullName))) {
    if ($cand -and (Test-Path $cand) -and ($cand -notlike "*WindowsApps*")) { $python = $cand; break }
}
if (-not $python) { Write-Host "python 을 찾지 못했습니다" -ForegroundColor Red; exit 1 }

# chrome / edge
$chrome = @("$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
            "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
            "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
            "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
            "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe") |
          Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $chrome) { Write-Host "Chrome/Edge 를 찾지 못했습니다" -ForegroundColor Red; exit 1 }

$port = 8123
$cdp = 9222
$profile = Join-Path $env:TEMP "lot-regress-chrome"

$server = Start-Process -FilePath $python -ArgumentList "-m http.server $port" -WorkingDirectory $build -PassThru -WindowStyle Hidden
$browser = Start-Process -FilePath $chrome -PassThru -ArgumentList @(
    "--headless=new", "--remote-debugging-port=$cdp", "--enable-unsafe-swiftshader", "--enable-unsafe-webgpu",
    "--window-size=1100,850", "--user-data-dir=$profile", "http://localhost:$port/WebGPUApp.html")
try {
    Start-Sleep -Seconds 4
    $env:CDP_PORT = "$cdp"
    & $node (Join-Path $PSScriptRoot "regress.mjs") $Filter
    $code = $LASTEXITCODE
} finally {
    Stop-Process -Id $browser.Id -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
}
exit $code
