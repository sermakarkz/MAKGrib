/**********************************************************************
XyGrib: meteorological GRIB file viewer
Copyright (C) 2008-2012 - Jacques Zaninetti - http://www.zygrib.org

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
***********************************************************************/

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QTimer>
#include <QElapsedTimer>
#include <QUrl>

#include <QBuffer>
#include <cstring>

#include <atomic>
#include <thread>

#include "NomadsLoader.h"
#include "Util.h"
#include "WinHttpFetch.h"

static const char *NOMADS_CGI = "https://nomads.ncep.noaa.gov/cgi-bin/";

// The fields XyGrib asks the OpenGribs server for, named the way NOAA's
// filter script names them. NOMADS crosses variables with levels, so a
// few records arrive that nobody asked for - temperature at the ground,
// the orography, humidity on the 0 °C surface. They are small, they sit
// on levels of their own, and dropping them would cost one extra request
// per forecast hour, so they are simply left in.
static const char *ATM_FIELDS =
        "&var_UGRD=on&var_VGRD=on&var_GUST=on&var_PRMSL=on&var_TMP=on"
        "&var_RH=on&var_TCDC=on&var_APCP=on&var_CAPE=on&var_CIN=on&var_HGT=on"
        "&lev_10_m_above_ground=on&lev_2_m_above_ground=on"
        "&lev_mean_sea_level=on&lev_surface=on&lev_entire_atmosphere=on"
        "&lev_0C_isotherm=on";

// Significant height, swell and wind wave, each with height, direction
// and period. Swell is published as a sequence of components; only the
// first one is taken, which is what the wave display shows.
static const char *WAVE_FIELDS =
        "&var_HTSGW=on&var_PERPW=on&var_DIRPW=on&var_WVHGT=on&var_WVPER=on"
        "&var_WVDIR=on&var_SWELL=on&var_SWPER=on&var_SWDIR=on"
        "&lev_surface=on&lev_1_in_sequence=on";

// One field, enough to tell whether a forecast hour is on disk yet.
static const char *ATM_PROBE  = "&var_PRMSL=on&lev_mean_sea_level=on";
static const char *WAVE_PROBE = "&var_HTSGW=on&lev_surface=on";

// GFS and GFS-Wave both stop here.
static const int MAX_HOUR = 384;
// Up to this hour NOAA publishes every hour; past it, every three.
static const int HOURLY_UNTIL = 120;
// Runs to look back through when finding the latest complete one: 48 h.
static const int RUNS_TRIED = 8;
// NOMADS is a shared public service and asks callers not to hammer it.
static const int MIN_GAP_MS = 250;

// Roughly what one forecast hour weighs once NOAA has packed it: this
// many bytes per grid point for the whole set of fields. Measured on
// boxes from 7 degrees square up to the whole globe, where it holds to
// within a few per cent. Wave files are mostly sea mask and pack harder.
static const double ATM_BYTES_PER_POINT  = 24.0;
static const double WAVE_BYTES_PER_POINT = 8.0;
// Past this, a direct pull stops being a fallback and becomes an
// unattended multi-gigabyte download. The whole world at 0.25° is 25 MB
// for a single forecast hour, so ten days of it is two gigabytes - which
// from the outside is indistinguishable from the program having hung.
// The OpenGribs server refuses anything over 50 MB; this leaves more
// room and still stops "every model for the whole world" before it
// starts.
static const double MAX_TOTAL_BYTES = 150.0*1024*1024;
// NOAA is assumed to manage at least this much, which is what decides
// how long one forecast hour may take before it counts as stalled.
static const double SLOWEST_BYTES_PER_SEC = 20000.0;
static const int MIN_TIMEOUT_MS = 30000;
static const int MAX_TIMEOUT_MS = 180000;
// A stalled forecast hour is asked for this many times before the run
// is given up.
static const int TRIES_PER_HOUR = 3;
// Probing for the newest run must fail fast. Eight candidates at the
// full timeout would leave the window sitting there for four minutes,
// which from the outside is a hung program, not a slow one.
static const int PROBE_TIMEOUT_MS = 15000;

