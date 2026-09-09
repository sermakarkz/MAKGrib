#!/bin/bash
# Кросс-сборка нативных библиотек MAKGrib под Android.
#
#   ./android-deps.sh arm64-v8a     — для телефонов
#   ./android-deps.sh x86_64        — для эмулятора
#
# PROJ и SQLite сюда не входят намеренно: все проекции, кроме
# Projection_ZYGRIB, тянут за собой PROJ, а тот — SQLite и базу CRS.
# На телефоне достаточно Projection_ZYGRIB, это чистая арифметика.
# zlib тоже не собираем — она есть в самой Android.
set -e

ABI="${1:-arm64-v8a}"
API=28                       # Android 9: минимум, который умеет Qt 6.8
NDK="${ANDROID_NDK_ROOT:?не задан ANDROID_NDK_ROOT}"
OUT="$HOME/android-deps/$ABI"
WORK="/tmp/android-deps-$ABI"

TOOLS="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
# Дешёвые телефоны до сих пор приходят с 32-битной системой, даже когда
# железо 64-битное: Redmi 9A — ровно такой случай. Поэтому armeabi-v7a
# нужен по-настоящему, а не для галочки.
case "$ABI" in
  arm64-v8a)   TRIPLE=aarch64-linux-android ;;
  armeabi-v7a) TRIPLE=armv7a-linux-androideabi ;;
  x86_64)      TRIPLE=x86_64-linux-android ;;
  *) echo "неизвестная архитектура: $ABI"; exit 1 ;;
esac

# configure ждёт классическую тройку, а компилятор у NDK назван с
# уровнем API в имени.
CONFIG_HOST="$TRIPLE"
[ "$ABI" = "armeabi-v7a" ] && CONFIG_HOST=arm-linux-androideabi

export CC="$TOOLS/${TRIPLE}${API}-clang"
export CXX="$TOOLS/${TRIPLE}${API}-clang++"
export AR="$TOOLS/llvm-ar"
export RANLIB="$TOOLS/llvm-ranlib"
export STRIP="$TOOLS/llvm-strip"
export CFLAGS="-fPIC -O2"
export CXXFLAGS="-fPIC -O2"

mkdir -p "$OUT" "$WORK"
cd "$WORK"

fetch () {           # fetch <url> <каталог-после-распаковки>
  local url="$1" dir="$2" file="${1##*/}"
  [ -d "$dir" ] && return 0
  echo "  качаю $file"
  curl -sSL -o "$file" "$url"
  case "$file" in
    *.tar.gz|*.tgz) tar xzf "$file" ;;
    *.tar.xz)       tar xJf "$file" ;;
    *.zip)          unzip -q "$file" ;;
  esac
}

echo "== bzip2 =="
if [ ! -f "$OUT/lib/libbz2.a" ]; then
  fetch https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz bzip2-1.0.8
  cd bzip2-1.0.8
  make -s clean 2>/dev/null || true
  make -s CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="$CFLAGS -D_FILE_OFFSET_BITS=64" libbz2.a
  mkdir -p "$OUT/lib" "$OUT/include"
  cp libbz2.a "$OUT/lib/"; cp bzlib.h "$OUT/include/"
  cd "$WORK"
fi
echo "  $(ls -la $OUT/lib/libbz2.a | awk '{print $5}') байт"

echo "== libpng =="
if [ ! -f "$OUT/lib/libpng16.a" ]; then
  fetch https://download.sourceforge.net/libpng/libpng-1.6.43.tar.xz libpng-1.6.43
  cd libpng-1.6.43
  ./configure --host="$CONFIG_HOST" --prefix="$OUT" \
              --enable-static --disable-shared --with-pic >/dev/null
  make -s -j"$(( $(nproc) / 2 ))" >/dev/null && make -s install >/dev/null
  cd "$WORK"
fi
echo "  $(ls -la $OUT/lib/libpng16.a | awk '{print $5}') байт"

echo "== openjpeg =="
if [ ! -f "$OUT/lib/libopenjp2.a" ]; then
  fetch https://github.com/uclouvain/openjpeg/archive/refs/tags/v2.5.2.tar.gz openjpeg-2.5.2
  cd openjpeg-2.5.2
  rm -rf b && mkdir b && cd b
  cmake .. -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$API" \
        -DCMAKE_INSTALL_PREFIX="$OUT" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CODEC=OFF >/dev/null
  make -s -j"$(( $(nproc) / 2 ))" >/dev/null && make -s install >/dev/null
  cd "$WORK"
fi
echo "  $(ls -la $OUT/lib/libopenjp2.a | awk '{print $5}') байт"

echo "== libnova =="
if [ ! -f "$OUT/lib/libnova.a" ]; then
  fetch https://deb.debian.org/debian/pool/main/libn/libnova/libnova_0.16.orig.tar.xz libnova
  cd libnova
  # Те же две правки, что и для Windows: 32-битный time_t объявлен
  # вручную, а gmtime ждёт настоящий time_t.
  sed -i '/#define _USE_32BIT_TIME_T/d' src/julian_day.c lntest/test.c 2>/dev/null || true
  sed -i 's/gmt = gmtime(&tv\.tv_sec);/{ time_t _t = (time_t) tv.tv_sec; gmt = gmtime(\&_t); }/' \
      src/julian_day.c
  NOCONFIGURE=1 ./autogen.sh >/dev/null 2>&1 || autoreconf -fi >/dev/null 2>&1
  ./configure --host="$CONFIG_HOST" --prefix="$OUT" \
              --enable-static --disable-shared --with-pic >/dev/null
  make -s -j"$(( $(nproc) / 2 ))" >/dev/null && make -s install >/dev/null
  cd "$WORK"
fi
echo "  $(ls -la $OUT/lib/libnova.a | awk '{print $5}') байт"

echo
echo "== готово: $OUT =="
ls -la "$OUT/lib"/*.a | awk '{printf "  %-22s %8d байт\n", $NF, $5}'
echo "  проверка архитектуры:"
"$TOOLS/llvm-readelf" -h "$OUT/lib/libbz2.a" 2>/dev/null | grep -m1 Machine | sed 's/^/    /'
