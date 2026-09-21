# 一次性构建 libde265 + libheif，产物安装到 ..\third_party\libheif
$ErrorActionPreference = "Stop"

# 环境适配：本机裸 PATH 无 cmake，使用 VS 自带 CMake（通过 vswhere 探测全路径并加入 PATH）
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$cmakeExe = & $vswhere -latest -products * -find **/cmake.exe | Select-Object -First 1
if ($cmakeExe) { $env:Path = (Split-Path $cmakeExe) + ";" + $env:Path }

function Assert-Ok($what) {
  # 环境适配：原生 exe 非零退出码不会触发 $ErrorActionPreference，需手动检查，避免失败后静默继续
  if ($LASTEXITCODE -ne 0) { throw "$what failed (exit code $LASTEXITCODE)" }
}
$root  = Split-Path -Parent $PSScriptRoot          # 仓库根
$work  = Join-Path $root "third_party\libheif\_build"
New-Item -ItemType Directory -Force $work | Out-Null
function Fetch-Extract($url, $name) {
  $tgz = Join-Path $work "$name.tar.gz"
  if (-not (Test-Path $tgz)) { curl.exe -sL $url -o $tgz }
  if (-not (Test-Path (Join-Path $work $name))) {
    # 环境适配：显式使用系统 bsdtar；若从 Git Bash 继承 PATH，裸 tar 会解析为 GNU tar 并把 "D:\..." 误当作 host:path
    & "$env:SystemRoot\System32\tar.exe" -xzf $tgz -C $work
    if ($LASTEXITCODE -ne 0) { throw "tar extract failed for $name (exit $LASTEXITCODE)" }
  }
}
Fetch-Extract "https://github.com/strukturag/libde265/releases/download/v1.0.15/libde265-1.0.15.tar.gz" "libde265-1.0.15"
Fetch-Extract "https://github.com/strukturag/libheif/releases/download/v1.23.4/libheif-1.23.4.tar.gz"  "libheif-1.23.4"

$de265 = Join-Path $work "libde265-1.0.15"
# 环境适配：libde265 1.0.15 声明 cmake_minimum_required(<3.5)，VS 自带 CMake 4.x 已移除该兼容，需显式放行策略下限
# 缺陷修复（HEIC 马赛克噪声根因）：VS18 / MSVC 14.51 在默认 /O2（含 /Ob2 内联）下会错误编译 libde265 的
# HEVC 解码热路径，导致 heif_decode_image 返回 heif_error_Ok 但像素为块状噪声（尺寸正确、内容错误）。
# 实测：/O2 解码 example.heic 中心像素 (2,99,0) 纯绿噪声；改用 /O1 后中心像素 (143,135,90)、整帧像素正确。
# 因此显式将 libde265 的 Release 优化降级为 /O1（/Od 亦正确但过慢，/O1 兼顾正确性与性能）。
Remove-Item -Recurse -Force "$de265\b" -ErrorAction SilentlyContinue
cmake -S $de265 -B "$de265\b" -A x64 "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
  "-DCMAKE_C_FLAGS_RELEASE=/O1 /MD /DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE=/O1 /MD /DNDEBUG"
Assert-Ok "cmake configure libde265"
cmake --build "$de265\b" --config Release
Assert-Ok "cmake build libde265"
cmake --install "$de265\b" --config Release --prefix (Join-Path $work "de265-install")
Assert-Ok "cmake install libde265"

Remove-Item -Recurse -Force "$work\heif-b" -ErrorAction SilentlyContinue
cmake -S "$work\libheif-1.23.4" -B "$work\heif-b" -A x64 `
  -DWITH_EXAMPLES=OFF -DBUILD_TESTING=OFF `
  -DCMAKE_PREFIX_PATH="$work\de265-install" `
  -DCMAKE_INSTALL_PREFIX="$root\third_party\libheif"
Assert-Ok "cmake configure libheif"
cmake --build "$work\heif-b" --config Release
Assert-Ok "cmake build libheif"
cmake --install "$work\heif-b" --config Release
Assert-Ok "cmake install libheif"

# 修正：brief 的 Files 清单要求 third_party\libheif\bin\libde265.dll 入库，
# 但 libheif 安装步骤不会拷贝该运行时依赖（dumpbin 证实 heif.dll 动态导入 libde265.dll），此处补齐。
Copy-Item "$work\de265-install\bin\libde265.dll" "$root\third_party\libheif\bin\" -Force

# 测试样例（来自 libheif 源码包 examples/example.heic）
New-Item -ItemType Directory -Force "$root\third_party\testdata" | Out-Null
Copy-Item "$work\libheif-1.23.4\examples\example.heic" "$root\third_party\testdata\example.heic" -Force
Write-Host "OK: third_party\libheif and testdata ready"
