# Общее окружение для сборки Android-приложения roSkos.
# Использование: . scripts/android-env.sh

export JAVA_HOME="${JAVA_HOME:-$HOME/toolchain/jdk}"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/26.1.10909125}"
export PATH="$HOME/.cargo/bin:$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/build-tools/34.0.0:$PATH"
export CARGO="$HOME/.cargo/bin/cargo"
