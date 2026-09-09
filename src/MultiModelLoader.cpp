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
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QSysInfo>
#include <QTimer>

#include "MultiModelLoader.h"
#include "DialogForecastProgress.h"
#include "SourceRegistry.h"
#include "Util.h"
#include "Version.h"

// Atmospheric fields worth having for a passage: wind, pressure, rain,
// cloud, temperature, humidity, 0 °C isotherm, CAPE, CIN, gusts.
static const char *ATM_PARAMS  = "W;P;R;C;T;H;I;c;i;G;";
// Significant waves, swell and wind wave, each with height/direction/period.
static const char *WAVE_PARAMS = "s;H;D;P;h;d;p;";

// The server refuses ICON-EU as soon as the western edge of the request
// sits east of this meridian, although the model itself reaches 62.5°E
// (checked against DWD's own ICON-EU grid). Moving the western edge here
// and asking again is enough to get the data.
// Other nests refuse for a real reason - ARPEGE-EU genuinely ends at
// 42°E - and for those the retry simply fails again, or brings back a
// degenerate one-column file that isDegenerateGrib() below rejects.
static const double NEST_WEST_LIMIT = 45.0;

//---------------------------------------------------------------------
// A model asked for an area it does not really cover answers with a
// sliver of its edge: a grid one point wide. Such a file loads as valid
// GRIB but carries nothing usable, so it must not become a forecast.
static bool isDegenerateGrib (const QByteArray &d)
{
	int p = 0;
	if (d.size() < 32 || !d.startsWith("GRIB"))
		return true;

	const unsigned char *b = reinterpret_cast<const unsigned char*>(d.constData());
	auto u32 = [&](int at) {
		return (quint32(b[at])<<24) | (quint32(b[at+1])<<16)
		     | (quint32(b[at+2])<<8) | quint32(b[at+3]);
	};

	// Walk the sections of the first message looking for the grid (3).
	quint64 len = 0;
	for (int k=0; k<8; k++)
		len = (len<<8) | b[8+k];
	if (len == 0 || static_cast<int>(len) > d.size())
		return true;

	int q = p + 16;
	while (q < static_cast<int>(len) - 4) {
		quint32 sl = u32(q);
		int sn = b[q+4];
		if (sl == 0 || q + static_cast<int>(sl) > d.size())
			break;
		if (sn == 3) {
			quint32 ni = u32(q+30);
			quint32 nj = u32(q+34);
			return (ni < 2 || nj < 2);
		}
		q += sl;
	}
	return false;
}

