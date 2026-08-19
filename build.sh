#!/usr/bin/env bash
#===============================================================================
# RshMod Zygisk 模块 完整打包脚本
# 用法：
#   ./build.sh module           构建并打包 Magisk/Zygisk 模块 .zip(含 dex,native 可选)
#   ./build.sh module --no-native   仅打包(跳过 native .so 编译,用于无 NDK 环境)
#   ./build.sh native           仅编译 Zygisk native(需要 x86_64 / 支持的 NDK)
#   ./build.sh manager          构建管理端 APK(有 apktool 时)
#   ./build.sh all              全部构建
# 环境变量(自动探测,可显式覆盖):
#   NDK             Android NDK 根目录(需可执行 ndk-build)
#   ANDROID_JAR     android.jar 路径(编译 Java runtime 用)
#   ANDROID_SDK_ROOT Android SDK 根目录(找 build-tools/d8)
#===============================================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
MODULE_STAGE="$OUT/module"
ZX="$ROOT/zygisk"
mkdir -p "$OUT" "$MODULE_STAGE"
echo "=== RshMod 构建脚本 ==="

# ---- 智能探测 SDK ----
if [ -z "$ANDROID_SDK_ROOT" ]; then
  for cand in "$ANDROID_HOME" "$HOME/Android/Sdk" /tmp/android-sdk /opt/android-sdk; do
    [ -d "$cand" ] && ANDROID_SDK_ROOT="$cand" && break
  done
fi
if [ -z "$ANDROID_JAR" ]; then
  for p in "$ANDROID_SDK_ROOT/platforms"/android-*/android.jar; do
    [ -f "$p" ] && ANDROID_JAR="$p" && break
  done
fi
D8_BIN=""; D8_JAR=""
if [ -n "$ANDROID_SDK_ROOT" ]; then
  for b in "$ANDROID_SDK_ROOT/build-tools"/*/d8; do
    [ -x "$b" ] 2>/dev/null && { D8_BIN="$b"; break; }
  done
  for b in "$ANDROID_SDK_ROOT/build-tools"/*/lib/d8.jar; do
    [ -f "$b" ] && { D8_JAR="$b"; break; }
  done
fi
echo "  ANDROID_JAR=$ANDROID_JAR"
echo "  D8_BIN=$D8_BIN  D8_JAR=$D8_JAR"

# ---- [1] 编译 Zygisk native ----
build_native() {
  echo "[native] 编译 Zygisk native (.so) ..."
  if [ -z "$NDK" ]; then
    echo "!!! 未设置 NDK,跳过 native"
    return 1
  fi
  local ndkbuild="$NDK/ndk-build"
  if [ ! -x "$ndkbuild" ] && command -v ndk-build >/dev/null 2>&1; then
    ndkbuild="$(command -v ndk-build)"
  fi
  [ -x "$ndkbuild" ] || { echo "!!! 无 ndk-build,跳过 native"; return 1; }
  local abis="arm64-v8a armeabi-v7a x86 x86_64"
  local ok=0
  for abi in $abis; do
    echo "    -> $abi"
    local libs_dir="$MODULE_STAGE/zygisk/$abi"
    rm -rf "$libs_dir"; mkdir -p "$libs_dir"
    if ( cd "$ZX" && "$ndkbuild" \
          NDK_PROJECT_PATH="$ZX" \
          APP_BUILD_SCRIPT="$ZX/Android.mk" \
          NDK_APPLICATION_MK="$ZX/Application.mk" \
          NDK_OUT="$OUT/obj" \
          NDK_LIBS_OUT="$OUT/libs" \
          APP_ABI="$abi" \
          APP_PLATFORM=android-26 -j4 ); then
      local found
      found=$(find "$OUT/libs/$abi" -name '*.so' 2>/dev/null | head -1)
      if [ -n "$found" ]; then cp "$found" "$libs_dir/librshmod.so"; ok=$((ok+1)); fi
    else
      echo "    !! $abi 编译失败"
    fi
  done
  echo "  native 编译完成: $ok/4 abi"
  [ "$ok" -gt 0 ]
}

