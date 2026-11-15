# 在 Windows CI 上按 codec_registry.json 编译各协议版本的 codec_v*.dll
# 每个分支单独 configure/build 一次，产物放在 out/codecs/
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$registry = Get-Content codec_registry.json -Raw | ConvertFrom-Json
$ready = @($registry.codecs | Where-Object { $_.status -eq 'ready' })

New-Item -ItemType Directory -Force -Path vendor/protocol, build/codecs, out/codecs | Out-Null

foreach ($c in $ready) {
    $v = $c.protocol
    $branch = $c.branch
    $abi = $c.abi
    $src = "vendor/protocol/v$v"
    $build = "build/codecs/v$v"

    if (-not (Test-Path "$src/CMakeLists.txt")) {
        Write-Host "==> clone Protocol branch $branch -> $src"
        git clone --depth 1 -b $branch https://github.com/SculkCatalystMC/Protocol.git $src
        if ($LASTEXITCODE -ne 0) { throw "clone failed for $branch" }
    }

    # 字段级模型目前只在 main(=1001) 上验证过；v944 / v975 的 API 与之不同，
    # 先用稳定的包头级编解码，避免跨分支 API 漂移导致编译失败。
    $fullModel = 'OFF'
    if ($branch -eq 'main') { $fullModel = 'ON' }

    Write-Host "==> configure codec v$v (branch=$branch abi=$abi full_model=$fullModel)"
    cmake -S codec -B $build -A x64 `
        "-DPROTOCOL_SOURCE_DIR=$src" `
        "-DCV_PROTOCOL_VERSION=$v" `
        "-DCV_ABI_NS=$abi" `
        "-DCV_BRANCH=$branch" `
        "-DCV_ENABLE_FULL_PACKET_MODEL=$fullModel"
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for v$v" }

    Write-Host "==> build codec v$v"
    cmake --build $build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "build failed for v$v" }

    $dll = Join-Path $build "out/Release/codec_v$v.dll"
    if (-not (Test-Path $dll)) { $dll = Join-Path $build "out/codec_v$v.dll" }
    if (-not (Test-Path $dll)) { throw "codec_v$v.dll not found" }
    Copy-Item $dll "out/codecs/codec_v$v.dll" -Force
    Write-Host "==> produced out/codecs/codec_v$v.dll"
}

Get-ChildItem out/codecs | ForEach-Object { Write-Host ("    {0} ({1} bytes)" -f $_.Name, $_.Length) }
