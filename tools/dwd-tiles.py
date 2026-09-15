#!/usr/bin/env python3
"""Нарезка прогнозов DWD на плитки.

Зачем плитки. У NOAA есть программа на сервере, которая режет прогноз по
присланному прямоугольнику: запросил свой экран — получил ровно его. Нам
такое повторить нечем, своего сервера нет. Поэтому режем заранее сеткой
квадратов, а читатель берёт те, что накрывают его экран, и склеивает их
у себя (GribReader::mergeTiles). Промах при этом ограничен сверху: даже
ради одного градуса скачается одна плитка, меньше мегабайта.

Размер плитки выбран не по градусам, а по числу точек — около 81×81 в
каждой, чтобы файлы у всех наборов вышли сопоставимыми: 5° на европейской
сетке 0.0625° и 20° на мировой 0.25°.

Почему один файл на плитку, а не по файлу на сутки. Делить по суткам
было бы честнее: моряк платил бы за то, что взял. Пробовали — не вышло,
и упёрлось не в объём, а в число файлов: GitHub на полутора тысячах
загрузок подряд отвечает «secondary rate limit» через сорок восемь
секунд. Причём замена файла — это два обращения, удаление и заливка. При
одном файле на плитку их триста семьдесят, и это проходит.

Что готовится:
    eu-wind     ICON-EU, 7 км — ветер, порывы и давление по Европе
    eu-wave     EWAM, 5 км    — волнение и зыбь; на восток только до 42°
    world-wave  GWAM, 0.25°   — волнение по всему миру, включая Каспий
    world-wind  ICON, 13 км   — ветер по миру (нужен cdo, см. REGRID)

Запуск:
    python3 tools/dwd-tiles.py                  все наборы
    python3 tools/dwd-tiles.py eu-wind eu-wave  только названные
Переменные окружения: WORK, OUT, DAYS, JOBS.
"""

import os, re, sys, json, time, bz2, shutil, subprocess
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone

import numpy as np
import eccodes as ec

NWP  = "https://opendata.dwd.de/weather/nwp"
WAVE = "https://opendata.dwd.de/weather/maritime/wave_models"

WORK = os.environ.get ("WORK", "/tmp/dwd")
OUT  = os.environ.get ("OUT",  "out")
DAYS = int (os.environ.get ("DAYS", "5"))      # суток вперёд
JOBS = int (os.environ.get ("JOBS", "6"))      # одновременных загрузок
STEP_HOURS = 3                                  # шаг по времени

# Упаковка: библиотека чтения GRIB2 у движка не знает шаблон 5.42 (CCSDS),
# которым жмёт ICON, и молча выбрасывает такую запись. Перекладываем в
# простую упаковку — при десяти битах выходит даже меньше исходного, а
# ошибка в сотые доли единицы прогнозу безразлична.
BITS = 10


# ---------------------------------------------------------------- наборы
def icon_eu_name (p, P, d, c, step):
    return (f"icon-eu_europe_regular-lat-lon_single-level_"
            f"{d}{c}_{step:03d}_{P}.grib2.bz2")

def icon_name (p, P, d, c, step):
    return (f"icon_global_icosahedral_single-level_"
            f"{d}{c}_{step:03d}_{P}.grib2.bz2")

def wave_name (model):
    def f (p, P, d, c, step):
        return f"{model.upper()}_{P}_{d}{c}_{step:03d}.grib2.bz2"
    return f


