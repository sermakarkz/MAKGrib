#!/bin/bash
# Проверка виртуального судна без графики: геодезия, счисление,
# сохранение маршрута, трек по реальному GRIB, сборка окон.
#
# Запуск: ./tests/run.sh [путь-к-grib] [ещё-grib ...]
# Требует, чтобы проект был уже собран (./rebuild.sh).

set -e
cd "$(dirname "$0")/.."
SRC="$PWD"
GRIB="${1:-/Users/sergey/Documents/zyGrib_mac-8.0.1/grib/Caspian_20260816_06z_GFS.grb}"

if [ ! -d build/src ]; then
  echo "Сначала соберите проект: ./rebuild.sh"; exit 1
fi
if [ ! -f "$GRIB" ]; then
  echo "GRIB не найден: $GRIB"; exit 1
fi

SDK="$(xcrun --show-sdk-path)"
QT="$(brew --prefix qt@5)"
OUT="$SRC/build/boattest"

cd build/src
# Линкуем те же объектники, что и приложение, кроме его main().
# Ищем и во вложенных каталогах: исходники есть не только в src/.
OBJS=()
while IFS= read -r o; do
  case "$o" in *main.cpp.o) continue;; esac
  OBJS+=("$o")
done < <(find CMakeFiles/MAKGrib.dir -name '*.o' | sort)

/usr/bin/c++ -std=gnu++11 -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -arch arm64 \
  -I"$SRC/src" -I"$SRC/src/util" -I"$SRC/src/map" -I"$SRC/src/GUI" \
  -I"$SRC/src/forecast" -I"$SRC/src/g2clib-1.6.0" \
  -I. -I./GUI -I./map -I./util -F"$QT/lib" \
  -I"$QT/lib/QtCore.framework/Headers"    -I"$QT/lib/QtGui.framework/Headers" \
  -I"$QT/lib/QtWidgets.framework/Headers" -I"$QT/lib/QtNetwork.framework/Headers" \
  -I"$QT/lib/QtXml.framework/Headers"     -I"$QT/lib/QtPrintSupport.framework/Headers" \
  -I"$(brew --prefix libnova)/include" -I"$(brew --prefix proj)/include" \
  -I"$(brew --prefix openjpeg)/include/openjpeg-2.5" -I/opt/homebrew/include \
  "$SRC/tests/boattest.cpp" "${OBJS[@]}" \
  g2clib-1.6.0/libg2clib.a GUI/libgui.a util/libutil.a map/libmap.a \
  "$(brew --prefix libnova)/lib/libnova.a" \
  "$(brew --prefix openjpeg)/lib/libopenjp2.dylib" \
  "$(brew --prefix proj)/lib/libproj.dylib" /opt/homebrew/lib/libpng.dylib -lbz2 -lz \
  -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork \
  -framework QtXml -framework QtPrintSupport \
  -o "$OUT"

# Тест проверяет сохранение маршрута, а значит пишет в настоящий xygrib.ini
# и затирает маршрут пользователя. Подмена HOME на macOS не помогает —
# Qt берёт домашнюю папку у системы, а не из окружения. Поэтому просто
# снимаем копию настроек и возвращаем её в любом случае, включая Ctrl-C.
INI="$HOME/Library/Preferences/xygrib.ini"
BACKUP="$SRC/build/xygrib.ini.testbackup"
if [ -f "$INI" ]; then
  cp "$INI" "$BACKUP"
  trap 'cp "$BACKUP" "$INI"; echo; echo "настройки восстановлены из копии"' EXIT INT TERM
fi

# Программа ищет данные по appDataDir из настроек, каталог запуска не важен.
cd "$SRC/build"
# Дополнительные файлы — для проверки нескольких прогнозов сразу.
EXTRA=()
for f in "$@"; do [ -f "$f" ] && [ "$f" != "$GRIB" ] && EXTRA+=("$f"); done

QT_QPA_PLATFORM=offscreen "$OUT" "$GRIB" "$SRC/build" "${EXTRA[@]}" 2>&1 | grep -v "^qt\."
BOAT=${PIPESTATUS[0]}

