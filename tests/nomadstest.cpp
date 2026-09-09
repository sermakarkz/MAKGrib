// Headless check of the NOAA NOMADS fallback: builds a GFS file and a
// GFS-Wave file straight from NOMADS, then reads them back with XyGrib's
// own GRIB reader and looks for the fields the display needs.
//
// This one really goes to the network. It is a check of the fallback,
// so there is nothing to stub out: the point is that NOAA answers and
// that what comes back is a forecast XyGrib can draw.
#include <cstdio>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QFileInfo>
#include <set>

#include "NomadsLoader.h"
#include "MultiModelLoader.h"
#include "FileLoaderGRIB.h"
#include "GribPlot.h"
#include "GribReader.h"
#include "DataPointInfo.h"
#include "Settings.h"
#include "Util.h"
#include "Font.h"
#include "zuFile.h"

static int failures = 0;

static void check (const char *what, bool ok, const QString &detail="")
{
	printf ("  [%s] %s%s\n", ok ? " OK " : "FAIL", what,
	        detail.isEmpty() ? "" : (" -> " + detail).toUtf8().constData());
	if (!ok) failures++;
}

//-------------------------------------------------------------------
// Loads a file the way Terrain does and reports what is inside it.
static bool readBack (const QString &path, GribPlot &plot, int *steps)
{
	int nbrecs = 0;
	ZUFILE *f = zu_open (qPrintable(path), "rb", ZU_COMPRESS_AUTO);
	if (f != nullptr) {
		GribReader r;
		nbrecs = r.countGribRecords (f);
		zu_close (f);
	}
	printf ("       %d GRIB records counted\n", nbrecs);
	plot.loadFile (path, nullptr, nbrecs);
	if (!plot.isReaderOk())
		return false;
	*steps = (int) plot.getReader()->getListDates().size();
	return true;
}

static void wants (GribPlot &plot, const char *name, int dataType)
{
	check (name, plot.getReader()->hasDataType (dataType));
}

