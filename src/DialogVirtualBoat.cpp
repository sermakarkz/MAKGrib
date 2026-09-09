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

#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QVector>

#include "DialogVirtualBoat.h"
#include "DataPointInfo.h"
#include "Orthodromie.h"
#include "Util.h"

//---------------------------------------------------------------------
// Seconds to add to a UTC instant to get the wall clock the rest of the
// program displays (Options -> Units -> time zone). Lets the departure
// editor speak the same time as every other date in the window.
static qint64 zoneOffset (time_t t)
{
	QDateTime dt = Util::applyTimeZone (t);
	dt.setTimeSpec (Qt::UTC);          // keep the digits, forget the zone
	return dt.toSecsSinceEpoch() - static_cast<qint64>(t);
}
//---------------------------------------------------------------------
static QString formatDuration (double hours)
{
	if (hours < 0)
		hours = 0;
	int totalMinutes = static_cast<int>(hours*60.0 + 0.5);
	int d = totalMinutes / (24*60);
	int h = (totalMinutes / 60) % 24;
	int m = totalMinutes % 60;
	if (d > 0)
		return QString("%1 d %2 h %3 min").arg(d).arg(h).arg(m);
	return QString("%1 h %2 min").arg(h).arg(m);
}

//=====================================================================
//  Route editor
//=====================================================================
DialogVirtualBoat::DialogVirtualBoat (VirtualBoat *boat_,
                                      GriddedPlotter *plotter_,
                                      QWidget *parent)
	: DialogBoxBase (parent)
{
	boat     = boat_;
	plotter  = plotter_;
	updating = false;

	setWindowTitle (tr("Virtual boat"));

	//-- departure and speed ------------------------------------------
	QGroupBox *grpNav = new QGroupBox (tr("Navigation"));
	QGridLayout *layNav = new QGridLayout ();

	edStartDate = new QDateTimeEdit ();
	edStartDate->setDisplayFormat ("dd.MM.yyyy  HH:mm");
	edStartDate->setTimeSpec (Qt::UTC);
	edStartDate->setCalendarPopup (true);
	time_t start = boat->getStartDate ();
	if (start == 0 && plotter != nullptr && plotter->isReaderOk())
		start = plotter->getReader()->getFirstDate ();
	if (start == 0)
		start = time (nullptr);
	QString tzSuffix;
	Util::applyTimeZone (start, &tzSuffix);
	edStartDate->setDateTime (QDateTime::fromSecsSinceEpoch (
	        static_cast<qint64>(start) + zoneOffset(start), Qt::UTC));

	edSpeed = new QDoubleSpinBox ();
	edSpeed->setRange (0.1, 60.0);
	edSpeed->setDecimals (1);
	edSpeed->setSingleStep (0.5);
	edSpeed->setSuffix (" " + tr("kts"));
	edSpeed->setValue (boat->getSpeed());

	edStartDate->setToolTip (tr("When the boat leaves the first waypoint. "
	        "Everything downstream — arrival times, the weather met along "
	        "the way — is counted from this moment."));
	edSpeed->setToolTip (tr("Speed made good over the ground, held for the "
	        "whole route. It is what turns the distance between waypoints "
	        "into a time of arrival."));

	layNav->addWidget (new QLabel (tr("Departure")+" ("+tzSuffix+") :"), 0,0);
	layNav->addWidget (edStartDate, 0,1);
	layNav->addWidget (new QLabel (tr("Boat speed")+" :"), 1,0);
	layNav->addWidget (edSpeed, 1,1);
	layNav->setColumnStretch (2, 1);
	grpNav->setLayout (layNav);

	//-- waypoints ----------------------------------------------------
	QGroupBox *grpWp = new QGroupBox (tr("Route"));
	QVBoxLayout *layWp = new QVBoxLayout ();

	tableWaypoints = new QTableWidget (0, 6);
	QStringList headers;
	headers << tr("N") << tr("Longitude") << tr("Latitude")
	        << tr("Course") << tr("Distance") << tr("Time at waypoint");
	tableWaypoints->setHorizontalHeaderLabels (headers);
	tableWaypoints->verticalHeader()->setVisible (false);
	tableWaypoints->setSelectionBehavior (QAbstractItemView::SelectRows);
	tableWaypoints->setMinimumHeight (180);
	tableWaypoints->horizontalHeader()->setStretchLastSection (true);
	tableWaypoints->setToolTip (tr("The waypoints in order. Longitude and "
	        "latitude can be edited straight in the table; course, distance "
	        "and time follow from them."));

	QHBoxLayout *layBtWp = new QHBoxLayout ();
	btAdd    = new QPushButton (tr("Add waypoint"));
	btRemove = new QPushButton (tr("Remove"));
	btClear  = new QPushButton (tr("Clear route"));
	btAdd->setToolTip (tr("Append a waypoint after the last one"));
	btRemove->setToolTip (tr("Remove the waypoint selected in the table"));
	btClear->setToolTip (tr("Delete the whole route"));
	layBtWp->addWidget (btAdd);
	layBtWp->addWidget (btRemove);
	layBtWp->addWidget (btClear);
	layBtWp->addStretch (1);

	layWp->addWidget (tableWaypoints);
	layWp->addLayout (layBtWp);
	grpWp->setLayout (layWp);

	//-- dead reckoning leg -------------------------------------------
	QGroupBox *grpDR = new QGroupBox (tr("Add a leg by course and distance"));
	QHBoxLayout *layDR = new QHBoxLayout ();

	edCourse = new QDoubleSpinBox ();
	edCourse->setRange (0, 359.9);
	edCourse->setDecimals (1);
	edCourse->setSuffix (" °");
	edCourse->setWrapping (true);

	edLegDistance = new QDoubleSpinBox ();
	edLegDistance->setRange (0.1, 20000.0);
	edLegDistance->setDecimals (1);
	edLegDistance->setValue (60.0);
	edLegDistance->setSuffix (" " + tr("NM"));

	btAddLeg = new QPushButton (tr("Add leg"));
	edCourse->setToolTip (tr("True course of the new leg, in degrees"));
	edLegDistance->setToolTip (tr("Length of the new leg, in nautical miles"));
	btAddLeg->setToolTip (tr("Add a waypoint this course and distance from "
	        "the last one — for laying out a route by dead reckoning "
	        "rather than by clicking the map."));

	layDR->addWidget (new QLabel (tr("Course")+" :"));
	layDR->addWidget (edCourse);
	layDR->addSpacing (12);
	layDR->addWidget (new QLabel (tr("Distance")+" :"));
	layDR->addWidget (edLegDistance);
	layDR->addSpacing (12);
	layDR->addWidget (btAddLeg);
	layDR->addStretch (1);
	grpDR->setLayout (layDR);

	//-- summary and buttons ------------------------------------------
	lbSummary = new QLabel ();
	lbSummary->setTextFormat (Qt::RichText);

	btOK     = new QPushButton (tr("OK"));
	btCancel = new QPushButton (tr("Cancel"));
	btOK->setDefault (true);
	btOK->setToolTip (tr("Keep the route and close"));
	btCancel->setToolTip (tr("Discard the changes made here"));

	QHBoxLayout *layBt = new QHBoxLayout ();
	layBt->addStretch (1);
	layBt->addWidget (btCancel);
	layBt->addWidget (btOK);

	QVBoxLayout *layMain = new QVBoxLayout ();
	layMain->addWidget (grpNav);
	layMain->addWidget (grpWp);
	layMain->addWidget (grpDR);
	layMain->addWidget (lbSummary);
	layMain->addLayout (layBt);
	setLayout (layMain);

	connect (btOK,     SIGNAL(clicked()), this, SLOT(slotBtOK()));
	connect (btCancel, SIGNAL(clicked()), this, SLOT(slotBtCancel()));
	connect (btAdd,    SIGNAL(clicked()), this, SLOT(slotBtAddWaypoint()));
	connect (btRemove, SIGNAL(clicked()), this, SLOT(slotBtRemoveWaypoint()));
	connect (btAddLeg, SIGNAL(clicked()), this, SLOT(slotBtAddLeg()));
	connect (btClear,  SIGNAL(clicked()), this, SLOT(slotBtClear()));
	connect (edSpeed,  SIGNAL(valueChanged(double)), this, SLOT(slotValueChanged()));
	connect (edStartDate, SIGNAL(dateTimeChanged(QDateTime)),
	         this, SLOT(slotValueChanged()));
	connect (tableWaypoints, SIGNAL(cellChanged(int,int)),
	         this, SLOT(slotCellChanged(int,int)));

	fillTable ();
	updateSummary ();
	resize (620, 560);
}

