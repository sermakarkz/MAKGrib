#!/usr/bin/env python3
# Готовит образцы для проверки склейки плиток (tests/tiletest.cpp).
#
# Раздавать прогноз кусками приходится потому, что резать под экран, как
# NOAA, некому: своего сервера нет. Куски нарезаны заранее квадратами, а
# сходятся в одну сетку уже на телефоне — вот эту сходимость и проверяем.
#
# Берёт настоящее поле ICON-EU и делает из одного и того же участка два
# файла: цельный и разрезанный на квадраты по 5°. Правильная склейка
# обязана дать из второго ровно то же, что лежит в первом.
#
# Запуск:  python3 tests/maketiles.py       (нужны eccodes и сеть)
import os, sys, subprocess, urllib.request
import numpy as np
import eccodes as ec

BASE = "https://opendata.dwd.de/weather/nwp/icon-eu/grib"
HERE = os.path.dirname (os.path.abspath (__file__))
OUT  = os.path.join (HERE, "data")

# Каспий и вокруг: четыре квадрата по 5° в углу 40°в.д., 35°с.ш.
TILES = [(40, 35), (45, 35), (40, 40), (45, 40)]
WHOLE = (40, 50, 35, 45)          # тот же участок одним куском
HOLE  = TILES[:3]                 # без северо-восточного — проверка дырки


def latest_run ():
    """Самый свежий выпуск — по тому, что лежит в каталоге.

    Гадать по часам нельзя: выкладывают с задержкой, да и часы машины
    могут уйти вперёд относительно сервера.
    """
    import re
    best = None
    for c in ("21","18","15","12","09","06","03","00"):
        try:
            with urllib.request.urlopen (f"{BASE}/{c}/v_10m/", timeout=30) as r:
                page = r.read().decode ("utf-8", "replace")
        except Exception:
            continue
        for m in re.finditer (r"single-level_(\d{8})(\d{2})_000_V_10M", page):
            key = (m.group(1), m.group(2))
            if best is None or key > best:
                best = key
    if best is None:
        sys.exit ("свежего выпуска ICON-EU не нашлось")
    return best


def fetch (d, c, step):
    name = f"icon-eu_europe_regular-lat-lon_single-level_{d}{c}_{step:03d}_V_10M.grib2"
    path = os.path.join (OUT, name)
    if not os.path.exists (path):
        url = f"{BASE}/{c}/v_10m/{name}.bz2"
        print ("качаю", url)
        urllib.request.urlretrieve (url, path + ".bz2")
        subprocess.check_call (["bunzip2", "-f", path + ".bz2"])
    return path


def cut (src, out, w, e, s, n):
    """Вырезать прямоугольник и дописать его в out."""
    with open (src, "rb") as f:
        h = ec.codes_grib_new_from_file (f)
        Ni = ec.codes_get (h, "Ni"); Nj = ec.codes_get (h, "Nj")
        lo1 = ec.codes_get (h, "longitudeOfFirstGridPointInDegrees")
        la1 = ec.codes_get (h, "latitudeOfFirstGridPointInDegrees")
        di  = ec.codes_get (h, "iDirectionIncrementInDegrees")
        dj  = ec.codes_get (h, "jDirectionIncrementInDegrees")
        jpos = ec.codes_get (h, "jScansPositively")
        v = ec.codes_get_values (h).reshape (Nj, Ni)
        lons = (lo1 + np.arange (Ni)*di + 180) % 360 - 180
        lats = la1 + np.arange (Nj)*dj*(1 if jpos else -1)
        ii = np.where ((lons >= w) & (lons <= e))[0]
        jj = np.where ((lats >= s) & (lats <= n))[0]
        sub = v[np.ix_(jj, ii)]
        h2 = ec.codes_clone (h)
        # ICON-EU приходит упакованной по CCSDS (шаблон 5.42), а движок
        # такого не умеет: g2clib на нём говорит «DRS Template 5.42 not
        # defined» и запись пропадает. Перекладываем в простую упаковку.
        # Даром: при десяти битах файл выходит даже меньше исходного, а
        # ошибка в сотые доли метра в секунду прогнозу безразлична.
        ec.codes_set (h2, "packingType", "grid_simple")
        ec.codes_set (h2, "bitsPerValue", 10)
        ec.codes_set (h2, "Ni", len(ii)); ec.codes_set (h2, "Nj", len(jj))
        ec.codes_set (h2, "longitudeOfFirstGridPointInDegrees", float(lons[ii[0]]) % 360)
        ec.codes_set (h2, "longitudeOfLastGridPointInDegrees",  float(lons[ii[-1]]) % 360)
        ec.codes_set (h2, "latitudeOfFirstGridPointInDegrees",  float(lats[jj[0]]))
        ec.codes_set (h2, "latitudeOfLastGridPointInDegrees",   float(lats[jj[-1]]))
        ec.codes_set_values (h2, sub.flatten())
        with open (out, "ab") as o:
            ec.codes_write (h2, o)
        ec.codes_release (h2); ec.codes_release (h)
    return len(ii), len(jj)


def main ():
    os.makedirs (OUT, exist_ok=True)
    d, c = latest_run ()
    print (f"выпуск ICON-EU: {d} {c} UTC")
    srcs = [fetch (d, c, s) for s in (0, 3)]

    for name in ("whole.grb2", "tiles.grb2", "tiles-hole.grb2"):
        p = os.path.join (OUT, name)
        if os.path.exists (p):
            os.remove (p)

    for src in srcs:
        cut (src, os.path.join (OUT, "whole.grb2"), *WHOLE)
        for (w, s) in TILES:
            cut (src, os.path.join (OUT, "tiles.grb2"), w, w+5, s, s+5)
        for (w, s) in HOLE:
            cut (src, os.path.join (OUT, "tiles-hole.grb2"), w, w+5, s, s+5)

    for name in ("whole.grb2", "tiles.grb2", "tiles-hole.grb2"):
        p = os.path.join (OUT, name)
        print (f"  {name:16s} {os.path.getsize(p):7d} Б")
    for src in srcs:                      # исходники не храним
        os.remove (src)


if __name__ == "__main__":
    main()