//---------------------------------------------------------------------
// Reads the grid corners of the first message. Returns false when the
// file cannot be understood.
static bool gribGridBounds (const QByteArray &d,
                            double *lo1, double *lo2, double *la1, double *la2)
{
	if (d.size() < 32 || !d.startsWith("GRIB"))
		return false;
	const unsigned char *b = reinterpret_cast<const unsigned char*>(d.constData());
	auto u32 = [&](int at) {
		return (quint32(b[at])<<24) | (quint32(b[at+1])<<16)
		     | (quint32(b[at+2])<<8) | quint32(b[at+3]);
	};
	auto deg = [&](int at) {
		quint32 v = u32(at);
		double x = (v < 0x80000000u) ? double(v)/1e6
		                             : (double(v) - 4294967296.0)/1e6;
		while (x > 180.0)  x -= 360.0;
		while (x < -180.0) x += 360.0;
		return x;
	};

	quint64 len = 0;
	for (int k=0; k<8; k++)
		len = (len<<8) | b[8+k];
	if (len == 0 || static_cast<int>(len) > d.size())
		return false;

	int q = 16;
	while (q < static_cast<int>(len) - 4) {
		quint32 sl = u32(q);
		int sn = b[q+4];
		if (sl == 0 || q + static_cast<int>(sl) > d.size())
			break;
		if (sn == 3) {
			double laA = deg(q+46), loA = deg(q+50);
			double laB = deg(q+55), loB = deg(q+59);
			*la1 = qMin(laA,laB);  *la2 = qMax(laA,laB);
			*lo1 = qMin(loA,loB);  *lo2 = qMax(loA,loB);
			return true;
		}
		q += sl;
	}
	return false;
}
//---------------------------------------------------------------------
MultiModelLoader::MultiModelLoader (QObject *parent) : QObject (parent)
{
}
//---------------------------------------------------------------------
QList<MultiModelLoader::ModelDef> MultiModelLoader::defaultModels ()
{
	// Depth and step are what each model actually publishes, taken from
	// the limits the download dialog offers for it.
	QList<ModelDef> lst;
	lst << ModelDef {QObject::tr("GFS 0.25°"),      "gfs_p25_",       false, 10, 3}
	    << ModelDef {QObject::tr("ICON Global"),    "icon_p25_",      false,  8, 3}
	    << ModelDef {QObject::tr("ICON-EU 7 km"),   "icon_eu_p06_",   false,  5, 1}
	    << ModelDef {QObject::tr("ARPEGE Global"),  "arpege_p50_",    false,  4, 3}
	    << ModelDef {QObject::tr("ARPEGE-EU 10 km"),"arpege_eu_p10_", false,  3, 1}
	    << ModelDef {QObject::tr("AROME 2.5 km"),   "arome_p025_",    false,  2, 1}
	    << ModelDef {QObject::tr("GWAM waves"),     "gwam_p25_",      true,   8, 3}
	    << ModelDef {QObject::tr("EWAM waves"),     "ewam_p05_",      true,   4, 3}
	    << ModelDef {QObject::tr("WW3 waves"),      "ww3_p50_",       true,   8, 3};
	return lst;
}

//---------------------------------------------------------------------
QString MultiModelLoader::askServer (const ModelDef &m,
                                     double x0, double y0, double x1, double y1,
                                     int days, int interval, QString *error,
                                     QProgressDialog *progress)
{
	QString ptype = "Unknown";
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
	ptype = QSysInfo::productType();
#endif

	QString url = QString("http://%1/getmygribs2.php?osys=%2&ver=%3"
	                      "&model=%4&la1=%5&la2=%6&lo1=%7&lo2=%8"
	                      "&intv=%9&days=%10&cyc=last&par=%11&wmdl=%12&wpar=%13")
	        .arg (Util::getServerName())
	        .arg (ptype)
	        .arg (Version::getVersion())
	        .arg (m.isWave ? "none" : m.code)
	        .arg (y0, 0, 'f', 2).arg (y1, 0, 'f', 2)
	        .arg (x0, 0, 'f', 2).arg (x1, 0, 'f', 2)
	        .arg (interval).arg (days)
	        .arg (m.isWave ? "" : ATM_PARAMS)
	        .arg (m.isWave ? m.code : "none")
	        .arg (m.isWave ? WAVE_PARAMS : "");

	QNetworkAccessManager mgr;
	QNetworkReply *reply = mgr.get (Util::makeNetworkRequest (url));

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot (true);
	connect (reply,    SIGNAL(finished()), &loop, SLOT(quit()));
	connect (&timeout, SIGNAL(timeout()),  &loop, SLOT(quit()));

	// Preparing a high resolution file takes the server a while; keep the
	// window responsive and the Cancel button usable meanwhile.
	QTimer ticker;
	if (progress != nullptr) {
		connect (&ticker, &QTimer::timeout, [&]() {
			QApplication::processEvents ();
			if (progress->wasCanceled())
				loop.quit ();
		});
		ticker.start (100);
	}

	timeout.start (90000);
	loop.exec ();
	ticker.stop ();

	if (!reply->isFinished()) {
		reply->abort ();
		*error = tr("no answer from the server");
		reply->deleteLater ();
		return QString();
	}
	if (reply->error() != QNetworkReply::NoError) {
		*error = reply->errorString();
		reply->deleteLater ();
		return QString();
	}

	QByteArray body = reply->readAll ();
	reply->deleteLater ();

	QJsonParseError perr;
	QJsonDocument doc = QJsonDocument::fromJson (body, &perr);
	if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
		*error = tr("unreadable answer");
		return QString();
	}
	QJsonObject obj = doc.object ();
	if (!obj.value("status").toBool()) {
		*error = obj.value("message").toString (tr("refused"));
		return QString();
	}
	return obj.value("message").toObject().value("url").toString();
}

