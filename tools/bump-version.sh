#!/bin/bash
# Поднять версию телефонного приложения: ./tools/bump-version.sh 1.2.0
#
# Номер хранится в манифесте и только там: оттуда его берёт и сборка, и
# сам код для проверки обновлений. Порядковый номер версии (versionCode)
# Android сравнивает при обновлении — он обязан расти.
set -e
[ -n "$1" ] || { echo "укажите версию, например 1.2.0"; exit 1; }
NAME=$1
cd "$(dirname "$0")/.."
M=android/package/AndroidManifest.xml

OLD_CODE=$(grep -oE 'android:versionCode="[0-9]+"' $M | grep -oE '[0-9]+')
# Номер версии складываем из её частей: 1.2.0 -> 120, 1.2.3 -> 123.
NEW_CODE=$(echo "$NAME" | awk -F. '{printf "%d%d%d", $1, $2, $3}')
[ "$NEW_CODE" -gt "$OLD_CODE" ] || { echo "версия $NAME не выше нынешней ($OLD_CODE)"; exit 1; }

sed -i.bak -E "s/android:versionCode=\"[0-9]+\"/android:versionCode=\"$NEW_CODE\"/; s/android:versionName=\"[^\"]+\"/android:versionName=\"$NAME\"/" $M
rm -f $M.bak

# Страница с описанием последней версии — её читает приложение.
B=https://github.com/sermakarkz/MAKGrib/releases/download/v$NAME
cat > docs/latest.json <<JSON
{
  "versionCode": $NEW_CODE,
  "versionName": "$NAME",
  "page": "https://github.com/sermakarkz/MAKGrib/releases/latest",
  "arm64": "$B/MAKGrib-$NAME-arm64-v8a.apk",
  "arm":   "$B/MAKGrib-$NAME-armeabi-v7a.apk"
}
JSON
echo "версия $NAME ($NEW_CODE) записана в манифест и в docs/latest.json"