SETS = {
    "eu-wind": dict (
        title  = "Ветер ICON-EU, 7 км",
        model  = "DWD ICON-EU",
        kind   = "wind",
        base   = f"{NWP}/icon-eu/grib",
        runs   = ("00","03","06","09","12","15","18","21"),
        name   = icon_eu_name,
        params = [("u_10m","U_10M"), ("v_10m","V_10M"),
                  ("vmax_10m","VMAX_10M"), ("pmsl","PMSL")],
        fields = ["ветер", "порывы", "давление"],
        tile   = 5,
        hours  = 120,          # докуда считает сама модель
        area   = (-23.5, 62.5, 29.5, 70.5),
        probe  = ("v_10m", r"single-level_(\d{8})\d{2}_{step}_V_10M"),
    ),
    "eu-wave": dict (
        title  = "Волнение EWAM, 5 км",
        model  = "DWD EWAM",
        kind   = "wave",
        base   = f"{WAVE}/ewam/grib",
        runs   = ("00","12"),
        name   = wave_name ("ewam"),
        # Высота, направление и период общего волнения, затем ветровое
        # волнение и зыбь врозь — этого у бесплатных источников больше
        # нет ни у кого.
        params = [("swh","SWH"), ("mwd","MWD"), ("tm10","TM10"),
                  ("shww","SHWW"), ("mdww","MDWW"),
                  ("shts","SHTS"), ("mdts","MDTS")],
        fields = ["волнение", "зыбь"],
        tile   = 5,
        hours  = 78,           # EWAM дальше не считает — всего 3 с четвертью суток
        area   = (-10.5, 42.0, 30.0, 66.0),
        probe  = ("swh", r"EWAM_SWH_(\d{8})\d{2}_{step}"),
    ),
    "world-wave": dict (
        title  = "Волнение GWAM, 0.25°",
        model  = "DWD GWAM",
        kind   = "wave",
        base   = f"{WAVE}/gwam/grib",
        runs   = ("00","12"),
        name   = wave_name ("gwam"),
        params = [("swh","SWH"), ("mwd","MWD"), ("tm10","TM10"),
                  ("shww","SHWW"), ("mdww","MDWW"),
                  ("shts","SHTS"), ("mdts","MDTS")],
        fields = ["волнение", "зыбь"],
        tile   = 20,
        hours  = 174,
        area   = (-180.0, 180.0, -78.0, 84.0),
        probe  = ("swh", r"GWAM_SWH_(\d{8})\d{2}_{step}"),
    ),
    "world-wind": dict (
        title  = "Ветер ICON, 13 км",
        model  = "DWD ICON global",
        kind   = "wind",
        base   = f"{NWP}/icon/grib",
        runs   = ("00","06","12","18"),
        name   = icon_name,
        params = [("u_10m","U_10M"), ("v_10m","V_10M"), ("pmsl","PMSL")],
        fields = ["ветер", "давление"],
        tile   = 20,
        hours  = 180,
        area   = (-180.0, 180.0, -90.0, 90.0),
        probe  = ("v_10m", r"single-level_(\d{8})\d{2}_{step}_V_10M"),
        # Глобальная ICON лежит на икосаэдре, обычной сетки у неё нет.
        # DWD выкладывает готовые веса, перекладка — одна команда cdo.
        regrid = "ICON_GLOBAL2WORLD_025_EASY",
    ),
}


# ------------------------------------------------------------ вспомогалки
def log (*a):
    print (*a, flush=True)


def fetch (url, dest, tries=3):
    for n in range (tries):
        try:
            with urllib.request.urlopen (url, timeout=120) as r:
                raw = r.read()
            if dest.endswith (".bz2"):
                raw = bz2.decompress (raw)
                dest = dest[:-4]
            with open (dest, "wb") as f:
                f.write (raw)
            return dest
        except Exception as e:
            if n == tries-1:
                return None
            time.sleep (2*(n+1))
    return None


def latest_run (base, probe_dir, pattern, runs):
    """Самый свежий выпуск, выложенный целиком.

    По часам гадать нельзя: выкладывают с задержкой, да и часы машины
    могут уйти вперёд относительно сервера. Но мало найти выпуск — надо
    убедиться, что он доложен до конца: DWD выкладывает срок за сроком
    часа два, и если хвататься за нулевой срок, достанется огрызок.
    Проверено на своей шкуре: взяли прогон 00 UTC, у которого было
    готово пятнадцать часов вперёд, и молча нарезали шесть сроков вместо
    сорока. Поэтому ищем по последнему нужному сроку.
    """
    best = None
    for c in runs:
        try:
            with urllib.request.urlopen (f"{base}/{c}/{probe_dir}/", timeout=60) as r:
                page = r.read().decode ("utf-8", "replace")
        except Exception:
            continue
        for m in re.finditer (pattern, page):
            key = (m.group(1), c)
            if best is None or key > best:
                best = key
    return best