//---------------------------------------------------------------------
bool MultiModelLoader::download (const QString &url, const QString &path,
                                 QProgressDialog *progress, int baseValue,
                                 const QString &label)
{
	QNetworkAccessManager mgr;
	QNetworkReply *reply = mgr.get (Util::makeNetworkRequest (url));

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot (true);
	connect (reply,    SIGNAL(finished()), &loop, SLOT(quit()));
	connect (&timeout, SIGNAL(timeout()),  &loop, SLOT(quit()));

	// Keep the window alive and the bar moving while the file arrives.
	QTimer ticker;
	bool canceled = false;
	if (progress != nullptr) {
		connect (reply, &QNetworkReply::downloadProgress,
		         [&](qint64 done, qint64 total) {
			int pct = (total > 0) ? int (100.0*done/total) : 0;
			progress->setValue (baseValue + pct);
			progress->setLabelText (label + QString("  %1 %2 %3 KB")
			        .arg(pct).arg("%").arg(done/1024));
		});
		connect (&ticker, &QTimer::timeout, [&]() {
			QApplication::processEvents ();
			if (progress->wasCanceled()) {
				canceled = true;
				loop.quit ();
			}
		});
		ticker.start (100);
	}

	timeout.start (300000);
	loop.exec ();
	ticker.stop ();

	if (canceled) {
		reply->abort ();
		reply->deleteLater ();
		return false;
	}

	bool ok = reply->isFinished() && reply->error() == QNetworkReply::NoError;
	QByteArray data = ok ? reply->readAll() : QByteArray();
	if (!reply->isFinished())
		reply->abort ();
	reply->deleteLater ();

	// A GRIB always starts with "GRIB"; anything else is an error page.
	if (!ok || data.size() < 100 || !data.startsWith("GRIB"))
		return false;
	if (isDegenerateGrib (data))
		return false;

	QFile f (path);
	if (!f.open (QIODevice::WriteOnly))
		return false;
	bool written = (f.write (data) == data.size());
	f.close ();
	return written;
}

