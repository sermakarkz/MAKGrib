#!/usr/bin/env python3
"""Переводы приложения MAKGrib для Android.

Здесь лежит таблица: русская строка — её перевод на каждый язык. Файлы
.ts собирает lupdate, эта программа проставляет в них переводы, а
lrelease превращает в .qm, которые кладутся в APK.

Строки со склонением (%n) хранятся списком форм — по правилам каждого
языка: в русском три (точка/точки/точек), в английском две, в турецком
и китайском одна.

Азербайджанский, туркменский, персидский и арабский переведены без
носителя языка: перед выпуском их стоит показать человеку, который на
них ходит в море.
"""
import os, re, subprocess, sys

LANGS = ["en", "de", "fr", "es", "it", "tr", "az", "fa", "tk", "ar", "zh"]

# Обычные строки.
T = {
"Глубина": dict(en="Days", de="Tage", fr="Jours", es="Días", it="Giorni",
    tr="Gün", az="Gün", fa="روز", tk="Gün", ar="أيام", zh="天数"),
"сут": dict(en="d", de="T", fr="j", es="d", it="g", tr="g", az="g",
    fa="روز", tk="g", ar="ي", zh="天"),
"Шаг": dict(en="Step", de="Schritt", fr="Pas", es="Paso", it="Passo",
    tr="Adım", az="Addım", fa="گام", tk="Ädim", ar="خطوة", zh="步长"),
"ч": dict(en="h", de="Std", fr="h", es="h", it="h", tr="s", az="s",
    fa="س", tk="sag", ar="س", zh="时"),
"Пояс": dict(en="Zone", de="Zone", fr="Fuseau", es="Zona", it="Fuso",
    tr="Dilim", az="Qurşaq", fa="منطقه", tk="Guşak", ar="المنطقة",
    zh="时区"),
"Язык": dict(en="Language", de="Sprache", fr="Langue", es="Idioma",
    it="Lingua", tr="Dil", az="Dil", fa="زبان", tk="Dil", ar="اللغة",
    zh="语言"),
"Число": dict(en="Day", de="Tag", fr="Jour", es="Día", it="Giorno",
    tr="Gün", az="Gün", fa="روز", tk="Gün", ar="اليوم", zh="日"),
"Месяц": dict(en="Month", de="Monat", fr="Mois", es="Mes", it="Mese",
    tr="Ay", az="Ay", fa="ماه", tk="Aý", ar="الشهر", zh="月"),
"Час": dict(en="Hour", de="Stunde", fr="Heure", es="Hora", it="Ora",
    tr="Saat", az="Saat", fa="ساعت", tk="Sagat", ar="الساعة", zh="时"),
"Мин": dict(en="Min", de="Min", fr="Min", es="Min", it="Min", tr="Dak",
    az="Dəq", fa="دقیقه", tk="Min", ar="دقيقة", zh="分"),
"Узлы": dict(en="Knots", de="Knoten", fr="Nœuds", es="Nudos", it="Nodi",
    tr="Knot", az="Düyün", fa="گره", tk="Uzel", ar="عقدة", zh="节"),
"Доли": dict(en="Tenths", de="Zehntel", fr="Dixièmes", es="Décimas",
    it="Decimi", tr="Ondalık", az="Onda bir", fa="دهم", tk="Ondan bir",
    ar="أعشار", zh="十分位"),
"Проложить": dict(en="Draw", de="Zeichnen", fr="Tracer", es="Trazar",
    it="Traccia", tr="Çiz", az="Çək", fa="ترسیم", tk="Çyz", ar="ارسم",
    zh="绘制"),
"Править": dict(en="Edit", de="Bearbeiten", fr="Modifier", es="Editar",
    it="Modifica", tr="Düzenle", az="Düzəliş", fa="ویرایش", tk="Düzet",
    ar="تحرير", zh="编辑"),
"Очистить": dict(en="Clear", de="Löschen", fr="Effacer", es="Borrar",
    it="Cancella", tr="Temizle", az="Təmizlə", fa="پاک کردن", tk="Arassala",
    ar="مسح", zh="清除"),
"Закончить": dict(en="Done", de="Fertig", fr="Terminer", es="Listo",
    it="Fatto", tr="Bitti", az="Hazır", fa="پایان", tk="Taýýar", ar="تم",
    zh="完成"),
"Раскладываю карты…": dict(en="Unpacking maps…", de="Karten werden entpackt…",
    fr="Décompression des cartes…", es="Descomprimiendo mapas…",
    it="Estrazione mappe…", tr="Haritalar açılıyor…",
    az="Xəritələr açılır…", fa="در حال باز کردن نقشه‌ها…",
    tk="Kartalar açylýar…", ar="جارٍ فك الخرائط…", zh="正在解包地图…"),
"Карты ещё раскладываются…": dict(en="Maps are still unpacking…",
    de="Karten werden noch entpackt…", fr="Cartes en cours de décompression…",
    es="Los mapas se están descomprimiendo…", it="Mappe in estrazione…",
    tr="Haritalar hâlâ açılıyor…", az="Xəritələr hələ açılır…",
    fa="نقشه‌ها هنوز باز می‌شوند…", tk="Kartalar heniz açylýar…",
    ar="لا تزال الخرائط تُفك…", zh="地图仍在解包…"),
"Касайтесь карты — ставьте точки": dict(en="Tap the map to add points",
    de="Karte antippen, um Punkte zu setzen",
    fr="Touchez la carte pour placer des points",
    es="Toque el mapa para añadir puntos",
    it="Tocca la mappa per aggiungere punti",
    tr="Nokta eklemek için haritaya dokunun",
    az="Nöqtə əlavə etmək üçün xəritəyə toxunun",
    fa="برای افزودن نقطه روی نقشه بزنید",
    tk="Nokat goşmak üçin karta degiň",
    ar="انقر على الخريطة لإضافة نقاط", zh="点击地图添加航点"),
"Тяните точки · долгое нажатие удалит": dict(
    en="Drag points · long press deletes",
    de="Punkte ziehen · langes Drücken löscht",
    fr="Déplacez les points · appui long pour supprimer",
    es="Arrastre los puntos · pulsación larga para borrar",
    it="Trascina i punti · pressione lunga per eliminare",
    tr="Noktaları sürükleyin · uzun basınca silinir",
    az="Nöqtələri sürüşdürün · uzun basmaq silir",
    fa="نقاط را بکشید · فشار طولانی حذف می‌کند",
    tk="Nokatlary süýşüriň · uzak basmak pozýar",
    ar="اسحب النقاط · الضغط المطوّل يحذف", zh="拖动航点 · 长按删除"),
"Маршрут не проложен": dict(en="No route yet", de="Keine Route",
    fr="Aucune route", es="Sin ruta", it="Nessuna rotta",
    tr="Rota yok", az="Marşrut yoxdur", fa="مسیری ثبت نشده",
    tk="Ugur ýok", ar="لا يوجد مسار", zh="尚无航线"),
" · выход %1 · приход %2": dict(
    en=" · departure %1 · arrival %2", de=" · Abfahrt %1 · Ankunft %2",
    fr=" · départ %1 · arrivée %2", es=" · salida %1 · llegada %2",
    it=" · partenza %1 · arrivo %2", tr=" · kalkış %1 · varış %2",
    az=" · çıxış %1 · gəliş %2", fa=" · حرکت %1 · رسیدن %2",
    tk=" · çykyş %1 · geliş %2", ar=" · المغادرة %1 · الوصول %2",
    zh=" · 出发 %1 · 到达 %2"),
"Закончить · %1 · %2": dict(en="Done · %1 · %2", de="Fertig · %1 · %2",
    fr="Terminer · %1 · %2", es="Listo · %1 · %2", it="Fatto · %1 · %2",
    tr="Bitti · %1 · %2", az="Hazır · %1 · %2", fa="پایان · %1 · %2",
    tk="Taýýar · %1 · %2", ar="تم · %1 · %2", zh="完成 · %1 · %2"),
"%1 · %2 · %3%4": dict(en="%1 · %2 · %3%4", de="%1 · %2 · %3%4",
    fr="%1 · %2 · %3%4", es="%1 · %2 · %3%4", it="%1 · %2 · %3%4",
    tr="%1 · %2 · %3%4", az="%1 · %2 · %3%4", fa="%1 · %2 · %3%4",
    tk="%1 · %2 · %3%4", ar="%1 · %2 · %3%4", zh="%1 · %2 · %3%4"),
"%1 ч %2 мин": dict(en="%1 h %2 min", de="%1 Std %2 Min",
    fr="%1 h %2 min", es="%1 h %2 min", it="%1 h %2 min",
    tr="%1 sa %2 dk", az="%1 s %2 dəq", fa="%1 ساعت %2 دقیقه",
    tk="%1 sag %2 min", ar="%1 س %2 د", zh="%1 小时 %2 分"),
"%1 мин": dict(en="%1 min", de="%1 Min", fr="%1 min", es="%1 min",
    it="%1 min", tr="%1 dk", az="%1 dəq", fa="%1 دقیقه", tk="%1 min",
    ar="%1 د", zh="%1 分"),
"GPS в этом телефоне недоступен": dict(en="GPS is not available on this phone",
    de="GPS ist auf diesem Telefon nicht verfügbar",
    fr="GPS indisponible sur ce téléphone",
    es="El GPS no está disponible en este teléfono",
    it="GPS non disponibile su questo telefono",
    tr="Bu telefonda GPS yok", az="Bu telefonda GPS yoxdur",
    fa="GPS در این گوشی در دسترس نیست", tk="Bu telefonda GPS ýok",
    ar="نظام تحديد المواقع غير متاح على هذا الهاتف", zh="本机不支持 GPS"),
"Без разрешения на место GPS не работает": dict(
    en="Without location permission GPS will not work",
    de="Ohne Standortberechtigung funktioniert GPS nicht",
    fr="Sans autorisation de localisation, le GPS ne fonctionne pas",
    es="Sin permiso de ubicación el GPS no funciona",
    it="Senza permesso di posizione il GPS non funziona",
    tr="Konum izni olmadan GPS çalışmaz",
    az="Məkan icazəsi olmadan GPS işləmir",
    fa="بدون اجازهٔ موقعیت، GPS کار نمی‌کند",
    tk="Ýerleşiş rugsady bolmasa GPS işlemeýär",
    ar="بدون إذن الموقع لن يعمل GPS", zh="没有定位权限，GPS 无法工作"),
"Место ещё не определено": dict(en="Position not fixed yet",
    de="Position noch nicht bestimmt", fr="Position pas encore déterminée",
    es="Posición aún no determinada", it="Posizione non ancora rilevata",
    tr="Konum henüz belirlenmedi", az="Mövqe hələ təyin olunmayıb",
    fa="موقعیت هنوز مشخص نشده", tk="Ýerleşiş heniz kesgitlenmedi",
    ar="لم يُحدَّد الموقع بعد", zh="尚未定位"),
"Язык сменится при следующем запуске": dict(
    en="Language will change on next start",
    de="Sprache ändert sich beim nächsten Start",
    fr="La langue changera au prochain démarrage",
    es="El idioma cambiará al reiniciar",
    it="La lingua cambierà al prossimo avvio",
    tr="Dil bir sonraki açılışta değişir",
    az="Dil növbəti açılışda dəyişəcək",
    fa="زبان در اجرای بعدی تغییر می‌کند",
    tk="Dil indiki gezek açylanda üýtgär",
    ar="ستتغيّر اللغة عند التشغيل التالي", zh="语言将在下次启动时改变"),
"данных нет": dict(en="no data", de="keine Daten", fr="pas de données",
    es="sin datos", it="nessun dato", tr="veri yok", az="məlumat yoxdur",
    fa="داده‌ای نیست", tk="maglumat ýok", ar="لا توجد بيانات", zh="无数据"),
"  · устарел": dict(en="  · out of date", de="  · veraltet",
    fr="  · périmé", es="  · caducado", it="  · scaduto",
    tr="  · süresi geçmiş", az="  · köhnəlmiş", fa="  · منقضی",
    tk="  · könelen", ar="  · منتهي", zh="  · 已过期"),
"прогноз не загружен": dict(en="forecast not loaded",
    de="Vorhersage nicht geladen", fr="prévision non chargée",
    es="pronóstico no cargado", it="previsione non caricata",
    tr="tahmin yüklenmedi", az="proqnoz yüklənməyib",
    fa="پیش‌بینی بارگذاری نشده", tk="çaklama ýüklenmedi",
    ar="لم يتم تحميل التوقعات", zh="未加载预报"),
"здесь данных нет": dict(en="no data here", de="hier keine Daten",
    fr="pas de données ici", es="aquí no hay datos", it="qui nessun dato",
    tr="burada veri yok", az="burada məlumat yoxdur", fa="اینجا داده‌ای نیست",
    tk="bu ýerde maglumat ýok", ar="لا بيانات هنا", zh="此处无数据"),
"Ветер %1 %2": dict(en="Wind %1 %2", de="Wind %1 %2", fr="Vent %1 %2",
    es="Viento %1 %2", it="Vento %1 %2", tr="Rüzgâr %1 %2",
    az="Külək %1 %2", fa="باد %1 %2", tk="Ýel %1 %2", ar="الرياح %1 %2",
    zh="风 %1 %2"),
"Порывы %1": dict(en="Gusts %1", de="Böen %1", fr="Rafales %1",
    es="Rachas %1", it="Raffiche %1", tr="Fırtına %1", az="Qasırğa %1",
    fa="تندباد %1", tk="Şemal urgusy %1", ar="هبات %1", zh="阵风 %1"),
"Волна %1 м": dict(en="Wave %1 m", de="Welle %1 m", fr="Vague %1 m",
    es="Ola %1 m", it="Onda %1 m", tr="Dalga %1 m", az="Dalğa %1 m",
    fa="موج %1 متر", tk="Tolkun %1 m", ar="الموج %1 م", zh="浪高 %1 米"),
"Давление %1": dict(en="Pressure %1", de="Druck %1", fr="Pression %1",
    es="Presión %1", it="Pressione %1", tr="Basınç %1", az="Təzyiq %1",
    fa="فشار %1", tk="Basyş %1", ar="الضغط %1", zh="气压 %1"),
"Воздух %1": dict(en="Air %1", de="Luft %1", fr="Air %1", es="Aire %1",
    it="Aria %1", tr="Hava %1", az="Hava %1", fa="هوا %1", tk="Howa %1",
    ar="الهواء %1", zh="气温 %1"),
"Вода %1": dict(en="Water %1", de="Wasser %1", fr="Eau %1", es="Agua %1",
    it="Acqua %1", tr="Su %1", az="Su %1", fa="آب %1", tk="Suw %1",
    ar="الماء %1", zh="水温 %1"),
"Облачность %1": dict(en="Cloud %1", de="Bewölkung %1", fr="Nuages %1",
    es="Nubes %1", it="Nuvole %1", tr="Bulut %1", az="Bulud %1",
    fa="ابر %1", tk="Bulut %1", ar="الغيوم %1", zh="云量 %1"),
"Осадки %1": dict(en="Rain %1", de="Niederschlag %1", fr="Pluie %1",
    es="Lluvia %1", it="Pioggia %1", tr="Yağış %1", az="Yağıntı %1",
    fa="بارش %1", tk="Ygal %1", ar="الأمطار %1", zh="降水 %1"),
"Качаю %1 из %2 · %3 КБ": dict(en="Loading %1 of %2 · %3 KB",
    de="Lade %1 von %2 · %3 KB", fr="Téléchargement %1 sur %2 · %3 Ko",
    es="Descargando %1 de %2 · %3 KB", it="Scarico %1 di %2 · %3 KB",
    tr="İndiriliyor %1/%2 · %3 KB", az="Yüklənir %1/%2 · %3 KB",
    fa="دریافت %1 از %2 · %3 کیلوبایت", tk="Ýüklenýär %1/%2 · %3 KB",
    ar="تنزيل %1 من %2 · %3 ك.ب", zh="下载 %1/%2 · %3 KB"),
"Качаю · %1 КБ": dict(en="Loading · %1 KB", de="Lade · %1 KB",
    fr="Téléchargement · %1 Ko", es="Descargando · %1 KB",
    it="Scarico · %1 KB", tr="İndiriliyor · %1 KB", az="Yüklənir · %1 KB",
    fa="دریافت · %1 کیلوبایت", tk="Ýüklenýär · %1 KB",
    ar="تنزيل · %1 ك.ب", zh="下载 · %1 KB"),
"Слои — язычком слева, срок — стрелками или шкалой, маршрут — значком с точками.":
    dict(en="Layers — tab on the left, time — arrows or the scale, route — the dotted icon.",
    de="Ebenen — Lasche links, Zeit — Pfeile oder Skala, Route — Punktsymbol.",
    fr="Couches — onglet à gauche, échéance — flèches ou échelle, route — icône à points.",
    es="Capas — pestaña izquierda, hora — flechas o escala, ruta — icono de puntos.",
    it="Livelli — linguetta a sinistra, ora — frecce o scala, rotta — icona a punti.",
    tr="Katmanlar — soldaki sekme, zaman — oklar veya ölçek, rota — noktalı simge.",
    az="Qatlar — soldakı dil, vaxt — oxlar və ya şkala, marşrut — nöqtəli işarə.",
    fa="لایه‌ها — زبانهٔ چپ، زمان — پیکان‌ها یا نوار، مسیر — نماد نقطه‌دار.",
    tk="Gatlaklar — çepdäki dil, wagt — oklar ýa-da şkala, ugur — nokatly nyşan.",
    ar="الطبقات — اللسان على اليسار، الوقت — الأسهم أو المقياس، المسار — أيقونة النقاط.",
    zh="图层 — 左侧标签，时间 — 箭头或时间轴，航线 — 点状图标。"),
"файл получен, но не прочитался": dict(en="file received but unreadable",
    de="Datei empfangen, aber unlesbar", fr="fichier reçu mais illisible",
    es="archivo recibido pero ilegible", it="file ricevuto ma illeggibile",
    tr="dosya alındı ama okunamadı", az="fayl alındı, amma oxunmadı",
    fa="فایل دریافت شد ولی خوانده نشد", tk="faýl alyndy, ýöne okalmady",
    ar="تم استلام الملف لكن تعذّرت قراءته", zh="文件已收到但无法读取"),
}

