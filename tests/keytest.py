#!/usr/bin/env python3
# Сверка имён плиток: то, что печатает приложение (tests/keytest), против
# того, что даёт нарезалка. Подробности — в шапке tests/keytest.cpp.
import importlib.util, math, sys, os

HERE = os.path.dirname (os.path.abspath (__file__))
spec = importlib.util.spec_from_file_location (
           "tiler", os.path.join (HERE, "..", "tools", "dwd-tiles.py"))
tiler = importlib.util.module_from_spec (spec)
spec.loader.exec_module (tiler)

bad = n = 0
for line in sys.stdin:
    parts = line.split()
    if len (parts) != 4:
        continue
    tile, lon, lat, key = int (parts[0]), float (parts[1]), float (parts[2]), parts[3]
    mine = tiler.tile_key (math.floor (lon/tile)*tile, math.floor (lat/tile)*tile)
    n += 1
    if mine != key:
        bad += 1
        if bad <= 8:
            print (f"  [FAIL] плитка {tile}°, {lon},{lat} — "
                   f"приложение {key}, нарезалка {mine}")

print (f"  [{' OK ' if bad == 0 else 'FAIL'}] имена плиток сходятся "
       f"-> сверено {n}, расхождений {bad}")
sys.exit (1 if bad or n == 0 else 0)