//---------------------------------------------------------------------
// Runs the event loop for a while, so the window keeps painting and the
// Cancel button keeps working.
static void pause (int ms, NomadsProgress *progress)
{
	QEventLoop loop;
	QTimer::singleShot (ms, &loop, SLOT(quit()));
	if (progress != nullptr) {
		// The loop below dispatches events itself, painting included, so
		// there is no processEvents() call here: made from inside a loop
		// that is already running, it only stacks one more loop on top.
		QTimer ticker;
		QObject::connect (&ticker, &QTimer::timeout, [&]() {
			if (progress->canceled())
				loop.quit ();
		});
		ticker.start (50);
		loop.exec ();
		ticker.stop ();
	}
	else {
		loop.exec ();
	}
}

#ifdef Q_OS_WIN
//---------------------------------------------------------------------
// На Windows качаем через WinHTTP, а не через Qt: OpenSSL в mingw-сборке
// Qt ищет сертификаты по линуксовым путям и на Windows не находит ни
// одного, из-за чего https к NOAA не работает вовсе. Хранилище Windows
// всегда на месте и обновляется системой.
//
// Сам вызов синхронный, поэтому уходит в отдельный поток, а здесь мы
// ждём его через цикл событий: окно продолжает жить, «Стоп» работает.
static QByteArray winGet (const QString &url, int timeoutMs,
                          NomadsProgress *progress, bool *canceled,
                          bool *noAnswer, QString *why)
{
	std::atomic<bool> done (false);
	QByteArray body;
	QString    err;
	int        status = 0;

	std::thread worker ([&]() {
		body = winHttpFetch (url, timeoutMs, &err, &status);
		done.store (true);
	});

	QEventLoop loop;
	QTimer tick;
	QElapsedTimer spent; spent.start ();
	QObject::connect (&tick, &QTimer::timeout, [&]() {
		if (done.load()) {
			loop.quit ();
			return;
		}
		if (progress != nullptr && progress->canceled()) {
			*canceled = true;
			loop.quit ();
		}
		// WinHTTP свои таймауты соблюдает сам, но подстрахуемся: поток
		// не должен пережить ожидание больше чем вдвое.
		if (spent.elapsed() > 2*timeoutMs + 5000)
			loop.quit ();
	});
	tick.start (100);
	loop.exec ();
	tick.stop ();

	// Поток пишет в наши переменные, поэтому дожидаемся его в любом
	// случае — иначе он писал бы в уже уничтоженный стек.
	worker.join ();

	if (noAnswer != nullptr)
		*noAnswer = !*canceled && status == 0;
	if (why != nullptr)
		*why = err;
	return (*canceled) ? QByteArray() : body;
}
#endif

//---------------------------------------------------------------------
// A blocking GET. Returns the body - empty when the request timed out or
// the connection broke, which is a different ending from an answer that
// is not a GRIB. Sets *canceled when Cancel was pressed meanwhile.
static QByteArray syncGet (QNetworkAccessManager &mgr, const QString &url,
                           int timeoutMs, NomadsProgress *progress,
                           bool *canceled, bool *noAnswer = nullptr,
                           QString *why = nullptr)
{
#ifdef Q_OS_WIN
	Q_UNUSED (mgr)
	return winGet (url, timeoutMs, progress, canceled, noAnswer, why);
#else
	QNetworkReply *reply = mgr.get (Util::makeNetworkRequest (url));

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot (true);
	QObject::connect (reply,    SIGNAL(finished()), &loop, SLOT(quit()));
	QObject::connect (&timeout, SIGNAL(timeout()),  &loop, SLOT(quit()));

	// Only watches the Cancel button; loop.exec() keeps the window alive.
	QTimer ticker;
	if (progress != nullptr) {
		QObject::connect (&ticker, &QTimer::timeout, [&]() {
			if (progress->canceled()) {
				*canceled = true;
				loop.quit ();
			}
		});
		ticker.start (100);
	}

	timeout.start (timeoutMs);
	loop.exec ();
	ticker.stop ();

	QByteArray body;
	if (!*canceled && reply->isFinished()
	        && reply->error() == QNetworkReply::NoError)
		body = reply->readAll ();

	// «Сервер не отвечает» и «сервер ответил 404» — разные вещи. Первое
	// значит, что связи нет и перебирать циклы бессмысленно; второе, что
	// этого расчёта ещё нет и надо взять предыдущий. Различаем по тому,
	// пришёл ли вообще код HTTP.
	if (noAnswer != nullptr) {
		QVariant code = reply->attribute (QNetworkRequest::HttpStatusCodeAttribute);
		*noAnswer = !*canceled && !code.isValid();
	}
	if (why != nullptr)
		*why = reply->isFinished() ? reply->errorString()
		                           : QObject::tr("no answer in time");

	if (!reply->isFinished())
		reply->abort ();
	reply->deleteLater ();
	return body;
#endif
}