# Проекция Меркатора без PROJ — отдельная проверка: под Android PROJ не
# собирается, и вся навигационная карта держится на этом классе.
cd "$SRC/build/src"
/usr/bin/c++ -std=gnu++11 -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -arch arm64 \
  -I"$SRC/src" -I"$SRC/src/util" -I"$SRC/src/map" -I"$SRC/src/GUI" \
  -I"$SRC/src/forecast" -I"$SRC/src/g2clib-1.6.0" \
  -I. -I./GUI -I./map -I./util -F"$QT/lib" \
  -I"$QT/lib/QtCore.framework/Headers"    -I"$QT/lib/QtGui.framework/Headers" \
  -I"$QT/lib/QtWidgets.framework/Headers" -I"$QT/lib/QtNetwork.framework/Headers" \
  -I"$QT/lib/QtXml.framework/Headers"     -I"$QT/lib/QtPrintSupport.framework/Headers" \
  -I"$(brew --prefix libnova)/include" -I"$(brew --prefix proj)/include" \
  -I"$(brew --prefix openjpeg)/include/openjpeg-2.5" -I/opt/homebrew/include \
  "$SRC/tests/mercatortest.cpp" "${OBJS[@]}" \
  g2clib-1.6.0/libg2clib.a GUI/libgui.a util/libutil.a map/libmap.a \
  "$(brew --prefix libnova)/lib/libnova.a" \
  "$(brew --prefix openjpeg)/lib/libopenjp2.dylib" \
  "$(brew --prefix proj)/lib/libproj.dylib" /opt/homebrew/lib/libpng.dylib -lbz2 -lz \
  -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork \
  -framework QtXml -framework QtPrintSupport \
  -o "$SRC/build/mercatortest"
cd "$SRC/build"
echo
echo "=== Проекция Меркатора без PROJ ==="
QT_QPA_PLATFORM=offscreen "$SRC/build/mercatortest" 2>&1 | grep -v "^qt\."
MERC=${PIPESTATUS[0]}

# Склейка плиток: прогноз, нарезанный квадратами, должен читаться так же,
# как тот же участок одним куском. Своего сервера, который резал бы под
# экран, у нас нет — квадраты нарезаны заранее и сходятся уже у читателя.
TILE=0
if [ -f "$SRC/tests/data/tiles.grb2" ]; then
  cd "$SRC/build/src"
  /usr/bin/c++ -std=gnu++11 -isysroot "$SDK" -isystem "$SDK/usr/include/c++/v1" -arch arm64 \
    -I"$SRC/src" -I"$SRC/src/util" -I"$SRC/src/map" -I"$SRC/src/GUI" \
    -I"$SRC/src/forecast" -I"$SRC/src/g2clib-1.6.0" \
    -I. -I./GUI -I./map -I./util -F"$QT/lib" \
    -I"$QT/lib/QtCore.framework/Headers"    -I"$QT/lib/QtGui.framework/Headers" \
    -I"$QT/lib/QtWidgets.framework/Headers" -I"$QT/lib/QtNetwork.framework/Headers" \
    -I"$QT/lib/QtXml.framework/Headers"     -I"$QT/lib/QtPrintSupport.framework/Headers" \
    -I"$(brew --prefix libnova)/include" -I"$(brew --prefix proj)/include" \
    -I"$(brew --prefix openjpeg)/include/openjpeg-2.5" -I/opt/homebrew/include \
    "$SRC/tests/tiletest.cpp" "${OBJS[@]}" \
    g2clib-1.6.0/libg2clib.a GUI/libgui.a util/libutil.a map/libmap.a \
    "$(brew --prefix libnova)/lib/libnova.a" \
    "$(brew --prefix openjpeg)/lib/libopenjp2.dylib" \
    "$(brew --prefix proj)/lib/libproj.dylib" /opt/homebrew/lib/libpng.dylib -lbz2 -lz \
    -framework QtCore -framework QtGui -framework QtWidgets -framework QtNetwork \
    -framework QtXml -framework QtPrintSupport \
    -o "$SRC/build/tiletest"
  cd "$SRC"
  echo
  echo "=== Склейка плиток прогноза ==="
  QT_QPA_PLATFORM=offscreen "$SRC/build/tiletest" "$SRC/tests/data" 2>&1 | grep -v "^qt\."
  TILE=${PIPESTATUS[0]}
else
  echo
  echo "=== Склейка плиток: образцов нет, пропущено ==="
  echo "    сделать: python3 tests/maketiles.py"
fi

# Год для даты выхода: Qt здесь не нужен, правило чистое.
echo
echo "=== Год маршрута через Новый год ==="
c++ -std=c++17 -o "$SRC/build/routetest" "$SRC/tests/routetest.cpp"
"$SRC/build/routetest"
ROUTE=$?

exit $(( BOAT + MERC + TILE + ROUTE ))