# Строки со склонением: формы по числу, в порядке, который ждёт Qt.
N = {
"%n точка(и)": dict(
    en=["%n point", "%n points"],
    de=["%n Punkt", "%n Punkte"],
    fr=["%n point", "%n points"],
    es=["%n punto", "%n puntos"],
    it=["%n punto", "%n punti"],
    tr=["%n nokta"],
    az=["%n nöqtə"],
    fa=["%n نقطه"],
    tk=["%n nokat"],
    ar=["%n نقطة", "نقطتان", "%n نقاط", "%n نقطة", "%n نقطة", "%n نقطة"],
    zh=["%n 个航点"]),
"Закончить · %n точка(и)": dict(
    en=["Done · %n point", "Done · %n points"],
    de=["Fertig · %n Punkt", "Fertig · %n Punkte"],
    fr=["Terminer · %n point", "Terminer · %n points"],
    es=["Listo · %n punto", "Listo · %n puntos"],
    it=["Fatto · %n punto", "Fatto · %n punti"],
    tr=["Bitti · %n nokta"],
    az=["Hazır · %n nöqtə"],
    fa=["پایان · %n نقطه"],
    tk=["Taýýar · %n nokat"],
    ar=["تم · %n نقطة", "تم · نقطتان", "تم · %n نقاط", "تم · %n نقطة",
        "تم · %n نقطة", "تم · %n نقطة"],
    zh=["完成 · %n 个航点"]),
"%n миля(и)": dict(
    en=["%n mile", "%n miles"],
    de=["%n Meile", "%n Meilen"],
    fr=["%n mille", "%n milles"],
    es=["%n milla", "%n millas"],
    it=["%n miglio", "%n miglia"],
    tr=["%n mil"],
    az=["%n mil"],
    fa=["%n مایل"],
    tk=["%n mil"],
    ar=["%n ميل", "ميلان", "%n أميال", "%n ميلاً", "%n ميل", "%n ميل"],
    zh=["%n 海里"]),
}