//---------------------------------------------------------------------
void DialogVirtualBoat::fillTable ()
{
	updating = true;
	tableWaypoints->setRowCount (boat->countWaypoints());

	for (int i=0; i<boat->countWaypoints(); i++)
	{
		const VirtualBoat::Waypoint &w = boat->getWaypoints()[i];

		QTableWidgetItem *itNum = new QTableWidgetItem (QString::number(i+1));
		itNum->setFlags (Qt::ItemIsEnabled);
		tableWaypoints->setItem (i, 0, itNum);

		tableWaypoints->setItem (i, 1,
		        new QTableWidgetItem (QString::number(w.lon, 'f', 4)));
		tableWaypoints->setItem (i, 2,
		        new QTableWidgetItem (QString::number(w.lat, 'f', 4)));

		// Time at this waypoint: everything sailed before reaching it.
		double runBefore = 0;
		for (int k=0; k<i; k++)
			runBefore += boat->getLegDistance (k);
		QString eta = Util::formatDateTimeShort (
		        boat->getStartDate()
		        + static_cast<time_t>(runBefore / boat->getSpeed() * 3600.0 + 0.5));

		// Course and distance describe the leg leaving this waypoint.
		QString course, dist;
		if (i+1 < boat->countWaypoints()) {
			Orthodromie ortho (w.lon, w.lat,
			                   boat->getWaypoints()[i+1].lon,
			                   boat->getWaypoints()[i+1].lat);
			course = QString::number (ortho.getAzimutDeg(), 'f', 0) + "°";
			dist   = Util::formatDistance (boat->getLegDistance (i));
		}

		for (int c=3; c<=5; c++) {
			QString txt = (c==3) ? course : (c==4) ? dist : eta;
			QTableWidgetItem *it = new QTableWidgetItem (txt);
			it->setFlags (Qt::ItemIsEnabled);
			tableWaypoints->setItem (i, c, it);
		}
	}
	tableWaypoints->resizeColumnsToContents ();
	updating = false;
}
//---------------------------------------------------------------------
void DialogVirtualBoat::updateSummary ()
{
	QString txt;
	if (!boat->isDefined()) {
		txt = "<i>" + tr("Add at least two waypoints to sail the route.") + "</i>";
	}
	else {
		txt = "<b>" + tr("Total") + " :</b> "
		    + Util::formatDistance (boat->getTotalDistance())
		    + " &nbsp;•&nbsp; " + formatDuration (boat->getTotalDuration())
		    + " &nbsp;•&nbsp; " + tr("ETA") + " "
		    + Util::formatDateTimeShort (boat->getArrivalDate());

		if (plotter != nullptr && plotter->isReaderOk()) {
			const std::set<time_t> dates = plotter->getReader()->getListDates ();
			time_t lastGrib = dates.empty() ? 0 : *dates.rbegin();
			if (lastGrib != 0 && boat->getArrivalDate() > lastGrib)
				txt += "<br><span style='color:#a04000'>"
				     + tr("The forecast ends before arrival: "
				          "weather is only available up to")
				     + " " + Util::formatDateTimeShort (lastGrib) + "</span>";
		}
	}
	lbSummary->setText (txt);
}
//---------------------------------------------------------------------
void DialogVirtualBoat::applyToBoat ()
{
	boat->setSpeed (edSpeed->value());
	// The editor shows local wall clock; store the real UTC instant.
	qint64 shown = edStartDate->dateTime().toSecsSinceEpoch();
	boat->setStartDate (
	        static_cast<time_t>(shown - zoneOffset (static_cast<time_t>(shown))));
}