//---------------------------------------------------------------------
// The area, in the form NOAA's filter script wants. A box that has
// wrapped past the antimeridian cannot be expressed here, so such a
// request falls back to the whole latitude band.
static void normalizeBox (double x0, double y0, double x1, double y1,
                          double *lo1, double *lo2, double *la1, double *la2)
{
	auto wrap = [](double lon) {
		while (lon >  180.0) lon -= 360.0;
		while (lon < -180.0) lon += 360.0;
		return lon;
	};
	*lo1 = wrap (qMin (x0, x1));
	*lo2 = wrap (qMax (x0, x1));
	*la1 = qMax (-90.0, qMin (y0, y1));
	*la2 = qMin ( 90.0, qMax (y0, y1));

	if (*lo2 <= *lo1) {        // wrapped: ask for everything at these latitudes
		*lo1 = -180.0;
		*lo2 =  180.0;
	}
	else {
		// A hairline box comes back one grid column wide, which loads as a
		// valid GRIB carrying nothing. Half a degree of margin avoids it.
		*lo1 = qMax (-180.0, *lo1 - 0.5);
		*lo2 = qMin ( 180.0, *lo2 + 0.5);
	}
	*la1 = qMax (-90.0, *la1 - 0.5);
	*la2 = qMin ( 90.0, *la2 + 0.5);
}

//---------------------------------------------------------------------
static QString boxQuery (double lo1, double lo2, double la1, double la2)
{
	return QString("&subregion=&toplat=%1&leftlon=%2&rightlon=%3&bottomlat=%4")
	        .arg (la2, 0, 'f', 2).arg (lo1, 0, 'f', 2)
	        .arg (lo2, 0, 'f', 2).arg (la1, 0, 'f', 2);
}

//---------------------------------------------------------------------
// What one forecast hour of this box is going to weigh. Both models are
// published on the same 0.25° grid.
static double bytesPerStep (bool wave,
                            double lo1, double lo2, double la1, double la2)
{
	double points = ((lo2-lo1)/0.25 + 1.0) * ((la2-la1)/0.25 + 1.0);
	return points * (wave ? WAVE_BYTES_PER_POINT : ATM_BYTES_PER_POINT);
}

//---------------------------------------------------------------------
// True when every message in this chunk carries a bitmap that marks all
// of its points as missing. NOAA's global wave model is defined on the
// open sea only: asked about an enclosed basin such as the Caspian or
// the Black Sea it answers with a properly formed field in which
// nothing at all is defined.
static bool allPointsMasked (const QByteArray &d)
{
	const unsigned char *b = reinterpret_cast<const unsigned char*>(d.constData());
	auto u32 = [&](int at) {
		return (quint32(b[at])<<24) | (quint32(b[at+1])<<16)
		     | (quint32(b[at+2])<<8) | quint32(b[at+3]);
	};

	int p = 0, messages = 0;
	while (p + 16 <= d.size() && memcmp (b+p, "GRIB", 4) == 0)
	{
		quint64 tlen = 0;
		for (int k=0; k<8; k++)
			tlen = (tlen<<8) | b[p+8+k];
		if (tlen == 0 || p + int(tlen) > d.size())
			break;

		bool masked = false;
		int q = p + 16;
		while (q + 5 < p + int(tlen)) {
			quint32 sl = u32(q);
			int sn = b[q+4];
			if (sl == 0 || q + int(sl) > d.size())
				break;
			if (sn == 6) {                  // bit map section
				if (b[q+5] == 0) {          // a bitmap is present
					masked = true;
					for (int k=q+6; k<q+int(sl) && masked; k++)
						if (b[k] != 0)
							masked = false;
				}
				break;                       // one such section per message
			}
			q += sl;
		}
		if (!masked)
			return false;
		messages++;
		p += int(tlen);
	}
	return messages > 0;
}