def fill (path, lang):
    s = open (path, encoding="utf-8").read ()

    def one (m):
        src = m.group (1)
        if src in T and lang in T[src]:
            return m.group(0).replace (m.group(2),
                       '<translation>%s</translation>' % T[src][lang])
        if src in N and lang in N[src]:
            forms = "".join ("<numerusform>%s</numerusform>" % f
                             for f in N[src][lang])
            return m.group(0).replace (m.group(2),
                       '<translation>%s</translation>' % forms)
        return m.group (0)

    s = re.sub (r'<source>(.*?)</source>\s*(<translation[^>]*>.*?</translation>)',
                one, s, flags=re.S)
    open (path, "w", encoding="utf-8").write (s)


if __name__ == "__main__":
    qt = os.path.expanduser (sys.argv[1] if len(sys.argv) > 1
                             else "~/Qt/6.8.3/gcc_64/bin")
    here = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
    trdir = os.path.join (here, "android", "tr")
    for lang in LANGS:
        ts = os.path.join (trdir, "makgrib_%s.ts" % lang)
        if not os.path.exists (ts):
            print ("нет файла", ts);  continue
        fill (ts, lang)
        subprocess.run ([os.path.join (qt, "lrelease"), "-silent", ts],
                        check=False)
    print ("переводы собраны:", ", ".join (LANGS))
