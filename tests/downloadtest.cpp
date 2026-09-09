// Проверка загрузки без графики: дёргает тот же FileLoaderGRIB, которым
// управляет диалог «Загрузить GRIB», и печатает всё, что он говорит.
// Собирается и под Linux, и под Windows — под Wine это единственный
// надёжный способ проверить сборку, не воюя с координатами мыши.
#include <cstdio>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QTimer>

#include "FileLoaderGRIB.h"
#include "Settings.h"
#include "Util.h"

int main (int argc, char **argv)
{
	QCoreApplication app (argc, argv);
	QCoreApplication::setOrganizationName ("MAKGrib");
	QCoreApplication::setApplicationName ("MAKGrib");
	Settings::initializeSettingsDir ();

	// Открытое море к юго-западу от Австралии: и атмосфера, и волнение.
	double x0 = 96.0, x1 = 104.0, y0 = -42.0, y1 = -34.0;
	int days = 1, interval = 6;

	printf ("сервер   : %s\n", qPrintable (Util::getServerName()));
	printf ("область  : %.1f..%.1f E, %.1f..%.1f N, %d сут, шаг %d ч\n",
	        x0, x1, y0, y1, days, interval);
	fflush (stdout);

	QNetworkAccessManager mgr;
	FileLoaderGRIB loader (&mgr, nullptr);

	QEventLoop loop;
	QElapsedTimer t; t.start();
	qint64 bytes = 0;
	QString error;
	bool ok = false;

	QObject::connect (&loader, &FileLoaderGRIB::signalGribSendMessage,
	        [&](QString m) {
		printf ("[%5.1f c] %s\n", t.elapsed()/1000.0, qPrintable(m));
		fflush (stdout);
	});
	QObject::connect (&loader, &FileLoaderGRIB::signalGribDataReceived,
	        [&](QByteArray *c, QString name) {
		bytes = (c != nullptr) ? c->size() : 0;
		ok = (c != nullptr) && c->startsWith ("GRIB");
		printf ("[%5.1f c] ПОЛУЧЕНО %s, %lld байт, GRIB=%s\n",
		        t.elapsed()/1000.0, qPrintable(name), (long long)bytes,
		        ok ? "да" : "НЕТ");
		loop.quit ();
	});
	QObject::connect (&loader, &FileLoaderGRIB::signalGribLoadError,
	        [&](QString e) {
		error = e;
		printf ("[%5.1f c] ОШИБКА: %s\n", t.elapsed()/1000.0, qPrintable(e));
		loop.quit ();
	});

	// Страховка: тест не должен висеть, чем бы дело ни кончилось.
	QTimer guard; guard.setSingleShot (true);
	QObject::connect (&guard, &QTimer::timeout, [&]() {
		error = "за 10 минут не ответило ничего — это зависание";
		printf ("[%5.1f c] %s\n", t.elapsed()/1000.0, qPrintable(error));
		loop.quit ();
	});
	guard.start (600000);

	loader.getGribFile ("GFS", x0, x1, y0, y1, 0.25, interval, days, "last",
	        true, true, true, true, true, true, true,   // ветер..изотерма
	        false, false, false,                        // снег, ледяной дождь
	        true, true, false,                          // CAPE, CIN, отражаемость
	        false, false, false, false, false, false, false, false,
	        false,                                      // skewT
	        true,                                       // порывы
	        "WW3", true, true, true);                   // волнение
	loop.exec ();

	printf ("\nитог: %s за %.1f c\n",
	        ok ? "данные получены" : qPrintable("не получено — " + error),
	        t.elapsed()/1000.0);
	return ok ? 0 : 1;
}