//---------------------------------------------------------------------
static QString buildUrl (bool wave, const QDate &day, int cyc, int hour,
                         const QString &box)
{
	QString ymd = day.toString ("yyyyMMdd");
	QString cc  = QString("%1").arg (cyc,  2, 10, QChar('0'));
	QString fff = QString("%1").arg (hour, 3, 10, QChar('0'));

	QString dir, file, script;
	if (wave) {
		script = "filter_gfswave.pl";
		dir    = "/gfs." + ymd + "/" + cc + "/wave/gridded";
		file   = "gfswave.t" + cc + "z.global.0p25.f" + fff + ".grib2";
	}
	else {
		script = "filter_gfs_0p25.pl";
		dir    = "/gfs." + ymd + "/" + cc + "/atmos";
		file   = "gfs.t" + cc + "z.pgrb2.0p25.f" + fff;
	}

	return QString (NOMADS_CGI) + script
	     + "?dir=" + QString::fromLatin1 (QUrl::toPercentEncoding (dir))
	     + "&file=" + file
	     + box;
}

//---------------------------------------------------------------------
// The forecast hours to ask for, following what NOAA actually publishes.
static QList<int> forecastHours (int days, int interval)
{
	QList<int> hours;
	int last = days*24;
	if (last > MAX_HOUR)
		last = MAX_HOUR;
	int step = (interval > 0) ? interval : 3;

	for (int h=0; h<=last; ) {
		hours << h;
		h += (h >= HOURLY_UNTIL && step < 3) ? 3 : step;
	}
	return hours;
}

//---------------------------------------------------------------------
// Finds the newest run that is complete to the depth wanted. The deepest
// hour is what gets probed: f000 of a run appears hours before its tail,
// and a run whose tail is missing would give a forecast that stops short.
static bool findRun (QNetworkAccessManager &mgr, bool wave,
                     const QString &box, int deepest,
                     NomadsProgress *progress,
                     QDate *day, int *cyc, bool *canceled,
                     bool *unreachable, QString *why)
{
	*unreachable = false;
	QDateTime now = QDateTime::currentDateTimeUtc ();
	QDateTime latest (now.date(), QTime ((now.time().hour()/6)*6, 0), Qt::UTC);

	for (int k=0; k<RUNS_TRIED; k++)
	{
		QDateTime run = latest.addSecs (-k*6*3600);
		QString url = buildUrl (wave, run.date(), run.time().hour(), deepest, box)
		            + (wave ? WAVE_PROBE : ATM_PROBE);

		if (progress != nullptr)
			progress->message (QObject::tr("checking the NOAA run of %1 %2z...")
			        .arg (run.date().toString("dd.MM"))
			        .arg (run.time().hour(), 2, 10, QChar('0')));

		bool noAnswer = false;
		QByteArray d = syncGet (mgr, url, PROBE_TIMEOUT_MS, progress,
		                        canceled, &noAnswer, why);
		if (*canceled)
			return false;
		if (d.startsWith ("GRIB")) {
			*day = run.date ();
			*cyc = run.time().hour ();
			return true;
		}
		// Nothing came back at all. Walking through seven more runs would
		// take minutes and cannot help: this is not "the run is not ready
		// yet", it is "NOAA cannot be reached from here".
		if (noAnswer) {
			*unreachable = true;
			return false;
		}
		pause (MIN_GAP_MS, progress);
		if (progress != nullptr && progress->canceled()) {
			*canceled = true;
			return false;
		}
	}
	return false;
}

//---------------------------------------------------------------------
bool NomadsLoader::covers (const QString &modelCode)
{
	return modelCode.startsWith ("gfs_") || modelCode.startsWith ("ww3_");
}

