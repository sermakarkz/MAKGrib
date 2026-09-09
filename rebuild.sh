#!/bin/bash
# Пересборка MAKGrib нативно под Apple Silicon (arm64).
# Запуск: ./rebuild.sh          — обычная пересборка (быстрая, инкрементальная)
#         ./rebuild.sh clean    — с нуля
#
# Результат кладётся в /Users/sergey/Documents/MAKGrib-native/MAKGrib.app
# Рядом с .app обязана лежать папка data — программа ищет её на три уровня
# выше своего бинарника (src/main.cpp:57).

set -e
cd "$(dirname "$0")"

SRC="$PWD"
DEST="/Users/sergey/Documents/MAKGrib-native"
SDK="$(xcrun --show-sdk-path)"
QT="$(brew --prefix qt@5)"

[ "$1" = "clean" ] && rm -rf build

# ── конфигурация ──────────────────────────────────────────
# Четыре нештатных флага, без которых сборка не идёт:
#   POLICY_VERSION_MINIMUM — cmake 4 не принимает cmake_minimum_required(3.1)
#   OSX_SYSROOT            — иначе не подставляется путь к SDK
#   CXX_FLAGS -isystem     — в Command Line Tools нет своих заголовков libc++,
#                            берём их из SDK
#   OPENJPEG_*             — в CMakeLists зашиты только суффиксы до 2.3,
#                            а Homebrew ставит 2.5
if [ ! -d build ]; then
  mkdir -p build && cd build
  cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_PREFIX_PATH="$QT;$(brew --prefix proj);$(brew --prefix libnova)" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT="$SDK" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DCMAKE_CXX_FLAGS="-isystem $SDK/usr/include/c++/v1" \
    -DOPENJPEG_INCLUDE_DIR="$(brew --prefix openjpeg)/include/openjpeg-2.5" \
    -DOPENJPEG_LIBRARIES="$(brew --prefix openjpeg)/lib/libopenjp2.dylib"
  cd "$SRC"
fi

# ── сборка ────────────────────────────────────────────────
cmake --build build -j "$(sysctl -n hw.ncpu)"

# ── упаковка ──────────────────────────────────────────────
pkill -f "MAKGrib-native" 2>/dev/null || true
mkdir -p "$DEST"
rm -rf "$DEST/MAKGrib.app"
cp -R build/src/MAKGrib.app "$DEST/"
# Иконка в бандл: CMake её не кладёт, это делали скрипты упаковки.
mkdir -p "$DEST/MAKGrib.app/Contents/Resources"
cp data/img/makgrib.icns "$DEST/MAKGrib.app/Contents/Resources/"

# Папка данных живёт в Application Support, а не рядом с .app: на macOS
# чтение из ~/Documents требует разрешения TCC, и без него карта пустая.
# Карты берём там же, где их держал XyGrib, чтобы не качать заново.
DATA="$HOME/Library/Application Support/MAKGrib/MAKGrib"
OLDDATA="$HOME/Library/Application Support/openGribs/XyGrib"
mkdir -p "$DATA/data/maps/gshhs"
# Карты высокого разрешения от XyGrib переносим один раз.
[ -d "$OLDDATA/data/maps" ] && rsync -a --ignore-existing "$OLDDATA/data/maps/" "$DATA/data/maps/"
# Всё кроме карт синхронизируем всегда, иначе правки переводов и палитр
# не доедут до работающей программы.
rsync -a --delete --exclude 'maps' data/ "$DATA/data/"
# Карты — только недостающие: в $DATA лежат файлы высокого разрешения,
# которых нет в исходниках, и --delete их бы снёс.
rsync -a --ignore-existing data/maps/ "$DATA/data/maps/"

# Программа запоминает папку данных в настройках (appDataDir) и папку рядом
# с .app при этом игнорирует. Без этой строчки правки переводов и палитр
# просто не доедут до работающей программы.
INI="$HOME/Library/Preferences/makgrib.ini"
if [ -f "$INI" ] && ! grep -qF "appDataDir=$DATA" "$INI"; then
  /usr/bin/sed -i '' "s|^appDataDir=.*|appDataDir=$DATA|" "$INI"
  grep -q "^appDataDir=" "$INI" || \
    /usr/bin/sed -i '' "1a\\
appDataDir=$DATA
" "$INI"
  echo "  настройка appDataDir переведена на $DATA"
fi

"$QT/bin/macdeployqt" "$DEST/MAKGrib.app" >/dev/null 2>&1

# macdeployqt тянет не все зависимости: libwebp тащит за собой libsharpyuv,
# а её он не кладёт и переписывает ссылку на несуществующий ../lib —
# приложение падает на старте с "Library not loaded". Дотягиваем сами:
# обходим бандл, ищем неразрешённые @rpath, копируем и правим ссылки,
# и так по кругу, пока новых не останется.
FW="$DEST/MAKGrib.app/Contents/Frameworks"
for pass in 1 2 3 4 5; do
  missing=0
  while IFS= read -r bin; do
    otool -L "$bin" 2>/dev/null | awk 'NR>1{print $1}' | grep '^@rpath/' | while read -r ref; do
      lib="${ref#@rpath/}"
      [ -f "$FW/$lib" ] && continue
      src=""
      for cand in /opt/homebrew/lib/"$lib" /opt/homebrew/opt/*/lib/"$lib"; do
        [ -f "$cand" ] && src="$cand" && break
      done
      [ -z "$src" ] && { echo "  не найдена зависимость: $lib"; continue; }
      cp -f "$src" "$FW/$lib"
      chmod u+w "$FW/$lib"
      install_name_tool -id "@executable_path/../Frameworks/$lib" "$FW/$lib" 2>/dev/null
      echo "  добавлено: $lib"
    done
  done < <(find "$DEST/MAKGrib.app/Contents/Frameworks" "$DEST/MAKGrib.app/Contents/MacOS" \
                -type f \( -name '*.dylib' -o -perm -u+x \) 2>/dev/null)

  # Переписываем все @rpath-ссылки на путь внутри бандла.
  while IFS= read -r bin; do
    chmod u+w "$bin" 2>/dev/null
    otool -L "$bin" 2>/dev/null | awk 'NR>1{print $1}' | grep '^@rpath/' | while read -r ref; do
      lib="${ref#@rpath/}"
      [ -f "$FW/$lib" ] || continue
      install_name_tool -change "$ref" "@executable_path/../Frameworks/$lib" "$bin" 2>/dev/null
    done
  done < <(find "$DEST/MAKGrib.app/Contents/Frameworks" "$DEST/MAKGrib.app/Contents/MacOS" \
                -type f \( -name '*.dylib' -o -perm -u+x \) 2>/dev/null)

  # Ещё остались неразрешённые?
  while IFS= read -r bin; do
    otool -L "$bin" 2>/dev/null | awk 'NR>1{print $1}' | grep '^@rpath/' | while read -r ref; do
      [ -f "$FW/${ref#@rpath/}" ] || echo x
    done
  done < <(find "$DEST/MAKGrib.app/Contents/Frameworks" -type f -name '*.dylib' 2>/dev/null) | grep -q x && missing=1
  [ "$missing" = 0 ] && break
done
# arm64 требует подписи, хотя бы ad-hoc, иначе macOS не запустит
codesign --force --deep --sign - "$DEST/MAKGrib.app" 2>/dev/null

echo
echo "Готово: $DEST/MAKGrib.app"
lipo -archs "$DEST/MAKGrib.app/Contents/MacOS/MAKGrib" | sed 's/^/  архитектура: /'
echo "  запуск: open $DEST/MAKGrib.app"
