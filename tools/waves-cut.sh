#!/bin/bash
# Волнение для замкнутых морей: у NOAA их нет вовсе, у немецкой службы
# погоды (DWD) есть. Скрипт забирает модель GWAM, режет по морям и
# складывает готовые кусочки в out/.
#
# Запускается заданием на GitHub дважды в сутки — своего сервера для
# этого не нужно. Проверено: 38 МБ на входе, 136 КБ на Каспий на выходе.
set -e
export LC_ALL=C          # дробные числа только с точкой: на русской
                         # локали разбор ломается и 9999 читается как 9

BASE=https://opendata.dwd.de/weather/maritime/wave_models/gwam/grib
WORK=${WORK:-/tmp/gwam}
OUT=${OUT:-out}
STEPS=${STEPS:-$(seq -w 0 3 72)}
PARAMS=${PARAMS:-"swh mwd tm10"}      # высота, направление, период

rm -rf "$WORK" "$OUT"; mkdir -p "$WORK" "$OUT"

# Последний выложенный выпуск: сегодняшний 00 или 12, иначе вчерашний.
D=""; C=""
for try in "$(date -u +%Y%m%d) 12" "$(date -u +%Y%m%d) 00" \
           "$(date -u -d yesterday +%Y%m%d) 12" "$(date -u -d yesterday +%Y%m%d) 00"; do
  set -- $try
  if curl -sfI --max-time 30 "$BASE/$2/swh/GWAM_SWH_$1$2_000.grib2.bz2" >/dev/null; then
    D=$1; C=$2; break
  fi
done
[ -n "$D" ] || { echo "у DWD нет свежего выпуска"; exit 1; }
echo "выпуск GWAM: $D $C UTC"

got=0
for p in $PARAMS; do
  P=$(echo "$p" | tr a-z A-Z)
  for s in $STEPS; do
    f=GWAM_${P}_${D}${C}_$(printf %03d $((10#$s))).grib2.bz2
    if curl -sf --max-time 90 -o "$WORK/$f" "$BASE/$C/$p/$f"; then
      bunzip2 -f "$WORK/$f"; got=$((got+1))
    fi
  done
done
echo "скачано файлов: $got"
[ "$got" -gt 0 ] || { echo "ничего не скачалось"; exit 1; }

# Море: имя, запад, восток, юг, север.
SEAS=${SEAS:-"caspian:47:55:36:48 black:27:42:40:48 azov:34:40:45:48"}

for sea in $SEAS; do
  IFS=: read -r name w e s n <<< "$sea"
  : > "$OUT/$name.grb2"
  for f in $(ls "$WORK"/*.grib2 | sort); do
    # cdo пишет заголовок, где порядок широт спорит с флагом обхода:
    # такой файл не читается ни eccodes, ни нашим движком. Флаг правим.
    cdo -s -sellonlatbox,"$w","$e","$s","$n" "$f" "$WORK/raw.grb2" 2>/dev/null || continue
    grib_set -s jScansPositively=0 "$WORK/raw.grb2" "$WORK/piece.grb2" 2>/dev/null || continue
    cat "$WORK/piece.grb2" >> "$OUT/$name.grb2"
  done
  printf "%-10s %6d КБ, сообщений %s\n" "$name" \
         $(( $(stat -c%s "$OUT/$name.grb2") / 1024 )) \
         "$(grib_count "$OUT/$name.grb2" 2>/dev/null || echo ?)"
done

# Опись для приложения: какие моря есть, где их границы и насколько свежо.
{
  echo "{"
  echo "  \"model\": \"DWD GWAM\","
  echo "  \"run\": \"${D}T${C}:00Z\","
  echo "  \"updated\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
  echo "  \"seas\": ["
  first=1
  for sea in $SEAS; do
    IFS=: read -r name w e s n <<< "$sea"
    [ $first -eq 1 ] || echo ","
    first=0
    printf '    {"name": "%s", "west": %s, "east": %s, "south": %s, "north": %s, "file": "%s.grb2"}' \
           "$name" "$w" "$e" "$s" "$n" "$name"
  done
  echo; echo "  ]"; echo "}"
} > "$OUT/waves.json"
cat "$OUT/waves.json"