def iso (d, c):
    """Время выпуска как полагается: 20260914 + 18 -> 2026-09-14T18:00:00Z."""
    return f"{d[0:4]}-{d[4:6]}-{d[6:8]}T{c}:00:00Z"


def tile_key (lon, lat):
    ew = "E" if lon >= 0 else "W"
    ns = "N" if lat >= 0 else "S"
    return f"{ns}{abs(int(round(lat))):02d}{ew}{abs(int(round(lon))):03d}"


# --------------------------------------------------------- маска морей
def sea_tiles (size):
    """Какие квадраты содержат воду.

    Ветер определён и над сушей, поэтому сама по себе запись не скажет,
    нужен ли квадрат. Спрашиваем у волновой модели: где GWAM считает
    волны, там море. Заодно это отсекает Сахару и Сибирь, ради которых
    держать плитки незачем.
    """
    global _MASK
    if "_MASK" not in globals():
        run = latest_run (f"{WAVE}/gwam/grib", "swh",
                          r"GWAM_SWH_(\d{8})\d{2}_000", ("12","00"))
        if run is None:
            sys.exit ("GWAM недоступна — маску морей взять неоткуда")
        d, c = run
        path = os.path.join (WORK, "mask.grib2")
        if not os.path.exists (path):
            got = fetch (f"{WAVE}/gwam/grib/{c}/swh/GWAM_SWH_{d}{c}_000.grib2.bz2",
                         path + ".bz2")
            if got is None:
                sys.exit ("маску морей скачать не удалось")
        with open (path, "rb") as f:
            h = ec.codes_grib_new_from_file (f)
            Ni = ec.codes_get (h, "Ni"); Nj = ec.codes_get (h, "Nj")
            lo1 = ec.codes_get (h, "longitudeOfFirstGridPointInDegrees")
            la1 = ec.codes_get (h, "latitudeOfFirstGridPointInDegrees")
            di  = ec.codes_get (h, "iDirectionIncrementInDegrees")
            dj  = ec.codes_get (h, "jDirectionIncrementInDegrees")
            jp  = ec.codes_get (h, "jScansPositively")
            ec.codes_set (h, "missingValue", 9.9e20)
            v = ec.codes_get_values (h).reshape (Nj, Ni)
            ec.codes_release (h)
        lons = (lo1 + np.arange (Ni)*di + 180) % 360 - 180
        lats = la1 + np.arange (Nj)*dj*(1 if jp else -1)
        jj, ii = np.where (v < 9e20)
        _MASK = (lons[ii], lats[jj])
        log (f"маска морей: {len(ii)} точек воды")
    lons, lats = _MASK
    keys = set()
    for lo, la in zip (lons, lats):
        keys.add ((np.floor (la/size)*size, np.floor (lo/size)*size))
    return {(float(la), float(lo)) for la, lo in keys}


# ------------------------------------------------------------- нарезка
def slice_field (path, tiles, size, out_for):
    """Разрезать одно поле одного срока и дописать куски в файлы плиток.

    Возвращает, сколько квадратов вышло непустыми.
    """
    written = 0
    with open (path, "rb") as f:
        h = ec.codes_grib_new_from_file (f)
        if h is None:
            return 0
        Ni = ec.codes_get (h, "Ni"); Nj = ec.codes_get (h, "Nj")
        lo1 = ec.codes_get (h, "longitudeOfFirstGridPointInDegrees")
        la1 = ec.codes_get (h, "latitudeOfFirstGridPointInDegrees")
        di  = ec.codes_get (h, "iDirectionIncrementInDegrees")
        dj  = ec.codes_get (h, "jDirectionIncrementInDegrees")
        jp  = ec.codes_get (h, "jScansPositively")
        ec.codes_set (h, "missingValue", 9.9e20)
        v = ec.codes_get_values (h).reshape (Nj, Ni)
        lons = (lo1 + np.arange (Ni)*di + 180) % 360 - 180
        lats = la1 + np.arange (Nj)*dj*(1 if jp else -1)

        for (la0, lo0) in tiles:
            ii = np.where ((lons >= lo0) & (lons <= lo0+size))[0]
            jj = np.where ((lats >= la0) & (lats <= la0+size))[0]
            if len(ii) < 2 or len(jj) < 2:
                continue                       # квадрат вне этой модели
            sub = v[np.ix_(jj, ii)]
            if not np.any (sub < 9e20):
                continue                       # вся плитка без данных
            h2 = ec.codes_clone (h)
            ec.codes_set (h2, "packingType", "grid_simple")
            ec.codes_set (h2, "bitsPerValue", BITS)
            ec.codes_set (h2, "Ni", len(ii)); ec.codes_set (h2, "Nj", len(jj))
            ec.codes_set (h2, "longitudeOfFirstGridPointInDegrees", float(lons[ii[0]]) % 360)
            ec.codes_set (h2, "longitudeOfLastGridPointInDegrees",  float(lons[ii[-1]]) % 360)
            ec.codes_set (h2, "latitudeOfFirstGridPointInDegrees",  float(lats[jj[0]]))
            ec.codes_set (h2, "latitudeOfLastGridPointInDegrees",   float(lats[jj[-1]]))
            if np.any (sub >= 9e20):
                ec.codes_set (h2, "bitmapPresent", 1)
                ec.codes_set (h2, "missingValue", 9.9e20)
            ec.codes_set_values (h2, sub.flatten())
            with open (out_for (la0, lo0), "ab") as o:
                ec.codes_write (h2, o)
            ec.codes_release (h2)
            written += 1
        ec.codes_release (h)
    return written