//---------------------------------------------------------------------
// The work itself. Everything is appended to sink, which is a file for
// fetch() and a buffer for fetchInto().
static NomadsLoader::Outcome runFetch (
                        const QString &modelCode,
                        double x0, double y0, double x1, double y1,
                        int days, int interval,
                        QIODevice &sink, NomadsProgress *progress)
{
	NomadsLoader::Outcome out;
	if (!NomadsLoader::covers (modelCode)) {
		out.error = QObject::tr("NOAA does not publish this model");
		return out;
	}
	bool wave = modelCode.startsWith ("ww3_");

	QList<int> hours = forecastHours (days, interval);
	if (hours.isEmpty()) {
		out.error = QObject::tr("nothing to ask for");
		return out;
	}

#ifndef Q_OS_WIN
	// NOMADS перенаправляет http на https. На Windows этим занимается
	// WinHTTP, ему ничего не нужно; на остальных системах шифрование
	// берётся у Qt, и оно должно быть готово. Спрашиваем заранее и с
	// ограничением по времени: иначе запрос может встать внутри своей
	// же инициализации и утащить туда всё окно.
	if (!Util::sslReady (5000)) {
		out.error = QObject::tr("HTTPS is not working in this build, and NOAA "
		                        "requires it");
		return out;
	}
#endif

	double lo1, lo2, la1, la2;
	normalizeBox (x0, y0, x1, y1, &lo1, &lo2, &la1, &la2);
	QString box = boxQuery (lo1, lo2, la1, la2);

	// NOAA serves whatever subset it is asked for, with none of the size
	// limits the OpenGribs server applies. A request made with the map
	// zoomed right out would quietly become a two-gigabyte download, so
	// it is refused here rather than discovered somewhere in the middle.
	double perStep = bytesPerStep (wave, lo1, lo2, la1, la2);
	double total   = perStep * hours.size();
	if (total > MAX_TOTAL_BYTES) {
		out.error = QObject::tr("area too large for a direct NOAA download "
		                        "(about %1 MB) — select a smaller area first")
		            .arg (total/(1024.0*1024.0), 0, 'f', 0);
		return out;
	}

	// A forecast hour that has not arrived by the time even a very slow
	// link would have delivered it counts as stalled, not as the end of
	// the run.
	int timeoutMs = qBound (MIN_TIMEOUT_MS,
	                        int (1000.0 * perStep / SLOWEST_BYTES_PER_SEC),
	                        MAX_TIMEOUT_MS);

	QNetworkAccessManager mgr;
	bool canceled = false;

	if (progress != nullptr)
		progress->message (QObject::tr("looking for the latest NOAA run..."));

	QDate day;
	int cyc = 0;
	bool unreachable = false;
	QString why;
	if (!findRun (mgr, wave, box, hours.last(), progress, &day, &cyc,
	              &canceled, &unreachable, &why)) {
		if (canceled)
			out.error = QObject::tr("canceled");
		else if (unreachable)
			out.error = QObject::tr("NOAA is not answering (%1) — check the "
			                        "internet connection").arg (why);
		else
			out.error = QObject::tr("no complete run on NOAA either");
		return out;
	}
	out.run = day.toString("yyyy-MM-dd") + " "
	        + QString("%1").arg(cyc, 2, 10, QChar('0')) + "z";
	out.name = QString("%1_0p25_%2_%3z_NOMADS.grb2")
	        .arg (wave ? "GFSWAVE" : "GFS")
	        .arg (day.toString("yyyyMMdd"))
	        .arg (cyc, 2, 10, QChar('0'));

	qint64 written = 0;
	for (int i=0; i<hours.size(); i++)
	{
		if (progress != nullptr) {
			if (progress->canceled()) {
				canceled = true;
				break;
			}
			progress->step (i, hours.size(), written);
		}

		QString url = buildUrl (wave, day, cyc, hours.at(i), box)
		            + (wave ? WAVE_FIELDS : ATM_FIELDS);

		// Two different endings look alike from here. A page that is not a
		// GRIB means the run genuinely stops at this hour, and asking
		// again would be pointless. An empty answer means the request
		// timed out or the connection broke, which is worth another go
		// before the whole forecast is cut short over one bad moment.
		QByteArray chunk;
		bool stalled = false;
		for (int attempt=1; ; attempt++) {
			chunk = syncGet (mgr, url, timeoutMs, progress, &canceled);
			if (canceled || !chunk.isEmpty())
				break;
			if (attempt >= TRIES_PER_HOUR) {
				stalled = true;
				break;
			}
			if (progress != nullptr)
				progress->message (QObject::tr("no answer, trying again"));
			pause (2000*attempt, progress);
			if (progress != nullptr && progress->canceled()) {
				canceled = true;
				break;
			}
		}
		if (canceled)
			break;

		if (!chunk.startsWith ("GRIB")) {
			out.truncated = true;
			out.error = stalled
			     ? QObject::tr("NOAA stopped answering at +%1 h").arg (hours.at(i))
			     : QObject::tr("that run ends at +%1 h").arg (out.hours);
			break;
		}
		// The first hour already says whether this model has anything to
		// give here. Without this the download would run its eighty-odd
		// requests to the end and produce a forecast that draws nothing.
		if (i == 0 && allPointsMasked (chunk)) {
			out.error = wave
			    ? QObject::tr("NOAA's wave model has no data here — enclosed "
			                  "seas such as the Caspian and the Black Sea lie "
			                  "outside its grid")
			    : QObject::tr("NOAA has no data for this area");
			out.hours = 0;
			return out;
		}
		if (sink.write (chunk) != chunk.size()) {
			out.error = QObject::tr("could not write the downloaded data");
			out.hours = 0;
			return out;
		}
		written += chunk.size ();
		out.hours = hours.at (i);
		pause (MIN_GAP_MS, progress);
	}

	// One record is not a forecast. A second step is the least that draws
	// an animation, and less than that means the run was not really there.
	if (canceled || out.hours < interval) {
		out.error = canceled ? QObject::tr("canceled")
		                     : QObject::tr("NOAA returned nothing usable");
		out.hours = 0;
		out.truncated = false;
		return out;
	}

	out.ok = true;
	return out;
}

