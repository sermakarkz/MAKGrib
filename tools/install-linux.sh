#!/bin/bash
# Установка MAKGrib для текущего пользователя: программа в ~/Applications,
# запись в меню приложений и ярлык на рабочем столе. Без sudo — всё в
# домашней папке, удаляется так же просто.
set -e
APPIMAGE="$1"
[ -f "$APPIMAGE" ] || { echo "нет файла: $APPIMAGE"; exit 1; }

BIN="$HOME/Applications"
ICONS="$HOME/.local/share/icons/hicolor/256x256/apps"
APPS="$HOME/.local/share/applications"
mkdir -p "$BIN" "$ICONS" "$APPS"

cp -f "$APPIMAGE" "$BIN/MAKGrib.AppImage"
chmod +x "$BIN/MAKGrib.AppImage"

# Иконку достаём из самого AppImage, чтобы не таскать её отдельно.
cd /tmp && rm -rf squashfs-root
"$BIN/MAKGrib.AppImage" --appimage-extract >/dev/null 2>&1
cp -f /tmp/squashfs-root/usr/share/icons/hicolor/256x256/apps/makgrib.png \
      "$ICONS/makgrib.png"
rm -rf /tmp/squashfs-root
gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

DESKTOP="$APPS/makgrib.desktop"
cat > "$DESKTOP" <<DESK
[Desktop Entry]
Type=Application
Version=1.0
Name=MAKGrib
GenericName=Просмотр прогнозов GRIB
Comment=Морские прогнозы погоды и прокладка маршрута
Exec=$BIN/MAKGrib.AppImage %f
Icon=makgrib
Terminal=false
Categories=Education;Science;
Keywords=grib;weather;marine;погода;прогноз;море;
StartupNotify=true
DESK
chmod +x "$DESKTOP"
update-desktop-database "$APPS" 2>/dev/null || true

# Рабочий стол: у GNOME он называется по-русски, спрашиваем систему.
DESKDIR="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
mkdir -p "$DESKDIR"
cp -f "$DESKTOP" "$DESKDIR/makgrib.desktop"
chmod +x "$DESKDIR/makgrib.desktop"
# Без этой отметки GNOME показывает ярлык как текстовый файл и не даёт
# его запустить.
gio set "$DESKDIR/makgrib.desktop" metadata::trusted true 2>/dev/null || true

echo "программа : $BIN/MAKGrib.AppImage"
echo "в меню    : $DESKTOP"
echo "на столе  : $DESKDIR/makgrib.desktop"