//---------------------------------------------------------------------
MultiModelLoader::Result MultiModelLoader::fetch (
                        const ModelDef &m,
                        double x0, double y0, double x1, double y1,
                        int days, int interval, const QString &destDir,
                        QProgressDialog *progress)
{
	Result r;
	r.label    = m.label;
	r.ok       = false;
	r.widened  = false;
	r.days     = days;
	r.interval = interval;
	r.hours    = days*24;

	QString error;
	QString url = askServer (m, x0, y0, x1, y1, days, interval, &error, progress);

	// The European nests are refused for a purely eastern box; retry with
	// the western edge pulled back to the limit the server accepts.
	if (url.isEmpty() && error.contains("out of bounds", Qt::CaseInsensitive)
	        && x0 > NEST_WEST_LIMIT) {
		QString retryError;
		url = askServer (m, NEST_WEST_LIMIT, y0, x1, y1, days, interval,
						 &retryError, progress);
		if (!url.isEmpty())
			r.widened = true;
		else
			error = retryError;
	}

	// The OpenGribs server has been down for days at a time (issue #326).
	// NOAA keeps publishing GFS and its wave model regardless, so for those
	// two the forecast can still be built, straight from NOMADS.
	ForecastSource *alt = SourceRegistry::instance().sourceFor (m.code);
	if (url.isEmpty() && alt != nullptr) {
		DialogForecastProgress bar (progress,
		        (progress != nullptr) ? progress->value() : 0, m.label);
		ForecastSource::Request rq {m.code, x0, y0, x1, y1, days, interval};
		QString gotPath;
		ForecastSource::Result n = alt->fetchToFile (rq, destDir, &gotPath,
		        (progress != nullptr) ? &bar : nullptr);
		if (n.ok) {
			r.ok       = true;
			r.fileName = gotPath;
			r.hours    = n.hours;
			r.days     = n.hours/24;
			// The server's own words are already on the lines of the models
			// that have no NOAA counterpart; here only the source matters.
			r.note = tr("from %1, run %2 — the OpenGribs server did not "
			            "deliver").arg (alt->name()).arg (n.run);
			if (n.truncated)
				r.note += "\n   " + n.error;
			return r;
		}
		r.note = error + " / " + alt->name() + ": " + n.error;
		return r;
	}

	if (url.isEmpty()) {
		r.note = error;
		return r;
	}

	QString name = QUrl(url).fileName();
	if (name.isEmpty())
		name = m.code + "download.grb2";
	QString path = QDir(destDir).absoluteFilePath (name);

	int baseValue = 0;
	if (progress != nullptr) {
		baseValue = progress->value ();
		progress->setLabelText (tr("Downloading")+" "+m.label+"...");
		QApplication::processEvents ();
	}

	if (!download (url, path, progress, baseValue,
	               tr("Downloading")+" "+m.label)) {
		r.note = r.widened ? tr("does not cover this area")
		                   : tr("download failed");
		return r;
	}

	// A model can accept the request and still answer with nothing but the
	// edge of its own domain. Say so plainly instead of adding a forecast
	// that is blank exactly where the user is looking.
	QFile check (path);
	if (check.open (QIODevice::ReadOnly)) {
		QByteArray head = check.read (65536);
		check.close ();
		double glo1, glo2, gla1, gla2;
		if (gribGridBounds (head, &glo1, &glo2, &gla1, &gla2)) {
			double wanted = qMax (0.001, x1 - x0);
			double got    = qMin (x1, glo2) - qMax (x0, glo1);
			if (got < 0.5*wanted) {
				QFile::remove (path);
				r.note = tr("covers this area only up to %1°E")
				         .arg (glo2, 0, 'f', 0);
				return r;
			}
		}
	}

	r.ok = true;
	r.fileName = path;
	if (r.widened)
		r.note = tr("area extended west to %1°E — the server refuses this "
		            "model otherwise").arg (NEST_WEST_LIMIT, 0, 'f', 0);
	return r;
}

//---------------------------------------------------------------------
QList<MultiModelLoader::Result> MultiModelLoader::run (
                        double x0, double y0, double x1, double y1,
                        const QString &destDir,
                        QProgressDialog *progress, int dayCap)
{
	QList<Result> results;
	const QList<ModelDef> models = defaultModels ();

	// 100 steps per model, so the bar also moves inside one download.
	if (progress != nullptr) {
		progress->setRange (0, models.size()*100);
		progress->setValue (0);
	}

	for (int i=0; i<models.size(); i++)
	{
		if (progress != nullptr) {
			if (progress->wasCanceled())
				break;
			progress->setLabelText (tr("Asking the server for")+" "
			                        + models.at(i).label + "...");
			progress->setValue (i*100);
			QApplication::processEvents ();
		}
		int days = models.at(i).maxDays;
		if (dayCap > 0 && dayCap < days)
			days = dayCap;
		results << fetch (models.at(i), x0, y0, x1, y1,
		                  days, models.at(i).minInterval, destDir, progress);
	}

	if (progress != nullptr)
		progress->setValue (models.size()*100);
	return results;
}
