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

# 빌드가 소스보다 오래됐으면 멈춘다. 옛 빌드로 통과/실패를 보면 시간을 통째로 날린다
# (JS 만 고쳤을 때 재링크가 안 되는 경우가 있었다).
$out = Get-Item (Join-Path $build "WebGPUApp.js")
$newest = Get-ChildItem (Join-Path $root "src"), (Join-Path $root "shaders") -Recurse -File |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newest -and $newest.LastWriteTime -gt $out.LastWriteTime) {
    Write-Host "빌드가 소스보다 오래됐습니다: $($newest.Name) > WebGPUApp.js" -ForegroundColor Yellow
    Write-Host "  .uild.ps1 을 먼저 돌리세요 (JS 만 고쳤다면 build\WebGPUApp.js 를 지우고 다시)" -ForegroundColor Yellow
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
