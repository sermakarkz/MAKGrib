// Где именно встаёт сеть под Windows: инициализация SSL или сам запрос.
// Печатает время каждого шага, чтобы было видно, что тормозит.
#include <cstdio>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QTimer>

static QElapsedTimer T;
static void say (const char *what) {
	printf ("[%7.2f c] %s\n", T.elapsed()/1000.0, what);
	fflush (stdout);
}

int main (int argc, char **argv)
{
	QCoreApplication app (argc, argv);
	T.start ();

	say ("старт");
	printf ("           Qt %s, сборка с SSL: %s\n", qVersion(),
	        QSslSocket::supportsSsl() ? "да" : "НЕТ");
	fflush (stdout);
	say ("после supportsSsl()");
	printf ("           библиотека: %s / %s\n",
	        qPrintable (QSslSocket::sslLibraryBuildVersionString()),
	        qPrintable (QSslSocket::sslLibraryVersionString()));
	fflush (stdout);

	QNetworkAccessManager mgr;
	say ("менеджер создан");

	const char *urls[] = {
		"http://grbsrv.opengribs.org/getversion.php",
		"https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl?dir=%2Fgfs.20260909%2F00%2Fatmos&file=gfs.t00z.pgrb2.0p25.f000&var_PRMSL=on&lev_mean_sea_level=on&subregion=&toplat=-34&leftlon=96&rightlon=104&bottomlat=-42"
	};
	for (int i=0; i<2; i++) {
		printf ("\n--- запрос %d: %.60s...\n", i+1, urls[i]);
		fflush (stdout);
		QNetworkRequest req { QUrl(urls[i]) };
		req.setRawHeader ("User-Agent", "MAKGrib/1.0.0");
		say ("перед get()");
		QNetworkReply *r = mgr.get (req);
		say ("после get()");

		QEventLoop loop;
		QTimer t; t.setSingleShot (true);
		QObject::connect (r, SIGNAL(finished()), &loop, SLOT(quit()));
		QObject::connect (&t, SIGNAL(timeout()), &loop, SLOT(quit()));
		t.start (20000);
		loop.exec ();
		if (r->isFinished())
			printf ("[%7.2f c] готово: ошибка=%d, байт=%lld\n",
			        T.elapsed()/1000.0, (int)r->error(),
			        (long long) r->readAll().size());
		else
			say ("НЕ ОТВЕТИЛ за 20 c");
		r->abort ();
		r->deleteLater ();
	}
	say ("конец");
	return 0;
}