int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	Settings::initializeSettingsDir ();
	Settings::initializeGribFilesDir ();
	Settings::findAppDataDir ();
	Font::loadAllFonts ();

	QString dir = (argc > 1) ? QString(argv[1]) : QDir::tempPath();
	// A small box on the Caspian: little data, quick to fetch, and the
	// area actually in use.
	const double x0=48.0, y0=40.0, x1=54.0, y1=46.0;
	// Waves need open sea. NOAA's global wave grid leaves out enclosed
	// basins, so anything wave-related is checked off Biscay instead.
	const double wx0=-16.0, wy0=42.0, wx1=-10.0, wy1=48.0;

	printf ("\n=== 1. Which models NOMADS stands in for ===\n");
	check ("GFS is covered",      NomadsLoader::covers ("gfs_p25_"));
	check ("WW3 is covered",      NomadsLoader::covers ("ww3_p50_"));
	check ("ICON is not",        !NomadsLoader::covers ("icon_p25_"));
	check ("ARPEGE is not",      !NomadsLoader::covers ("arpege_p50_"));
	check ("GWAM is not",        !NomadsLoader::covers ("gwam_p25_"));

	printf ("\n=== 2. GFS from NOMADS ===\n");
	// One day at 6 h: nine requests, enough to prove the whole chain.
	NomadsLoader::Outcome atm = NomadsLoader::fetch (
	        "gfs_p25_", x0, y0, x1, y1, 1, 6, dir);
	printf ("       run %s, reaches +%d h%s\n",
	        qPrintable (atm.run), atm.hours, atm.truncated ? " (truncated)" : "");
	check ("file was written", !atm.path.isEmpty(), atm.error);

	if (!atm.path.isEmpty()) {
		printf ("       %s, %lld KB\n", qPrintable (QFileInfo(atm.path).fileName()),
		        (long long) QFileInfo(atm.path).size()/1024);
		check ("run is named", !atm.run.isEmpty());
		check ("reaches a full day", atm.hours == 24, QString::number(atm.hours));

		GribPlot plot;
		int steps = 0;
		check ("XyGrib reads it back", readBack (atm.path, plot, &steps));
		if (plot.isReaderOk()) {
			printf ("       %d forecast steps in file\n", steps);
			// 0, 6, 12, 18, 24 h.
			check ("five forecast steps", steps == 5, QString::number(steps));
			wants (plot, "wind",              GRB_WIND_VX);
			wants (plot, "pressure at sea level", GRB_PRESSURE_MSL);
			wants (plot, "temperature",       GRB_TEMP);
			wants (plot, "relative humidity", GRB_HUMID_REL);
			wants (plot, "total cloud",       GRB_CLOUD_TOT);
			wants (plot, "precipitation",     GRB_PRECIP_TOT);
			wants (plot, "gust",              GRB_WIND_GUST);
			wants (plot, "CAPE",              GRB_CAPE);
			wants (plot, "CIN",               GRB_CIN);
			wants (plot, "0 °C isotherm",     GRB_GEOPOT_HGT);

			// Values really readable in the middle of the box.
			std::set<time_t> dates = plot.getReader()->getListDates();
			if (!dates.empty()) {
				time_t t = *dates.begin();
				DataPointInfo pf (plot.getReader(), 0.5*(x0+x1), 0.5*(y0+y1), t);
				float sp=0, di=0;
				bool gotWind = pf.getWindValues (plot.getWindAltitude(), &sp, &di);
				printf ("       wind at centre: %.1f m/s from %.0f deg\n", sp, di);
				check ("wind readable at the centre of the box", gotWind);
				check ("wind speed is plausible", gotWind && sp >= 0 && sp < 200);
			}

			// The area really is the one asked for, not a sliver.
			GriddedRecord *rec = plot.getReader()->getFirstRecord ();
			if (rec != nullptr) {
				printf ("       grid %d x %d, %.2f..%.2f E, %.2f..%.2f N\n",
				        rec->getNi(), rec->getNj(),
				        rec->getXmin(), rec->getXmax(),
				        rec->getYmin(), rec->getYmax());
				check ("grid is not degenerate",
				       rec->getNi() > 4 && rec->getNj() > 4);
				check ("grid really covers the box asked for",
				       rec->getXmin() <= x0+0.01 && rec->getXmax() >= x1-0.01
				    && rec->getYmin() <= y0+0.01 && rec->getYmax() >= y1-0.01);
			}
		}
	}

	printf ("\n=== 3. GFS-Wave from NOMADS ===\n");
	NomadsLoader::Outcome wav = NomadsLoader::fetch (
	        "ww3_p50_", wx0, wy0, wx1, wy1, 1, 6, dir);
	printf ("       run %s, reaches +%d h%s\n",
	        qPrintable (wav.run), wav.hours, wav.truncated ? " (truncated)" : "");
	check ("file was written", !wav.path.isEmpty(), wav.error);

	if (!wav.path.isEmpty()) {
		printf ("       %s, %lld KB\n", qPrintable (QFileInfo(wav.path).fileName()),
		        (long long) QFileInfo(wav.path).size()/1024);
		check ("wave file is a different file from the atmospheric one",
		       wav.path != atm.path);

		GribPlot plot;
		int steps = 0;
		check ("XyGrib reads it back", readBack (wav.path, plot, &steps));
		if (plot.isReaderOk()) {
			printf ("       %d forecast steps in file\n", steps);
			check ("five forecast steps", steps == 5, QString::number(steps));
			wants (plot, "significant height", GRB_WAV_SIG_HT);
			wants (plot, "swell height",       GRB_WAV_SWL_HT);
			wants (plot, "swell direction",    GRB_WAV_SWL_DIR);
			wants (plot, "swell period",       GRB_WAV_SWL_PER);
			wants (plot, "wind wave height",   GRB_WAV_WND_HT);
			wants (plot, "wind wave direction",GRB_WAV_WND_DIR);
			wants (plot, "wind wave period",   GRB_WAV_WND_PER);
			wants (plot, "primary direction",  GRB_WAV_PRIM_DIR);
			wants (plot, "primary period",     GRB_WAV_PRIM_PER);
		}
	}

	printf ("\n=== 4. A model NOAA does not publish ===\n");
	NomadsLoader::Outcome none = NomadsLoader::fetch (
	        "icon_eu_p06_", x0, y0, x1, y1, 1, 6, dir);
	check ("refused without touching the network", none.path.isEmpty());
	check ("and says why", !none.error.isEmpty(), none.error);

	printf ("\n=== 4b. An area too large to pull directly ===\n");
	// NOAA serves whatever subset it is asked for. The whole world at
	// 0.25 deg is 25 MB per forecast hour, so ten days of it is two
	// gigabytes: that has to be refused before the first request, not
	// discovered somewhere around +78 h.
	{
		QElapsedTimer t; t.start();
		NomadsLoader::Outcome big = NomadsLoader::fetch (
		        "gfs_p25_", -180.0, -90.0, 180.0, 90.0, 10, 3, dir);
		printf ("       answered in %lld ms: %s\n",
		        (long long) t.elapsed(), qPrintable (big.error));
		check ("the whole world is refused", big.path.isEmpty());
		check ("refused before asking NOAA anything", t.elapsed() < 1000,
		       QString("%1 ms").arg(t.elapsed()));
		check ("and names the size", big.error.contains("MB"), big.error);

		// The box the rest of this test uses must stay well inside it.
		NomadsLoader::Outcome ok = NomadsLoader::fetch (
		        "gfs_p25_", x0, y0, x1, y1, 1, 6, dir);
		check ("a normal area still goes through", !ok.path.isEmpty(), ok.error);
	}

	printf ("\n=== 4c. A sea NOAA's wave model does not cover ===\n");
	// The Caspian and the Black Sea lie outside the global wave grid.
	// NOAA answers for them with a correctly formed field in which every
	// point is missing, so the whole run would download and draw nothing.
	{
		QElapsedTimer t; t.start();
		NomadsLoader::Outcome dead = NomadsLoader::fetch (
		        "ww3_p50_", 48.0, 38.0, 54.0, 46.0, 8, 3, dir);
		printf ("       answered in %lld ms: %s\n",
		        (long long) t.elapsed(), qPrintable (dead.error));
		check ("an empty sea is refused", !dead.ok);
		check ("and says why", dead.error.contains("wave model"), dead.error);
		// 8 days at 3 h would be 65 requests; this must stop on the first.
		check ("it stops on the first forecast hour", t.elapsed() < 30000,
		       QString("%1 ms").arg(t.elapsed()));

		// The same request on the open sea still works.
		NomadsLoader::Outcome atl = NomadsLoader::fetch (
		        "ww3_p50_", -16.0, 42.0, -10.0, 48.0, 1, 12, dir);
		check ("the open Atlantic still comes through", atl.ok, atl.error);
	}

	printf ("\n=== 5. The fallback as the download-all action uses it ===\n");
	// Straight through MultiModelLoader, so this covers the hand-over from
	// a refusal by the OpenGribs server to NOAA. One day only, to keep the
	// number of requests down.
	{
		MultiModelLoader loader;
		QList<MultiModelLoader::Result> res =
		        loader.run (wx0, wy0, wx1, wy1, dir, nullptr, 1);
		check ("every model was tried", res.size() == 9,
		       QString::number (res.size()));

		int gotGfs = -1, gotWw3 = -1;
		for (int i=0; i<res.size(); i++) {
			const MultiModelLoader::Result &r = res.at(i);
			printf ("       %-18s %s  %s\n", qPrintable (r.label),
			        r.ok ? "ok  " : "none",
			        qPrintable (r.ok ? QString("+%1 h").arg(r.hours) : r.note));
			if (r.label.startsWith ("GFS"))  gotGfs = i;
			if (r.label.startsWith ("WW3"))  gotWw3 = i;
		}
		check ("GFS came back", gotGfs >= 0 && res.at(gotGfs).ok,
		       gotGfs >= 0 ? res.at(gotGfs).note : "not in the list");
		check ("WW3 came back", gotWw3 >= 0 && res.at(gotWw3).ok,
		       gotWw3 >= 0 ? res.at(gotWw3).note : "not in the list");
		if (gotGfs >= 0 && res.at(gotGfs).ok) {
			check ("and says where it came from",
			       res.at(gotGfs).note.contains ("NOMADS"),
			       res.at(gotGfs).note);
			check ("with a depth of one day",
			       res.at(gotGfs).hours == 24,
			       QString::number (res.at(gotGfs).hours));
		}
	}

	printf ("\n=== 5b. An unreachable server must not hang the window ===\n");
	// Направляем загрузчик в чёрную дыру: адрес не маршрутизируется, ответа
	// не будет никогда. Раньше у запроса не было таймаута вовсе, и диалог
	// ждал вечно — ровно то, что пользователь видит как «не отвечает».
	{
		qputenv ("MAKGRIB_SERVER", "10.255.255.1");
		QNetworkAccessManager mgr;
		FileLoaderGRIB loader (&mgr, nullptr);
		QEventLoop loop;
		QString err, lastMsg;
		bool gotData = false;
		QObject::connect (&loader, &FileLoaderGRIB::signalGribDataReceived,
		        [&](QByteArray *, QString) { gotData = true; loop.quit(); });
		QObject::connect (&loader, &FileLoaderGRIB::signalGribLoadError,
		        [&](QString e) { err = e; loop.quit(); });
		QObject::connect (&loader, &FileLoaderGRIB::signalGribSendMessage,
		        [&](QString m) { lastMsg = m; });
		QTimer guard; guard.setSingleShot (true);
		QObject::connect (&guard, &QTimer::timeout,
		        [&]() { err = "НИЧЕГО НЕ ОТВЕТИЛО"; loop.quit(); });
		guard.start (200000);

		QElapsedTimer t; t.start();
		loader.getGribFile ("GFS", wx0, wx1, wy0, wy1, 0.25, 6, 1, "last",
		        true, true, true, true, true, true, true,
		        false, false, false, true, true, false,
		        false, false, false, false, false, false, false, false,
		        false, true, "WW3", true, true, true);
		loop.exec ();
		guard.stop ();
		qunsetenv ("MAKGRIB_SERVER");

		printf ("       ответ за %.0f с: %s\n", t.elapsed()/1000.0,
		        qPrintable (gotData ? QString("данные с NOAA") : err));
		check ("не висит бесконечно", t.elapsed() < 190000,
		       QString("%1 с").arg(t.elapsed()/1000));
		// 45 с на попытку достучаться до сервера, дальше — сразу NOAA.
		check ("укладывается в минуту с небольшим", t.elapsed() < 130000,
		       QString("%1 с").arg(t.elapsed()/1000));
		check ("недоступный сервер уводит на NOAA, а не в ошибку", gotData, err);
	}

	printf ("\n=== 6. The ordinary download dialog falls back too ===\n");
	// Straight through FileLoaderGRIB, the loader the Download-GRIB dialog
	// drives. The OpenGribs server is asked first and refuses; what comes
	// back has to be a GRIB built from NOAA, atmosphere and waves in the
	// one file the dialog expects.
	{
		QNetworkAccessManager mgr;
		FileLoaderGRIB loader (&mgr, nullptr);

		QEventLoop loop;
		QByteArray got;
		QString gotName, err, lastMsg;
		QObject::connect (&loader, &FileLoaderGRIB::signalGribDataReceived,
		        [&](QByteArray *c, QString n) {
			if (c != nullptr) got = *c;
			gotName = n;
			loop.quit ();
		});
		QObject::connect (&loader, &FileLoaderGRIB::signalGribLoadError,
		        [&](QString e) { err = e; loop.quit(); });
		QObject::connect (&loader, &FileLoaderGRIB::signalGribSendMessage,
		        [&](QString m) { lastMsg = m; });

		QTimer guard;
		guard.setSingleShot (true);
		QObject::connect (&guard, &QTimer::timeout, [&]() {
			err = "the loader never answered";
			loop.quit ();
		});
		guard.start (600000);

		// GFS + WW3, one day at 6 h, over the same small box.
		// The area goes in as west, east, south, north.
		loader.getGribFile ("GFS", wx0, wx1, wy0, wy1,
		        0.25, 6, 1, "last",
		        true, true, true, true, true, true, true,   // wind..isotherm0
		        false, false, false,                        // snow, frz rain
		        true, true, false,                          // CAPE, CIN, refl
		        false, false, false, false, false, false, false, false,
		        false,                                      // skewT
		        true,                                       // gusts
		        "WW3", true, true, true);
		loop.exec ();
		guard.stop ();

		printf ("       last message: %s\n", qPrintable (lastMsg));
		check ("the dialog got data, not an error", !got.isEmpty(), err);

		if (!got.isEmpty()) {
			printf ("       %s, %d KB\n", qPrintable (gotName), got.size()/1024);
			check ("it is a GRIB", got.startsWith ("GRIB"));
			check ("the name says where it came from",
			       gotName.contains ("NOMADS"), gotName);
			check ("the name says it carries waves too",
			       gotName.contains ("WAVE"), gotName);

			// Written out and read back with XyGrib's own reader, the way
			// the dialog does it after asking where to save.
			QString path = QDir(dir).absoluteFilePath ("dialog_fallback.grb2");
			QFile f (path);
			if (f.open (QIODevice::WriteOnly)) {
				f.write (got);
				f.close ();
			}
			GribPlot plot;
			int steps = 0;
			check ("XyGrib reads it back", readBack (path, plot, &steps));
			if (plot.isReaderOk()) {
				printf ("       %d forecast steps in file\n", steps);
				wants (plot, "wind",               GRB_WIND_VX);
				wants (plot, "pressure at sea level", GRB_PRESSURE_MSL);
				// Both halves really are in the one file.
				wants (plot, "significant height",  GRB_WAV_SIG_HT);
				wants (plot, "swell height",        GRB_WAV_SWL_HT);
			}
		}
	}

	printf ("\n");
	if (failures == 0)
		printf ("Все проверки пройдены.\n\n");
	else
		printf ("Провалено проверок: %d\n\n", failures);
	return failures == 0 ? 0 : 1;
}