//---------------------------------------------------------------------
void DialogVirtualBoat::slotValueChanged ()
{
	if (updating)
		return;
	applyToBoat ();
	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotCellChanged (int row, int col)
{
	if (updating || (col != 1 && col != 2))
		return;
	QTableWidgetItem *it = tableWaypoints->item (row, col);
	if (it == nullptr)
		return;
	bool ok = false;
	double v = it->text().toDouble (&ok);
	if (!ok) {
		fillTable ();
		return;
	}
	const VirtualBoat::Waypoint &w = boat->getWaypoints()[row];
	if (col == 1)
		boat->setWaypoint (row, v, w.lat);
	else
		boat->setWaypoint (row, w.lon, v);

	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtAddWaypoint ()
{
	applyToBoat ();
	if (boat->countWaypoints() == 0) {
		// No route yet: start from the centre of the visible map area.
		boat->addWaypoint (0, 0);
	}
	else {
		// A new waypoint 60 NM due north keeps it visible and editable.
		boat->addLegByCourse (0, 60);
	}
	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtRemoveWaypoint ()
{
	int row = tableWaypoints->currentRow ();
	if (row < 0)
		row = boat->countWaypoints() - 1;
	boat->removeWaypoint (row);
	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtAddLeg ()
{
	applyToBoat ();
	if (boat->countWaypoints() == 0)
		return;
	boat->addLegByCourse (edCourse->value(), edLegDistance->value());
	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtClear ()
{
	boat->clear ();
	fillTable ();
	updateSummary ();
	emit signalBoatChanged ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtOK ()
{
	applyToBoat ();
	boat->writeSettings ();
	emit signalBoatChanged ();
	accept ();
}
//---------------------------------------------------------------------
void DialogVirtualBoat::slotBtCancel ()
{
	reject ();
}

//=====================================================================
//  Weather along the route
//=====================================================================
BoatTrackWindow::BoatTrackWindow (VirtualBoat *boat_,
                                  GriddedPlotter *plotter_,
                                  QWidget *parent)
	: QWidget (parent, Qt::Window)
{
	boat    = boat_;
	plotter = plotter_;

	setWindowTitle (tr("Weather along the route"));
	setAttribute (Qt::WA_DeleteOnClose, false);

	lbHeader = new QLabel ();
	lbHeader->setTextFormat (Qt::RichText);

	table = new QTableWidget (0, 0);
	table->verticalHeader()->setVisible (false);
	table->setEditTriggers (QAbstractItemView::NoEditTriggers);
	table->setSelectionBehavior (QAbstractItemView::SelectRows);
	table->setAlternatingRowColors (true);

	QVBoxLayout *lay = new QVBoxLayout ();
	lay->addWidget (lbHeader);
	lay->addWidget (table);
	setLayout (lay);

	refresh ();
	resize (940, 560);
}
//---------------------------------------------------------------------
void BoatTrackWindow::refresh ()
{
	table->setRowCount (0);

	if (boat == nullptr || !boat->isDefined()) {
		lbHeader->setText ("<i>" + tr("No route defined.") + "</i>");
		return;
	}
	if (plotter == nullptr || !plotter->isReaderOk()) {
		lbHeader->setText ("<i>" + tr("No GRIB file loaded.") + "</i>");
		return;
	}

	lbHeader->setText (
	        "<b>" + tr("Route") + " :</b> "
	        + Util::formatDistance (boat->getTotalDistance())
	        + " &nbsp;•&nbsp; " + tr("speed") + " "
	        + QString::number (boat->getSpeed(), 'f', 1) + " " + tr("kts")
	        + " &nbsp;•&nbsp; " + formatDuration (boat->getTotalDuration())
	        + " &nbsp;•&nbsp; " + tr("ETA") + " "
	        + Util::formatDateTimeShort (boat->getArrivalDate()));

	const std::set<time_t> dates = plotter->getReader()->getListDates ();
	const std::vector<VirtualBoat::TrackPoint> track = boat->makeTrack (dates);
	const Altitude windAlt = plotter->getWindAltitude ();

	// Every quantity the boat could possibly meet, in the order a
	// navigator reads them. Columns that stay empty for the whole route
	// are dropped afterwards, so the table shows exactly what the loaded
	// file really carries and nothing else.
	static const char *titles[] = {
		QT_TR_NOOP("Time"), QT_TR_NOOP("Position"), QT_TR_NOOP("Run"),
		QT_TR_NOOP("Course"),
		QT_TR_NOOP("Wind"), QT_TR_NOOP("Gust"),
		QT_TR_NOOP("Waves"), QT_TR_NOOP("Max wave"), QT_TR_NOOP("Swell"),
		QT_TR_NOOP("Wind wave"), QT_TR_NOOP("Whitecaps"),
		QT_TR_NOOP("Current"), QT_TR_NOOP("Water temp."),
		QT_TR_NOOP("Pressure"), QT_TR_NOOP("Temperature"),
		QT_TR_NOOP("Dew point"), QT_TR_NOOP("Humidity"),
		QT_TR_NOOP("Precipitation"), QT_TR_NOOP("Cloud cover"),
		QT_TR_NOOP("Isotherm 0°C"), QT_TR_NOOP("Snow depth"),
		QT_TR_NOOP("Snow"), QT_TR_NOOP("Freezing rain"),
		QT_TR_NOOP("CAPE"), QT_TR_NOOP("CIN"), QT_TR_NOOP("Reflectivity")
	};
	const int NCOL = static_cast<int>(sizeof(titles)/sizeof(titles[0]));

	QVector< QVector<QString> > rows;
	rows.reserve (static_cast<int>(track.size()));

	for (size_t i=0; i<track.size(); i++)
	{
		const VirtualBoat::TrackPoint &tp = track[i];
		DataPointInfo pf (plotter->getReader(), tp.lon, tp.lat, tp.date);

		QVector<QString> r (NCOL);
		int c = 0;

		r[c++] = Util::formatDateTimeShort (tp.date)
		       + (tp.arrived ? "  " + tr("(arrived)") : QString());
		r[c++] = Util::formatPosition (static_cast<float>(tp.lon),
		                               static_cast<float>(tp.lat));
		r[c++] = Util::formatDistance (static_cast<float>(tp.distanceDone));
		r[c++] = QString::number (tp.course, 'f', 0) + "°";

		float speed=0, dir=0;
		if (pf.getWindValues (windAlt, &speed, &dir))
			r[c] = Util::formatDirection (dir) + "  "
			     + Util::formatSpeed_Wind (speed);
		c++;
		if (pf.hasGUSTsfc())   r[c] = Util::formatSpeed_Wind (pf.GUSTsfc);
		c++;

		//-- sea ------------------------------------------------------
		const int waveKinds[4] = {GRB_PRV_WAV_SIG, GRB_PRV_WAV_MAX,
		                          GRB_PRV_WAV_SWL, GRB_PRV_WAV_WND};
		for (int k=0; k<4; k++) {
			float ht=0, per=0, wdir=0;
			if (pf.getWaveValues (waveKinds[k], &ht, &per, &wdir)
			        && GribDataIsDef(ht)) {
				QString s = Util::formatWaveHeight (ht);
				if (GribDataIsDef(per))
					s += "  " + Util::formatWavePeriod (per);
				if (GribDataIsDef(wdir))
					s += "  " + Util::formatWaveDirection (wdir);
				r[c] = s;
			}
			c++;
		}
		if (pf.hasWaveData (GRB_WAV_WHITCAP_PROB))
			r[c] = Util::formatWhiteCap (pf.getWaveData (GRB_WAV_WHITCAP_PROB));
		c++;

		float cspeed=0, cdir=0;
		if (pf.getCurrentValues (&cspeed, &cdir))
			r[c] = Util::formatDirection (cdir) + "  "
			     + Util::formatSpeed_Current (cspeed);
		c++;
		if (pf.hasWaterTemp()) r[c] = Util::formatTemperature (pf.waterTemp);
		c++;

		//-- air ------------------------------------------------------
		if (pf.hasPressureMSL()) r[c] = Util::formatPressure (pf.pressureMSL);
		c++;
		if (pf.hasTemp())        r[c] = Util::formatTemperature (pf.temp);
		c++;
		if (pf.hasDewPoint())    r[c] = Util::formatTemperature (pf.dewPoint);
		c++;
		if (pf.hasHumidRel())    r[c] = Util::formatPercentValue (pf.humidRel);
		c++;
		if (pf.hasRain())        r[c] = Util::formatRain (pf.rain);
		c++;
		if (pf.hasCloudTotal())  r[c] = Util::formatPercentValue (pf.cloudTotal);
		c++;
		if (pf.hasIsotherm0HGT())
			r[c] = Util::formatIsotherm0HGT (pf.isotherm0HGT);
		c++;
		if (pf.hasSnowDepth())   r[c] = Util::formatSnowDepth (pf.snowDepth);
		c++;
		if (pf.hasSnowCateg() && pf.snowCateg > 0.5)      r[c] = tr("yes");
		c++;
		if (pf.hasFrzRainCateg() && pf.frzRainCateg > 0.5) r[c] = tr("yes");
		c++;
		if (pf.hasCAPEsfc())     r[c] = Util::formatCAPEsfc (pf.CAPEsfc);
		c++;
		if (pf.hasCINsfc())      r[c] = Util::formatCAPEsfc (pf.CINsfc);
		c++;
		if (pf.hasCompReflect()) r[c] = Util::formatReflect (pf.compReflect);
		c++;

		rows.append (r);
	}

	// Keep the four route columns plus every column with at least one value.
	QVector<int> shown;
	for (int c=0; c<NCOL; c++) {
		bool any = (c < 4);
		for (int i=0; !any && i<rows.size(); i++)
			any = !rows[i][c].isEmpty();
		if (any)
			shown.append (c);
	}

	table->setColumnCount (shown.size());
	QStringList headers;
	for (int k=0; k<shown.size(); k++)
		headers << tr (titles[shown[k]]);
	table->setHorizontalHeaderLabels (headers);
	table->setRowCount (rows.size());

	for (int i=0; i<rows.size(); i++)
		for (int k=0; k<shown.size(); k++) {
			QString txt = rows[i][shown[k]];
			table->setItem (i, k,
			        new QTableWidgetItem (txt.isEmpty() ? "-" : txt));
		}

	table->resizeColumnsToContents ();
}