# ---- [2] 编译 Java 运行时 -> dex ----
build_dex() {
  echo "[dex] 编译 Java 运行时 RshRuntime -> dex ..."
  if [ -z "$ANDROID_JAR" ]; then echo "!!! 未找到 android.jar,跳过 dex"; return 1; fi
  if [ -z "$D8_BIN" ] && [ -z "$D8_JAR" ]; then echo "!!! 未找到 d8,跳过 dex"; return 1; fi
  rm -rf "$OUT/classes" "$OUT/dex"; mkdir -p "$OUT/classes" "$OUT/dex"
  javac -source 8 -target 8 -cp "$ANDROID_JAR" -d "$OUT/classes" "$ROOT/jni/RshRuntime.java"
  local r8=0
  if [ -n "$D8_BIN" ] && [ -x "$D8_BIN" ]; then
    if "$D8_BIN" --release --lib "$ANDROID_JAR" --min-api 26 --output "$OUT/dex" $(find "$OUT/classes" -name '*.class'); then r8=1; fi
  fi
  if [ "$r8" = 0 ] && [ -n "$D8_JAR" ]; then
    java -cp "$D8_JAR" com.android.tools.r8.D8 --release --lib "$ANDROID_JAR" --min-api 26 --output "$OUT/dex" $(find "$OUT/classes" -name '*.class') && r8=1
  fi
  if [ "$r8" != 1 ]; then echo "!!! d8 执行失败"; return 1; fi
  cp "$OUT/dex/"*.dex "$MODULE_STAGE/rshmod_rt.dex"
  echo "  dex OK -> $MODULE_STAGE/rshmod_rt.dex ($(wc -c < "$MODULE_STAGE/rshmod_rt.dex") bytes)"
}

# ---- [3] 组装模块目录 ----
stage_module() {
  echo "[stage] 组装模块目录 ..."
  cp "$ROOT/module/module.prop"          "$MODULE_STAGE/"
  cp "$ROOT/module/customize.sh"         "$MODULE_STAGE/"
  cp "$ROOT/module/service.sh"           "$MODULE_STAGE/"
  cp "$ROOT/module/rshmod_cli.sh"        "$MODULE_STAGE/"
  cp "$ROOT/module/rsh_info.sh"          "$MODULE_STAGE/"
  cp "$ROOT/module/rshmod_config.json"   "$MODULE_STAGE/"
  rm -rf "$MODULE_STAGE/web"
  cp -r "$ROOT/web"                      "$MODULE_STAGE/web"
  chmod +x "$MODULE_STAGE"/*.sh
  if [ ! -f "$MODULE_STAGE/zygisk/arm64-v8a/librshmod.so" ]; then
    echo "  [提示] 未含 native .so,需在有 x86_64 NDK 的环境补编(见 README)"
  fi
}

# ---- [4] 打包 ----
pack_module() {
  echo "[pack] 打包 Zygisk 模块 zip ..."
  local zip="$OUT/RshMod-zygisk-v1.8.5.zip"
  rm -f "$zip"
  if command -v zip >/dev/null 2>&1; then
    ( cd "$MODULE_STAGE" && zip -r "$zip" . )
  else
    # 无 zip 工具时用 python 打包(标准库,跨平台)
    python3 - "$MODULE_STAGE" "$zip" <<'PYEOF'
import sys, os, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(src):
        for f in files:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, src)
            z.write(p, rel)
PYEOF
  fi
  echo ">>> 模块已生成: $zip"
}

build_manager() {
  echo "[manager] 构建管理端 APK ..."
  if command -v apktool >/dev/null 2>&1 && [ -d "$ROOT/manager" ]; then
    ( cd "$ROOT/manager" && apktool b . -o "$OUT/RshModManager.apk" ) && echo ">>> 管理端: $OUT/RshModManager.apk" || echo "!!! 管理端构建失败"
  else
    echo "!!! 未找到 apktool 或 manager 工程不完整,跳过管理端"
  fi
}

MODE="${1:-all}"; NO_NATIVE=0
[ "${2:-}" = "--no-native" ] && NO_NATIVE=1

case "$MODE" in
  module)
    if [ "$NO_NATIVE" = 1 ]; then echo "[跳过 native]"
    else build_native || echo "[继续] native 不可用,继续打包(无 .so)"; fi
    build_dex; stage_module; pack_module ;;
  native) build_native ;;
  manager) build_manager ;;
  all)
    if [ "$NO_NATIVE" = 1 ]; then echo "[跳过 native]"
    else build_native || echo "[继续] native 不可用,继续(无 .so)"; fi
    build_dex; stage_module; pack_module; build_manager ;;
  *) echo "用法: $0 {module|module --no-native|native|manager|all}" ;;
esac