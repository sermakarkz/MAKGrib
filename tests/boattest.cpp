// Headless check of the virtual boat: geodesy, dead reckoning, the route
// dialog and the weather-along-route table, without touching the GUI.
#include <cstdio>
#include <QApplication>
#include <QStringList>

#include "VirtualBoat.h"
#include "DialogVirtualBoat.h"
#include "GribPlot.h"
#include "Settings.h"
#include "Util.h"
#include "Font.h"
#include "zuFile.h"
#include "Terrain.h"
#include "Projection.h"
#include "GshhsReader.h"
#include "MultiModelLoader.h"
#include "DataQString.h"
#include <QDir>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTableWidget>
#include <QFileInfo>
#include <memory>

static int failures = 0;

static void check (const char *what, bool ok, const QString &detail="")
{
	printf ("  [%s] %s%s\n", ok ? " OK " : "FAIL", what,
	        detail.isEmpty() ? "" : (" -> " + detail).toUtf8().constData());
	if (!ok) failures++;
}

int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	Settings::initializeSettingsDir ();
	Settings::initializeGribFilesDir ();
	Settings::findAppDataDir ();
	Font::loadAllFonts ();

	printf ("\n=== 1. Geodesy and dead reckoning ===\n");

	VirtualBoat boat;
	boat.setStart (47.9, 45.7, 1786860000);   // 2026-08-16 06:00 UTC
	boat.addWaypoint (48.5, 43.4);            // Makhachkala
	boat.addWaypoint (49.9, 40.5);            // Baku
	boat.addWaypoint (52.9, 40.1);            // Turkmenbashi
	boat.setSpeed (10.0);

	check ("route is sailable", boat.isDefined());
	check ("4 waypoints", boat.countWaypoints() == 4,
	       QString::number (boat.countWaypoints()));

	double leg0 = boat.getLegDistance (0);
	double total = boat.getTotalDistance ();
	printf ("       leg 1 = %.1f NM, total = %.1f NM, duration = %.2f h\n",
	        leg0, total, boat.getTotalDuration());
	// Astrakhan -> Makhachkala is about 140 NM on the chart.
	check ("leg 1 plausible (130..150 NM)", leg0 > 130 && leg0 < 150);
	check ("duration = distance / speed",
	       fabs (boat.getTotalDuration() - total/10.0) < 1e-9);

	// After 9 h at 10 kts the boat must have run exactly 90 NM.
	double lon, lat, run, course;
	bool ok = boat.positionAt (1786860000 + 9*3600, &lon, &lat, &run, &course);
	printf ("       after 9 h: %.4f %.4f, run %.2f NM, course %.0f deg\n",
	        lon, lat, run, course);
	check ("position found", ok);
	check ("run = 90.00 NM", fabs (run - 90.0) < 0.001,
	       QString::number (run, 'f', 3));
	check ("still on leg 1 (lat between)", lat < 45.7 && lat > 43.4);

	// 27 h -> 270 NM, past the first waypoint, on the second leg.
	ok = boat.positionAt (1786860000 + 27*3600, &lon, &lat, &run, &course);
	printf ("       after 27 h: %.4f %.4f, run %.2f NM\n", lon, lat, run);
	check ("run = 270 NM", fabs (run - 270.0) < 0.001);
	check ("passed waypoint 2 (lat < 43.4)", lat < 43.4);

	// Before departure there is no position.
	check ("no position before departure",
	       !boat.positionAt (1786860000 - 3600, &lon, &lat, &run, &course));

	// Far beyond the ETA the boat waits at the last waypoint.
	boat.positionAt (1786860000 + 1000*3600, &lon, &lat, &run, &course);
	check ("clamped at arrival", fabs (run - total) < 0.001
	       && fabs (lon - 52.9) < 0.01 && fabs (lat - 40.1) < 0.01);

	// A leg added by course and distance is the dead reckoning case.
	VirtualBoat dr;
	dr.setStart (50.0, 42.0, 1786860000);
	dr.addLegByCourse (90.0, 60.0);           // 60 NM due east
	check ("dead reckoning leg created", dr.countWaypoints() == 2);
	printf ("       60 NM on course 090 from 50.0/42.0 -> %.4f %.4f\n",
	        dr.getWaypoints()[1].lon, dr.getWaypoints()[1].lat);
	check ("east leg keeps latitude (~42)",
	       fabs (dr.getWaypoints()[1].lat - 42.0) < 0.2);
	check ("east leg measures 60 NM",
	       fabs (dr.getLegDistance(0) - 60.0) < 0.01,
	       QString::number (dr.getLegDistance(0), 'f', 3));

	printf ("\n=== 2. Settings round trip ===\n");
	boat.writeSettings ();
	VirtualBoat reloaded;
	reloaded.readSettings ();
	check ("waypoints survive save/load",
	       reloaded.countWaypoints() == boat.countWaypoints(),
	       QString::number (reloaded.countWaypoints()));
	check ("distance survives save/load",
	       fabs (reloaded.getTotalDistance() - total) < 0.01);
	check ("speed survives save/load", fabs (reloaded.getSpeed() - 10.0) < 1e-9);

	printf ("\n=== 3. Track over a real GRIB ===\n");
	// The reader needs the record count up front, exactly like Terrain does.
	int nbrecs = 0;
	{
		ZUFILE *f = zu_open (argv[1], "rb", ZU_COMPRESS_AUTO);
		if (f != nullptr) {
			GribReader r;
			nbrecs = r.countGribRecords (f);
			zu_close (f);
		}
	}
	printf ("       %d GRIB records counted\n", nbrecs);
	GribPlot plot;
	plot.loadFile (QString(argv[1]), nullptr, nbrecs);
	check ("GRIB loaded", plot.isReaderOk());

	if (plot.isReaderOk()) {
		std::set<time_t> dates = plot.getReader()->getListDates();
		printf ("       %d forecast steps in file\n", (int)dates.size());
		std::vector<VirtualBoat::TrackPoint> track = boat.makeTrack (dates);
		printf ("       %d track points\n", (int)track.size());
		check ("track is not empty", track.size() > 0);
		bool increasing = true;
		for (size_t i=1; i<track.size(); i++)
			if (track[i].distanceDone < track[i-1].distanceDone
			 || track[i].date <= track[i-1].date)
				increasing = false;
		check ("time and run increase monotonically", increasing);
		if (!track.empty())
			check ("first point is at departure or later",
			       track[0].date >= boat.getStartDate());

		// Weather really available at the boat position.
		if (!track.empty()) {
			DataPointInfo pf (plot.getReader(), track[0].lon, track[0].lat,
			                  track[0].date);
			float sp=0, di=0;
			bool w = pf.getWindValues (plot.getWindAltitude(), &sp, &di);
			printf ("       wind at first track point: %.1f m/s, %.0f deg\n", sp, di);
			check ("wind readable at the boat position", w);
		}

		printf ("\n=== 4. Widgets build without crashing ===\n");
		BoatTrackWindow *win = new BoatTrackWindow (&boat, &plot);
		win->refresh ();
		check ("track window built", win != nullptr);
		// The table keeps only the columns the file really carries.
		QTableWidget *tw = win->findChild<QTableWidget*>();
		if (tw != nullptr) {
			QStringList cols;
			for (int c=0; c<tw->columnCount(); c++)
				cols << tw->horizontalHeaderItem(c)->text();
			printf ("       %d columns kept: %s\n", tw->columnCount(),
			        cols.join(", ").toUtf8().constData());
			check ("route columns always present", tw->columnCount() >= 4);
			check ("weather columns detected", tw->columnCount() > 4);
		}

		DialogVirtualBoat *dlg = new DialogVirtualBoat (&boat, &plot);
		check ("route dialog built", dlg != nullptr);
		// Render both windows offscreen so the layout can actually be seen.
		win->resize (980, 520);
		win->grab().save (QString(argv[2]) + "/shot_track.png");
		dlg->resize (640, 580);
		dlg->grab().save (QString(argv[2]) + "/shot_dialog.png");
		printf ("       screenshots written to %s\n", argv[2]);
		delete dlg;
		delete win;
	}

	// Coverage probe: does the file really carry data over the Caspian,
	// or is it just spilling values onto everything inside the box?
	if (plot.isReaderOk()) {
		printf ("\n--- coverage probe (sea vs deep inland) ---\n");
		struct { const char *name; double lon, lat; } spots[] = {
			{"mid Caspian    ", 50.5, 41.5},
			{"north Caspian  ", 50.0, 45.0},
			{"inland steppe  ", 54.5, 46.5},
			{"inland Iran    ", 53.0, 35.0}
		};
		time_t d0 = plot.getReader()->getFirstDate();
		for (int k=0; k<4; k++) {
			DataPointInfo pf (plot.getReader(), spots[k].lon, spots[k].lat, d0);
			float ht=0, per=0, wd=0;
			bool w = pf.getWaveValues (GRB_PRV_WAV_SIG, &ht, &per, &wd)
			         && GribDataIsDef(ht);
			float sp=0, di=0;
			bool wind = pf.getWindValues (plot.getWindAltitude(), &sp, &di);
			printf ("       %s wave=%-12s wind=%s\n", spots[k].name,
			        w ? QString("%1 m / %2 s").arg(ht,0,'f',2).arg(per,0,'f',0)
			              .toUtf8().constData() : "none",
			        wind ? "yes" : "none");
		}
	}

	printf ("\n=== 5. Clicking the route on the map ===\n");
	{
		// A real Terrain widget, fed with synthetic mouse events: this is
		// the only way to exercise the click handling without a GUI session.
		Projection *proj = new Projection_EQU_CYL (800, 600, 50.0, 43.0, 30.0);
		std::shared_ptr<GshhsReader> gshhs =
		        std::make_shared<GshhsReader> (Util::pathGshhs(), 1);
		Terrain *terre = new Terrain (nullptr, proj, gshhs);
		terre->resize (800, 600);

		VirtualBoat *b = terre->getVirtualBoat ();
		b->clear ();

		check ("not drawing at start", !terre->isRouteDrawing());
		terre->startRouteDrawing ();
		check ("drawing mode entered", terre->isRouteDrawing());

		// Three left clicks -> three waypoints.
		const int px[3] = {200, 340, 480};
		const int py[3] = {150, 300, 420};
		for (int k=0; k<3; k++) {
			QMouseEvent press (QEvent::MouseButtonPress, QPointF(px[k],py[k]),
			                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent (terre, &press);
			QMouseEvent rel (QEvent::MouseButtonRelease, QPointF(px[k],py[k]),
			                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent (terre, &rel);
		}
		printf ("       after 3 left clicks: %d waypoints\n", b->countWaypoints());
		check ("3 clicks give 3 waypoints", b->countWaypoints() == 3);

		// The clicked pixel must map back to the clicked position.
		double wx, wy;
		proj->screen2map (px[0], py[0], &wx, &wy);
		check ("waypoint 1 sits under the click",
		       fabs (b->getWaypoints()[0].lon - wx) < 1e-6
		    && fabs (b->getWaypoints()[0].lat - wy) < 1e-6);

		// Backspace takes back the last point.
		QKeyEvent back (QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
		QApplication::sendEvent (terre, &back);
		check ("backspace removes the last waypoint", b->countWaypoints() == 2,
		       QString::number (b->countWaypoints()));

		// The right button ends the route and leaves the mode.
		QMouseEvent rpress (QEvent::MouseButtonPress, QPointF(500,500),
		                    Qt::RightButton, Qt::RightButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &rpress);
		QMouseEvent rrel (QEvent::MouseButtonRelease, QPointF(500,500),
		                  Qt::RightButton, Qt::RightButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &rrel);
		check ("right click ends the drawing", !terre->isRouteDrawing());
		check ("right click adds no waypoint", b->countWaypoints() == 2);
		check ("finished route is sailable", b->isDefined());
		printf ("       drawn route: %d waypoints, %.1f NM\n",
		        b->countWaypoints(), b->getTotalDistance());

		// Render the map with the route and the elastic segment following
		// the cursor, so the drawing can actually be looked at.
		terre->startRouteDrawing ();
		QMouseEvent moveTo (QEvent::MouseMove, QPointF(620,230),
		                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &moveTo);
		terre->grab().save (QString(argv[2]) + "/shot_map_route.png");
		printf ("       map with the route rendered\n");
		terre->stopRouteDrawing ();

		// Esc throws the whole route away.
		terre->startRouteDrawing ();
		QMouseEvent p2 (QEvent::MouseButtonPress, QPointF(300,300),
		                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &p2);
		QKeyEvent esc (QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent (terre, &esc);
		check ("escape cancels the route", b->countWaypoints() == 0
		       && !terre->isRouteDrawing());

		printf ("\n=== 6. Correcting the route ===\n");
		// Rebuild a small route to edit.
		b->clear ();
		terre->startRouteDrawing ();
		const int qx[3] = {200, 400, 600};
		const int qy[3] = {200, 300, 200};
		for (int k=0; k<3; k++) {
			QMouseEvent pr (QEvent::MouseButtonPress, QPointF(qx[k],qy[k]),
			                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent (terre, &pr);
		}
		terre->stopRouteDrawing ();
		check ("route of 3 waypoints ready", b->countWaypoints() == 3);

		// Hit testing: a waypoint is found under its own pixel, not elsewhere.
		check ("waypoint found under its pixel",
		       b->findWaypoint (proj, qx[1], qy[1]) == 1);
		check ("no waypoint far away",
		       b->findWaypoint (proj, 50, 550) == -1);

		// The middle of a leg is on the leg, not on a waypoint.
		int midx = (qx[0]+qx[1])/2, midy = (qy[0]+qy[1])/2;
		double ilon=0, ilat=0;
		check ("leg found at its middle",
		       b->findLeg (proj, midx, midy, 8, &ilon, &ilat) == 0);
		check ("no leg far away",
		       b->findLeg (proj, 50, 550, 8, &ilon, &ilat) == -1);

		// Dragging waypoint 1 with the left button must move it and must
		// not start a zone selection.
		double before = b->getTotalDistance ();
		QMouseEvent dpress (QEvent::MouseButtonPress, QPointF(qx[1],qy[1]),
		                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &dpress);
		check ("drag does not start a zone selection", !terre->isSelectingZone());
		QMouseEvent dmove (QEvent::MouseMove, QPointF(qx[1]+70,qy[1]+40),
		                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &dmove);
		QMouseEvent drel (QEvent::MouseButtonRelease, QPointF(qx[1]+70,qy[1]+40),
		                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &drel);
		double wx2, wy2;
		proj->screen2map (qx[1]+70, qy[1]+40, &wx2, &wy2);
		check ("waypoint followed the mouse",
		       fabs (b->getWaypoints()[1].lon - wx2) < 1e-6
		    && fabs (b->getWaypoints()[1].lat - wy2) < 1e-6);
		check ("still 3 waypoints after the drag", b->countWaypoints() == 3);
		check ("route length changed", fabs (b->getTotalDistance()-before) > 0.01);

		// Insert into a leg, then delete a waypoint.
		int leg = b->findLeg (proj, (qx[0]+qx[1])/2, (qy[0]+qy[1])/2 + 20,
		                      40, &ilon, &ilat);
		if (leg >= 0) {
			b->insertWaypoint (leg+1, ilon, ilat);
			check ("insert adds one waypoint in place",
			       b->countWaypoints() == 4);
			b->removeWaypoint (leg+1);
		}
		b->removeWaypoint (0);
		check ("delete removes one waypoint", b->countWaypoints() == 2);

		b->clear ();

		// Outside the mode the map must behave exactly as before.
		QMouseEvent p3 (QEvent::MouseButtonPress, QPointF(300,300),
		                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent (terre, &p3);
		check ("normal click adds nothing once finished", b->countWaypoints() == 0);
		check ("normal click still starts a zone selection",
		       terre->isSelectingZone());

		delete terre;
	}

	printf ("\n=== 7. Several forecasts loaded at once ===\n");
	{
		Projection *proj = new Projection_EQU_CYL (800, 600, 51.0, 42.0, 30.0);
		std::shared_ptr<GshhsReader> gshhs =
		        std::make_shared<GshhsReader> (Util::pathGshhs(), 1);
		Terrain *terre = new Terrain (nullptr, proj, gshhs);
		terre->resize (800, 600);

		// argv[1] plus any extra files given on the command line.
		QStringList files;
		files << QString(argv[1]);
		for (int a=3; a<argc; a++)
			files << QString(argv[a]);

		int loaded = 0;
		for (int k=0; k<files.size(); k++) {
			// keepPrevious=true from the second file on.
			FileDataType t = terre->loadMeteoDataFile (files.at(k), false, k>0);
			if (t == DATATYPE_GRIB) loaded++;
			printf ("       %-28s -> %s\n",
			        QFileInfo(files.at(k)).fileName().toUtf8().constData(),
			        t == DATATYPE_GRIB ? "loaded" : "rejected");
		}
		check ("every file became a slot", terre->countModels() == loaded,
		       QString("%1 slots / %2 loaded").arg(terre->countModels()).arg(loaded));

		int withWaves = 0;
		for (int i=0; i<terre->countModels(); i++) {
			bool w = terre->modelHasWaves (i);
			if (w) withWaves++;
			printf ("       slot %d: %-16s %s\n", i+1,
			        terre->getModelName(i).toUtf8().constData(),
			        w ? "(sea state)" : "(atmosphere only)");
		}
		check ("slots are named after the model, not the file",
		       !terre->getModelName(0).isEmpty()
		       && !terre->getModelName(0).contains(".grb"));
		// An atmospheric file must not claim to carry waves: that is what
		// the hint in the status bar is built on.
		check ("an atmospheric forecast reports no sea state",
		       !terre->modelHasWaves (0));
		printf ("       %d of %d slots carry sea state\n",
		        withWaves, terre->countModels());

		if (terre->countModels() >= 2) {
			// Switching must keep the displayed instant.
			terre->setActiveModel (0);
			time_t t0 = terre->getGriddedPlotter()->getCurrentDate();
			// move a few steps forward in time
			std::set<time_t> d = terre->getGriddedPlotter()->getReader()->getListDates();
			std::set<time_t>::iterator it = d.begin();
			for (int k=0; k<4 && it!=d.end(); k++) ++it;
			time_t want = (it!=d.end()) ? *it : t0;
			terre->getGriddedPlotter()->setCurrentDate (want);

			check ("switch to slot 2 accepted", terre->setActiveModel(1));
			check ("active index followed", terre->getActiveModel() == 1);
			time_t got = terre->getGriddedPlotter()->getCurrentDate();
			double drift = fabs (difftime (got, want)) / 3600.0;
			printf ("       asked %ld, got %ld (drift %.1f h)\n",
			        (long)want, (long)got, drift);
			check ("valid time carried over (< 3 h drift)", drift < 3.0);

			// Data really comes from the other model now.
			check ("reader really changed",
			       terre->getGriddedPlotter() != nullptr
			       && terre->getGriddedPlotter()->isReaderOk());

			check ("switching back works", terre->setActiveModel(0));
			check ("re-selecting the same slot is a no-op",
			       !terre->setActiveModel(0));

			// A model without the chosen field must not wipe the user's
			// colour map preference out of the settings.
			Util::setSetting ("colorMapData",
			        DataCodeStr::serialize (DataCode(GRB_PRV_WIND_XY2D,
			                                         LV_ABOV_GND, 10)));
			QString mapBefore = Util::getSetting("colorMapData","").toString();
			// A substitute applied because the model lacks the field must
			// not be written down as the user's choice...
			terre->setColorMapData (DataCode(GRB_WAV_SIG_HT,LV_GND_SURF,0), false);
			QString mapAfter = Util::getSetting("colorMapData","").toString();
			printf ("       colour map stored before='%s' after='%s'\n",
			        mapBefore.toUtf8().constData(), mapAfter.toUtf8().constData());
			check ("substitute colour map does not overwrite the preference",
			       mapBefore == mapAfter);
			// ...while a real choice still is.
			terre->setColorMapData (DataCode(GRB_WAV_SIG_HT,LV_GND_SURF,0), true);
			check ("a real choice is still stored",
			       Util::getSetting("colorMapData","").toString() != mapBefore);
			Util::setSetting ("colorMapData", mapBefore);

			// Closing one slot keeps the rest.
			int before = terre->countModels();
			terre->removeModel (0);
			check ("removing a slot keeps the others",
			       terre->countModels() == before-1
			       && terre->getGriddedPlotter() != nullptr);
		}
		delete terre;
	}


	// Real network test, only when asked: XYGRIB_TEST_NET=1
	if (qgetenv("XYGRIB_TEST_NET") == "1")
	{
		printf ("\n=== 8. Downloading every forecast (real server) ===\n");
		QString dir = QString(argv[2]) + "/models";
		QDir().mkpath (dir);

		MultiModelLoader loader;
		QList<MultiModelLoader::Result> res =
		        loader.run (46, 36, 56, 48, dir, nullptr);

		int okCount = 0;
		for (const MultiModelLoader::Result &r : res) {
			printf ("       %-18s %-4s %2d d / %d h  %s\n",
			        r.label.toUtf8().constData(),
			        r.ok ? "OK" : "--", r.days, r.interval,
			        r.note.isEmpty() ? "" : ("(" + r.note + ")").toUtf8().constData());
			if (r.ok) okCount++;
		}
		check ("at least four models downloaded", okCount >= 4,
		       QString::number(okCount));

		// Every downloaded file must really be loadable and become a slot.
		Projection *pr = new Projection_EQU_CYL (800, 600, 51.0, 42.0, 30.0);
		std::shared_ptr<GshhsReader> gs =
		        std::make_shared<GshhsReader> (Util::pathGshhs(), 1);
		Terrain *tr2 = new Terrain (nullptr, pr, gs);
		int nLoaded = 0;
		for (const MultiModelLoader::Result &r : res) {
			if (!r.ok) continue;
			if (tr2->loadMeteoDataFile (r.fileName, false, nLoaded>0) == DATATYPE_GRIB)
				nLoaded++;
		}
		printf ("       %d slots after loading everything\n", tr2->countModels());
		for (int i=0; i<tr2->countModels(); i++)
			printf ("         %d. %s\n", i+1,
			        tr2->getModelName(i).toUtf8().constData());
		check ("all downloaded files load as slots",
		       tr2->countModels() == okCount);
		check ("nest widening worked for at least one model",
		       [&]{ for (const MultiModelLoader::Result &r : res)
		                if (r.ok && r.widened) return true;
		            return false; }());
		delete tr2;
	}

	printf ("\n%s  (%d failure(s))\n\n",
	        failures==0 ? "ALL CHECKS PASSED" : "THERE ARE FAILURES", failures);
	return failures==0 ? 0 : 1;
}