//---------------------------------------------------------------------
NomadsLoader::Outcome NomadsLoader::fetch (
                        const QString &modelCode,
                        double x0, double y0, double x1, double y1,
                        int days, int interval, const QString &destDir,
                        NomadsProgress *progress)
{
	// Written as it arrives: a deep run over a wide box is too big to
	// hold in memory, and a GRIB file is just its messages end to end.
	// The name is only known once the run has been found, so the file
	// starts out under a temporary one.
	QString tmpPath = QDir(destDir).absoluteFilePath (".xygrib-nomads.part");
	QFile f (tmpPath);
	if (!f.open (QIODevice::WriteOnly)) {
		Outcome out;
		out.error = tr("cannot write") + " " + tmpPath;
		return out;
	}
	Outcome out = runFetch (modelCode, x0, y0, x1, y1, days, interval,
	                        f, progress);
	f.close ();

	if (!out.ok) {
		QFile::remove (tmpPath);
		return out;
	}
	QString path = QDir(destDir).absoluteFilePath (out.name);
	QFile::remove (path);
	if (!QFile::rename (tmpPath, path)) {
		QFile::remove (tmpPath);
		out.ok = false;
		out.error = tr("cannot write") + " " + path;
		return out;
	}
	out.path = path;
	return out;
}

//---------------------------------------------------------------------
NomadsLoader::Outcome NomadsLoader::fetchInto (
                        QByteArray *out, const QString &modelCode,
                        double x0, double y0, double x1, double y1,
                        int days, int interval, NomadsProgress *progress)
{
	QBuffer buf (out);
	// Append: called twice, the atmosphere and the waves end up in one
	// file, which is exactly what the download dialog asks for.
	buf.open (QIODevice::WriteOnly | QIODevice::Append);
	Outcome res = runFetch (modelCode, x0, y0, x1, y1, days, interval,
	                        buf, progress);
	buf.close ();
	return res;
}