def regrid (src, dst, weights_dir):
    """Переложить икосаэдр на обычную сетку готовыми весами DWD."""
    grid = os.path.join (weights_dir, "target_grid_world_025.txt")
    wts  = os.path.join (weights_dir, "weights_icogl2world_025.nc")
    try:
        subprocess.run (["cdo", "-s", f"remap,{grid},{wts}", src, dst],
                        check=True, capture_output=True)
        return os.path.exists (dst)
    except Exception as e:
        log ("  перекладка не вышла:", e)
        return False


def prepare_weights (tag):
    """Скачать и распаковать веса перекладки, если их ещё нет."""
    d = os.path.join (WORK, tag)
    if os.path.isdir (d):
        return d
    os.makedirs (d, exist_ok=True)
    url = f"https://opendata.dwd.de/weather/lib/cdo/{tag}.tar.bz2"
    tar = os.path.join (WORK, tag + ".tar.bz2")
    log (f"  качаю веса перекладки {tag}")
    try:
        urllib.request.urlretrieve (url, tar)
        subprocess.run (["tar", "xjf", tar, "-C", d], check=True)
        return d
    except Exception as e:
        log ("  веса не скачались:", e)
        return None


# ------------------------------------------------------------- один набор
def build (name, spec):
    log (f"\n=== {name}: {spec['title']} ===")
    if shutil.which ("cdo") is None and spec.get ("regrid"):
        log ("  нет cdo — набор пропущен")
        return None

    # Сколько просим и сколько модель может — берём меньшее. У EWAM это
    # 78 часов, и просить у неё пять суток бессмысленно: она бы просто
    # не нашлась, а прежде отдавала бы огрызок молча.
    last  = min (DAYS*24, spec["hours"])
    steps = list (range (0, last + 1, STEP_HOURS))
    probe_dir, probe_pat = spec["probe"]
    # Не format: в самом образце есть \d{8}, и подстановка по фигурным
    # скобкам принимает восьмёрку за номер поля.
    run = latest_run (spec["base"], probe_dir,
                      probe_pat.replace ("{step}", f"{steps[-1]:03d}"),
                      spec["runs"])
    if run is None:
        log ("  свежего выпуска не нашлось")
        return None
    d, c = run
    log (f"  выпуск {d} {c} UTC")

    weights = prepare_weights (spec["regrid"]) if spec.get ("regrid") else None
    if spec.get ("regrid") and weights is None:
        return None

    size  = spec["tile"]
    aW, aE, aS, aN = spec["area"]
    tiles = sorted (t for t in sea_tiles (size)
                    if t[1] + size > aW and t[1] < aE
                    and t[0] + size > aS and t[0] < aN)
    log (f"  квадратов с морем в области: {len(tiles)}")

    made  = {}                                   # плитка -> [байты по суткам]

    def path_for (la0, lo0):
        return os.path.join (OUT, f"{name}_{tile_key(lo0,la0)}.grb2")

    # Старые куски убрать: дописываем в конец, второй прогон удвоил бы.
    for (la0, lo0) in tiles:
        p = path_for (la0, lo0)
        if os.path.exists (p):
            os.remove (p)

    t0 = time.time()
    missed = []
    for step in steps:
        # Скачиваем поля одного срока разом: сеть тут узкое место.
        def get (pp):
            p, P = pp
            fn  = spec["name"] (p, P, d, c, step)
            dst = os.path.join (WORK, fn)
            return fetch (f"{spec['base']}/{c}/{p}/{fn}", dst)

        with ThreadPoolExecutor (max_workers=JOBS) as pool:
            files = list (pool.map (get, spec["params"]))

        if any (f is None for f in files):
            missed.append (step)
        for src in files:
            if src is None:
                continue
            if weights is not None:
                out = src + ".ll"
                if not regrid (src, out, weights):
                    os.remove (src); continue
                os.remove (src); src = out
            slice_field (src, tiles, size, path_for)
            os.remove (src)
        if step % 24 == 0:
            log (f"  срок {step:3d} ч  ({time.time()-t0:.0f} с)")

    # Опись: точный размер каждого файла. По ней приложение скажет
    # человеку, во сколько мегабайт обойдётся загрузка, ещё до того как
    # он её начнёт — у большинства связь в роуминге или спутниковая.
    if missed:
        log (f"  ВНИМАНИЕ: нет данных на сроки {missed} — "
             f"выпуск неполон, {len(missed)} из {len(steps)}")
    sizes = {}
    total = 0
    for (la0, lo0) in tiles:
        p = path_for (la0, lo0)
        if not os.path.exists (p):
            continue
        n = os.path.getsize (p)
        # Список из одного числа, а не просто число: прежде тут лежал
        # размер по суткам, и приложение умеет складывать столько первых,
        # сколько суток попросили. Форму сохраняем — вдруг делить по
        # срокам когда-нибудь снова станет можно.
        sizes[tile_key (lo0, la0)] = [n]
        total += n
    log (f"  плиток вышло: {len(sizes)}, всего {total/1048576:.1f} МБ, "
         f"глубина {last} ч, сроков {len(steps)}, {time.time()-t0:.0f} с")

    index = f"files-{name}.json"
    with open (os.path.join (OUT, index), "w") as f:
        json.dump ({"set": name, "run": iso (d, c),
                    "tile": size, "days": DAYS, "sizes": sizes}, f)

    return dict (id=name, title=spec["title"], model=spec["model"],
                 kind=spec["kind"], fields=spec["fields"],
                 run=iso (d, c), tile=size,
                 # Глубина настоящая, а не заказанная: приложение по ней
                 # подписывает источник, и завышать её нельзя.
                 days=last//24, hours=last, interval=STEP_HOURS,
                 # Границы набора — чтобы приложение могло сказать «этого
                 # источника в вашем районе нет», не скачивая опись целиком.
                 west=aW, east=aE, south=aS, north=aN,
                 tiles=len(sizes), bytes=total, index=index)


def main ():
    want = sys.argv[1:] or list (SETS.keys())
    for w in want:
        if w not in SETS:
            sys.exit (f"нет такого набора: {w}")
    os.makedirs (WORK, exist_ok=True)
    os.makedirs (OUT,  exist_ok=True)

    done = []
    for name in want:
        try:
            r = build (name, SETS[name])
        except Exception as e:
            log (f"  {name}: сорвалось — {e}")
            r = None
        if r is not None:
            done.append (r)

    if not done:
        sys.exit ("ни одного набора не собралось")

    with open (os.path.join (OUT, "manifest-v1.json"), "w") as f:
        json.dump ({"version": 1,
                    "generated": datetime.now (timezone.utc)
                                 .strftime ("%Y-%m-%dT%H:%M:%SZ"),
                    "sets": done}, f, ensure_ascii=False, indent=1)
    log ("\nописи готовы:")
    for r in done:
        log (f"  {r['id']:12s} {r['tiles']:4d} плиток  "
             f"{r['bytes']/1048576:7.1f} МБ  выпуск {r['run']}")


if __name__ == "__main__":
    main()
