#!/usr/bin/env bash
# Сборка APK roSkos Donut Bench без Gradle: aapt2 + javac + d8 + NDK clang++.
set -euo pipefail

cd "$(dirname "$0")/.."
. scripts/android-env.sh
export PATH="$HOME/.cargo/bin:$PATH"

APP_DIR=android/app
BUILD_DIR=android/build
GEN_DIR="$BUILD_DIR/generated"
CLASSES_DIR="$BUILD_DIR/classes"
DEX_DIR="$BUILD_DIR/dex"
LIBS_DIR="$BUILD_DIR/lib"
APK_OUT=android/roskos-donut-bench.apk
ANDROID_JAR="$ANDROID_HOME/platforms/android-34/android.jar"
NDK_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"

rm -rf "$GEN_DIR" "$CLASSES_DIR" "$DEX_DIR" "$LIBS_DIR"
mkdir -p "$GEN_DIR" "$CLASSES_DIR" "$DEX_DIR" "$LIBS_DIR"

echo "==> Компиляция шейдеров Vulkan (glslc)"
"$ANDROID_NDK_HOME/shader-tools/linux-x86_64/glslc" --target-env=vulkan1.1 -O \
  "$APP_DIR/cpp/shaders/vk_donut.vert" -o "$GEN_DIR/vk_donut.vert.spv"
"$ANDROID_NDK_HOME/shader-tools/linux-x86_64/glslc" --target-env=vulkan1.1 -O \
  "$APP_DIR/cpp/shaders/vk_donut.frag" -o "$GEN_DIR/vk_donut.frag.spv"
python3 scripts/embed_spirv.py "$GEN_DIR/vk_donut.vert.spv" vk_donut_vert_spv "$GEN_DIR/vk_shaders.h"
python3 scripts/embed_spirv.py "$GEN_DIR/vk_donut.frag.spv" vk_donut_frag_spv "$GEN_DIR/vk_shaders.h" --append

echo "==> Сборка Rust-ядра под Android"
build_rust() {
  local target=$1
  local triple=$2
  local env_name
  env_name="CARGO_TARGET_$(echo "$target" | tr 'a-z-' 'A-Z_')_LINKER"
  env "$env_name=$NDK_BIN/$triple" \
    cargo build --release --target "$target" --manifest-path crates/roskos-core/Cargo.toml
}
build_rust aarch64-linux-android aarch64-linux-android24-clang
build_rust x86_64-linux-android x86_64-linux-android24-clang

echo "==> Сборка нативных библиотек"
SOURCES=(
  "$APP_DIR/cpp/common.cpp"
  "$APP_DIR/cpp/gles_backend.cpp"
  "$APP_DIR/cpp/roskos_backend.cpp"
  "$APP_DIR/cpp/world_backend.cpp"
  "$APP_DIR/cpp/vulkan_setup.cpp"
  "$APP_DIR/cpp/vulkan_render.cpp"
  "$APP_DIR/cpp/main.cpp"
)

build_native() {
  local abi=$1
  local cxx=$2
  local rust_lib=$3
  echo "    $abi"
  mkdir -p "$LIBS_DIR/$abi"
  "$NDK_BIN/$cxx" -shared -fPIC -std=c++17 -O2 \
    -I"$APP_DIR/cpp" -I"$GEN_DIR" \
    -o "$LIBS_DIR/$abi/libroskosbench.so" \
    "${SOURCES[@]}" "$rust_lib" \
    -static-libstdc++ \
    -lvulkan -lEGL -lGLESv3 -landroid -llog -ldl -lm \
    -Wl,--exclude-libs,ALL
}

build_native arm64-v8a aarch64-linux-android24-clang++ \
  "$(pwd)/crates/roskos-core/target/aarch64-linux-android/release/libroskos_core.a"
build_native x86_64 x86_64-linux-android24-clang++ \
  "$(pwd)/crates/roskos-core/target/x86_64-linux-android/release/libroskos_core.a"

echo "==> Сборка Java и dex"
javac -source 8 -target 8 -bootclasspath "$ANDROID_JAR" -d "$CLASSES_DIR" \
  $(find "$APP_DIR/java" -name '*.java')
d8 --lib "$ANDROID_JAR" --min-api 24 --output "$DEX_DIR" \
  $(find "$CLASSES_DIR" -name '*.class')

echo "==> Сборка APK"
aapt2 link -o "$BUILD_DIR/unsigned.apk" \
  --manifest "$APP_DIR/AndroidManifest.xml" \
  -I "$ANDROID_JAR" \
  --min-sdk-version 24 --target-sdk-version 34 \
  --version-code 1 --version-name 1.0

BUILD_ABS="$(pwd)/$BUILD_DIR"
cp "$BUILD_DIR/unsigned.apk" "$BUILD_DIR/app.apk"
(cd "$DEX_DIR" && zip -q "$BUILD_ABS/app.apk" classes.dex)
(cd "$BUILD_DIR" && zip -q -r app.apk lib)

zipalign -f 4 "$BUILD_DIR/app.apk" "$BUILD_DIR/aligned.apk"

if [ ! -f "$BUILD_DIR/debug.keystore" ]; then
  keytool -genkeypair -keystore "$BUILD_DIR/debug.keystore" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi

apksigner sign --ks "$BUILD_DIR/debug.keystore" --ks-pass pass:android --key-pass pass:android \
  --out "$APK_OUT" "$BUILD_DIR/aligned.apk"

echo "==> Готово: $APK_OUT"
ls -lh "$APK_OUT"
unzip -l "$APK_OUT" | sed -n '1,20p'
