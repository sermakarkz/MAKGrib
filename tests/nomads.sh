#!/bin/bash
# Проверка запасного пути через NOAA NOMADS: сборка GFS и GFS-Wave
# напрямую с сервера NOAA и чтение результата штатным ридером XyGrib.
#
# Запуск: ./tests/nomads.sh [папка-для-файлов]
# Требует сети и уже собранного проекта (./rebuild.sh).

set -e
cd "$(dirname "$0")/.."
SRC="$PWD"
DEST="${1:-$SRC/build/nomads}"

if [ ! -d build/src ]; then
  echo "Сначала соберите проект: ./rebuild.sh"; exit 1
fi
mkdir -p "$DEST"

SDK="$(xcrun --show-sdk-path)"
QT="$(brew --prefix qt@5)"
OUT="$SRC/build/nomadstest"

cd build/src
# Линкуем те же объектники, что и приложение, кроме его main().
OBJS=()
for o in CMakeFiles/MAKGrib.dir/*.o; do
  case "$o" in *main.cpp.o) continue;; esac
  OBJS+=("$o")
done

/usr/bin/c++ -std=gnu++11 -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -arch arm64 \
  -I"$SRC/src" -I"$SRC/src/util" -I"$SRC/src/map" -I"$SRC/src/GUI" -I"$SRC/src/g2clib-1.6.0" \
  -I. -I./GUI -I./map -I./util -F"$QT/lib" \
  -I"$QT/lib/QtCore.framework/Headers"    -I"$QT/lib/QtGui.framework/Headers" \
  -I"$QT/lib/QtWidgets.framework/Headers" -I"$QT/lib/QtNetwork.framework/Headers" \
  -I"$QT/lib/QtXml.framework/Headers"     -I"$QT/lib/QtPrintSupport.framework/Headers" \
  -I"$(brew --prefix libnova)/include" -I"$(brew --prefix proj)/include" \
  -I"$(brew --prefix openjpeg)/include/openjpeg-2.5" -I/opt/homebrew/include \
  "$SRC/tests/nomadstest.cpp" "${OBJS[@]}" \
  g2clib-1.6.0/libg2clib.a GUI/libgui.a util/libutil.a map/libmap.a \
  "$(brew --prefix libnova)/lib/libnova.a" \
  "$(brew --prefix openjpeg)/lib/libopenjp2.dylib" \
  "$(brew --prefix proj)/lib/libproj.dylib" /opt/homebrew/lib/libpng.dylib -lbz2 -lz \
  -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork \
  -framework QtXml -framework QtPrintSupport \
  -o "$OUT"

cd "$SRC/build"
QT_QPA_PLATFORM=offscreen "$OUT" "$DEST" 2>&1 | grep -v "^qt\."
