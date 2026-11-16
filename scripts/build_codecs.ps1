# 在 Windows CI 上按 codec_registry.json 编译各协议版本的 codec_v*.dll
# 每个分支单独 configure/build 一次，产物放在 out/codecs/
#
# MSVC 兼容补丁（克隆后、configure 前打，补丁面只覆盖克隆出的 vendor 目录，
# 不修改上游仓库）：
#   1. Protocol v944/v975 分支的 static_assert(mXxx.size() == ...) 在常量表达式里
#      调用 std::bitset::size()：GCC 的实现是 static constexpr（接受对象名调用），
#      MSVC 的实现是非静态 constexpr 成员（obj.size() 报 C2131，decltype(x)::size()
#      报 C2352）。改写为对 constexpr 临时对象调用 decltype(mXxx){}.size()，
#      两个编译器都接受。
#   2. Protocol 的 CMake 把 sculk/reflection 钉在 v1.4.0，其 string_utils.hpp
#      缺 #include <charconv>（GCC 传递包含，MSVC 没有），报 C2039。
#      用 /FIcharconv 强制前置包含解决，无需改 Reflection。
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
    $fidelity = if ($c.fidelity) { $c.fidelity } else { 'upstream' }

    if (-not (Test-Path "$src/CMakeLists.txt")) {
        Write-Host "==> clone Protocol branch $branch -> $src (protocol v$v, fidelity=$fidelity)"
        git clone --depth 1 -b $branch https://github.com/SculkCatalystMC/Protocol.git $src
        if ($LASTEXITCODE -ne 0) { throw "clone failed for $branch" }

        # 补丁 1：static_assert(obj.size()) -> static_assert(decltype(obj){}.size())
        $patched = 0
        Get-ChildItem "$src/src" -Recurse -Filter *.cpp | ForEach-Object {
            $text = Get-Content $_.FullName -Raw
            $new = $text -replace 'static_assert\((m\w+)\.size\(\) == (\w+)\);', 'static_assert(decltype($1){}.size() == $2);'
            if ($new -ne $text) {
                Set-Content $_.FullName -Value $new -NoNewline -Encoding UTF8
                $patched++
            }
        }
        Write-Host "==> MSVC patch: static_assert rewritten in $patched file(s)"

        # 补丁 2：若上游 Reflection 已修复 <charconv>（GIT_TAG > v1.4.0）则本补丁自动失效。
        $cmake = Join-Path $src 'CMakeLists.txt'
        if ((Test-Path $cmake) -and (Select-String -Path $cmake -Pattern 'GIT_TAG v1\.4\.0' -Quiet)) {
            Write-Host "==> reflection pinned to v1.4.0; /FIcharconv workaround will be applied at configure"
        }
    }

    # 字段级模型目前只在 main(=1001) 上验证过；v944 / v975 / v898(派生) 的 API 与之不同，
    # 先用稳定的包头级编解码，避免跨分支 API 漂移导致编译失败。
    # sculk/protocol/Version.hpp 也只存在于 main 分支，其余分支用 registry 的 mc 字符串。
    $fullModel = 'OFF'
    $hasVer = 'OFF'
    if ($branch -eq 'main') { $fullModel = 'ON'; $hasVer = 'ON' }
    $mcVersion = if ($c.mc) { $c.mc } else { "protocol $v" }

    # /FIcharconv：reflection v1.4.0 的 string_utils.hpp 缺 <charconv>（MSVC C2039）。
    $cxxFlags = '/DWIN32 /D_WINDOWS /EHsc /GR /utf-8 /FIcharconv'

    Write-Host "==> configure codec v$v (branch=$branch abi=$abi fidelity=$fidelity full_model=$fullModel)"
    cmake -S codec -B $build -A x64 `
        "-DPROTOCOL_SOURCE_DIR=$src" `
        "-DCV_PROTOCOL_VERSION=$v" `
        "-DCV_ABI_NS=$abi" `
        "-DCV_BRANCH=$branch" `
        "-DCV_MC_VERSION=$mcVersion" `
        "-DCV_HAS_VERSION_HPP=$hasVer" `
        "-DCMAKE_CXX_FLAGS=$cxxFlags" `
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
