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

#include <cmath>
#include <cstdlib>

#include <QStringList>
#include <QPolygonF>

#include "VirtualBoat.h"
#include "Orthodromie.h"
#include "Util.h"

//---------------------------------------------------------------------
// Brings a longitude back into [-180, 180[ so that the projection and
// the antimeridian test below always see comparable values.
static double normalizeLon (double lon)
{
	while (lon >= 180.0)  lon -= 360.0;
	while (lon <  -180.0) lon += 360.0;
	return lon;
}

//=====================================================================
VirtualBoat::VirtualBoat ()
{
	speedKt   = 8.0;
	startDate = 0;
	visible   = true;
	highlight = -1;
	name      = "";
}
//---------------------------------------------------------------------
void VirtualBoat::clear ()
{
	waypoints.clear ();
}
//---------------------------------------------------------------------
void VirtualBoat::setStart (double lon, double lat, time_t date)
{
	waypoints.clear ();
	Waypoint w;
	w.lon = normalizeLon (lon);
	w.lat = lat;
	waypoints.push_back (w);
	startDate = date;
}
//---------------------------------------------------------------------
void VirtualBoat::addWaypoint (double lon, double lat)
{
	Waypoint w;
	w.lon = normalizeLon (lon);
	w.lat = lat;
	waypoints.push_back (w);
}
//---------------------------------------------------------------------
void VirtualBoat::setWaypoint (int index, double lon, double lat)
{
	if (index < 0 || index >= countWaypoints())
		return;
	waypoints[index].lon = normalizeLon (lon);
	waypoints[index].lat = lat;
}
//---------------------------------------------------------------------
void VirtualBoat::removeWaypoint (int index)
{
	if (index < 0 || index >= countWaypoints())
		return;
	waypoints.erase (waypoints.begin() + index);
}
//---------------------------------------------------------------------
void VirtualBoat::insertWaypoint (int index, double lon, double lat)
{
	if (index < 0) index = 0;
	if (index > countWaypoints()) index = countWaypoints();
	Waypoint w;
	w.lon = normalizeLon (lon);
	w.lat = lat;
	waypoints.insert (waypoints.begin() + index, w);
}
//---------------------------------------------------------------------
void VirtualBoat::addLegByCourse (double courseDeg, double distanceNM)
{
	if (waypoints.empty() || distanceNM <= 0)
		return;
	const Waypoint &last = waypoints.back ();
	double lon, lat;
	Orthodromie ortho (0,0, 0,0);
	ortho.getCoordsForDist (last.lon, last.lat, distanceNM, courseDeg, &lon, &lat);
	addWaypoint (lon, lat);
}

//=====================================================================
double VirtualBoat::getLegDistance (int index) const
{
	if (index < 0 || index+1 >= countWaypoints())
		return 0;
	Orthodromie ortho (waypoints[index].lon,   waypoints[index].lat,
	                   waypoints[index+1].lon, waypoints[index+1].lat);
	return ortho.getDistance ();
}
//---------------------------------------------------------------------
double VirtualBoat::getLegCourse (int index) const
{
	if (index < 0 || index+1 >= countWaypoints())
		return 0;
	Orthodromie ortho (waypoints[index].lon,   waypoints[index].lat,
	                   waypoints[index+1].lon, waypoints[index+1].lat);
	return ortho.getAzimutDeg ();
}
//---------------------------------------------------------------------
double VirtualBoat::getTotalDistance () const
{
	double d = 0;
	for (int i=0; i+1<countWaypoints(); i++)
		d += getLegDistance (i);
	return d;
}
//---------------------------------------------------------------------
double VirtualBoat::getTotalDuration () const
{
	return getTotalDistance() / speedKt;
}
//---------------------------------------------------------------------
time_t VirtualBoat::getArrivalDate () const
{
	return startDate + static_cast<time_t>(getTotalDuration()*3600.0 + 0.5);
}

//=====================================================================
bool VirtualBoat::positionAt (time_t date,
                              double *lon, double *lat,
                              double *distanceDone, double *course) const
{
	if (!isDefined() || date < startDate)
		return false;

	double elapsedHours = static_cast<double>(date - startDate) / 3600.0;
	double runNM        = elapsedHours * speedKt;
	double total        = getTotalDistance ();

	// Arrived : stay on the last waypoint.
	if (runNM >= total) {
		const Waypoint &end = waypoints.back ();
		if (lon)          *lon = end.lon;
		if (lat)          *lat = end.lat;
		if (distanceDone) *distanceDone = total;
		if (course)       *course = getLegCourse (countWaypoints()-2);
		return true;
	}

	// Walk the legs until the run is used up.
	double done = 0;
	for (int i=0; i+1<countWaypoints(); i++) {
		double legDist = getLegDistance (i);
		if (runNM <= done + legDist || i+2 == countWaypoints()) {
			double intoLeg = runNM - done;
			if (intoLeg < 0)
				intoLeg = 0;
			double legCourse = getLegCourse (i);
			double x, y;
			Orthodromie ortho (0,0, 0,0);
			ortho.getCoordsForDist (waypoints[i].lon, waypoints[i].lat,
			                        intoLeg, legCourse, &x, &y);
			// Recompute the course from the reached point: on a great
			// circle the heading drifts along the leg.
			Orthodromie here (normalizeLon(x), y,
			                  waypoints[i+1].lon, waypoints[i+1].lat);
			if (lon)          *lon = normalizeLon (x);
			if (lat)          *lat = y;
			if (distanceDone) *distanceDone = runNM;
			if (course)       *course = here.getAzimutDeg ();
			return true;
		}
		done += legDist;
	}
	return false;
}

//---------------------------------------------------------------------
std::vector<VirtualBoat::TrackPoint>
VirtualBoat::makeTrack (const std::set<time_t> &dates) const
{
	std::vector<TrackPoint> track;
	if (!isDefined())
		return track;

	time_t arrival = getArrivalDate ();
	bool   arrivalAdded = false;

	for (std::set<time_t>::const_iterator it=dates.begin(); it!=dates.end(); ++it)
	{
		time_t date = *it;
		if (date < startDate)
			continue;

		// Insert the exact arrival time in the right place, so the last
		// row of the table is the real ETA and not the GRIB step after it.
		if (date > arrival && !arrivalAdded) {
			TrackPoint tp;
			tp.date = arrival;
			positionAt (arrival, &tp.lon, &tp.lat, &tp.distanceDone, &tp.course);
			tp.arrived = true;
			track.push_back (tp);
			arrivalAdded = true;
			break;
		}

		TrackPoint tp;
		tp.date = date;
		if (!positionAt (date, &tp.lon, &tp.lat, &tp.distanceDone, &tp.course))
			continue;
		tp.arrived = (date >= arrival);
		if (tp.arrived)
			arrivalAdded = true;
		track.push_back (tp);
		if (tp.arrived)
			break;
	}

	// The whole GRIB ends before the boat arrives : nothing more to add.
	return track;
}

//=====================================================================
// Squared distance from point (px,py) to the screen segment (ax,ay)-(bx,by).
static double distToSegment2 (double px, double py,
                              double ax, double ay, double bx, double by)
{
	double dx = bx-ax, dy = by-ay;
	double len2 = dx*dx + dy*dy;
	double t = (len2 < 1e-9) ? 0.0 : ((px-ax)*dx + (py-ay)*dy) / len2;
	if (t < 0) t = 0;
	else if (t > 1) t = 1;
	double cx = ax + t*dx, cy = ay + t*dy;
	return (px-cx)*(px-cx) + (py-cy)*(py-cy);
}
//---------------------------------------------------------------------
int VirtualBoat::findWaypoint (Projection *proj, int x, int y, int tolerance) const
{
	int best = -1;
	double bestDist2 = static_cast<double>(tolerance)*tolerance;
	for (int i=0; i<countWaypoints(); i++) {
		int wx, wy;
		proj->map2screen (waypoints[i].lon, waypoints[i].lat, &wx, &wy);
		double d2 = static_cast<double>(x-wx)*(x-wx)
		          + static_cast<double>(y-wy)*(y-wy);
		if (d2 <= bestDist2) {
			bestDist2 = d2;
			best = i;
		}
	}
	return best;
}
//---------------------------------------------------------------------
int VirtualBoat::findLeg (Projection *proj, int x, int y, int tolerance,
                          double *lon, double *lat) const
{
	int best = -1;
	double bestDist2 = static_cast<double>(tolerance)*tolerance;

	for (int i=0; i+1<countWaypoints(); i++)
	{
		// The leg is drawn as a great circle, so hit test the same
		// sampled polyline that is actually on screen.
		Orthodromie ortho (waypoints[i].lon,   waypoints[i].lat,
		                   waypoints[i+1].lon, waypoints[i+1].lat);
		double dist   = ortho.getDistance ();
		double course = ortho.getAzimutDeg ();
		int steps = static_cast<int>(dist / 20.0) + 1;
		if (steps < 2)   steps = 2;
		if (steps > 300) steps = 300;

		double prevLon = waypoints[i].lon, prevLat = waypoints[i].lat;
		for (int k=1; k<=steps; k++)
		{
			double clon, clat;
			if (k == steps) {
				clon = waypoints[i+1].lon;
				clat = waypoints[i+1].lat;
			}
			else {
				Orthodromie o (0,0, 0,0);
				o.getCoordsForDist (waypoints[i].lon, waypoints[i].lat,
				                    dist*k/steps, course, &clon, &clat);
			}
			int ax, ay, bx, by;
			proj->map2screen (normalizeLon(prevLon), prevLat, &ax, &ay);
			proj->map2screen (normalizeLon(clon), clat, &bx, &by);
			// Ignore the piece that wraps around the antimeridian.
			if (std::abs (bx-ax) <= proj->getW()/2) {
				double d2 = distToSegment2 (x, y, ax, ay, bx, by);
				if (d2 <= bestDist2) {
					bestDist2 = d2;
					best = i;
					// Insert at the middle of the sub-segment we hit.
					if (lon) *lon = (prevLon + clon) / 2.0;
					if (lat) *lat = (prevLat + clat) / 2.0;
				}
			}
			prevLon = clon;
			prevLat = clat;
		}
	}
	return best;
}

//=====================================================================
void VirtualBoat::drawSegment (QPainter &pnt, Projection *proj,
                               double lon0, double lat0,
                               double lon1, double lat1)
{
	int x0, y0, x1, y1;
	proj->map2screen (normalizeLon(lon0), lat0, &x0, &y0);
	proj->map2screen (normalizeLon(lon1), lat1, &x1, &y1);
	// A segment that jumps across most of the window is a wrap around
	// the antimeridian, not a real course : drop it.
	if (std::abs (x1-x0) > proj->getW()/2)
		return;
	pnt.drawLine (x0, y0, x1, y1);
}
//---------------------------------------------------------------------
void VirtualBoat::drawLeg (QPainter &pnt, Projection *proj,
                           const Waypoint &a, const Waypoint &b) const
{
	Orthodromie ortho (a.lon, a.lat, b.lon, b.lat);
	double dist   = ortho.getDistance ();
	double course = ortho.getAzimutDeg ();

	// Follow the great circle instead of drawing a straight screen line:
	// on long legs the two differ a lot.
	int steps = static_cast<int>(dist / 20.0) + 1;
	if (steps < 2)   steps = 2;
	if (steps > 300) steps = 300;

	double prevLon = a.lon, prevLat = a.lat;
	for (int k=1; k<=steps; k++)
	{
		double lon, lat;
		if (k == steps) {
			lon = b.lon;
			lat = b.lat;
		}
		else {
			Orthodromie o (0,0, 0,0);
			o.getCoordsForDist (a.lon, a.lat, dist*k/steps, course, &lon, &lat);
		}
		drawSegment (pnt, proj, prevLon, prevLat, lon, lat);
		prevLon = lon;
		prevLat = lat;
	}
}
//---------------------------------------------------------------------
void VirtualBoat::draw (QPainter &pnt, Projection *proj, time_t currentDate) const
{
	if (!visible || countWaypoints() < 1)
		return;

	pnt.save ();
	pnt.setRenderHint (QPainter::Antialiasing, true);

	const QColor routeColor (200, 30, 60);

	//-- the route itself ---------------------------------------------
	if (countWaypoints() >= 2) {
		QPen pen (routeColor);
		pen.setWidthF (2.0);
		pen.setStyle (Qt::DashLine);
		pnt.setPen (pen);
		pnt.setBrush (Qt::NoBrush);
		for (int i=0; i+1<countWaypoints(); i++)
			drawLeg (pnt, proj, waypoints[i], waypoints[i+1]);
	}

	//-- waypoints ----------------------------------------------------
	QPen penWp (Qt::white);
	penWp.setWidthF (1.2);
	pnt.setPen (penWp);
	pnt.setBrush (QBrush (routeColor));
	for (int i=0; i<countWaypoints(); i++) {
		int x, y;
		proj->map2screen (waypoints[i].lon, waypoints[i].lat, &x, &y);
		if (i == highlight) {
			// The waypoint the mouse would grab: bigger, with a halo.
			pnt.setBrush (Qt::NoBrush);
			QPen halo (QColor (255, 255, 255, 200));
			halo.setWidthF (2.0);
			pnt.setPen (halo);
			pnt.drawEllipse (QPoint(x,y), 9, 9);
			pnt.setPen (penWp);
			pnt.setBrush (QBrush (routeColor));
			pnt.drawEllipse (QPoint(x,y), 6, 6);
		}
		else {
			pnt.drawEllipse (QPoint(x,y), 4, 4);
		}
	}

	//-- the boat at the displayed date -------------------------------
	double lon, lat, dist, course;
	if (isDefined() && positionAt (currentDate, &lon, &lat, &dist, &course))
	{
		int x, y;
		proj->map2screen (lon, lat, &x, &y);

		// An arrow head pointing to the current course.
		QPolygonF arrow;
		arrow << QPointF ( 0, -11)
		      << QPointF ( 7,   9)
		      << QPointF ( 0,   4)
		      << QPointF (-7,   9);

		pnt.save ();
		pnt.translate (x, y);
		pnt.rotate (course);
		QPen penBoat (Qt::white);
		penBoat.setWidthF (1.5);
		pnt.setPen (penBoat);
		pnt.setBrush (QBrush (routeColor));
		pnt.drawPolygon (arrow);
		pnt.restore ();

		//-- label ----------------------------------------------------
		QString txt = Util::formatDateTimeShort (currentDate)
		            + "  " + Util::formatDistance (dist);
		if (dist >= getTotalDistance() - 0.01)
			txt += "  " + QObject::tr("(arrived)");

		QFont font = pnt.font ();
		font.setBold (true);
		pnt.setFont (font);
		QFontMetrics fmet (font);
		QRect rect = fmet.boundingRect (txt);
		rect.adjust (-4, -2, 4, 2);
		rect.moveTo (x+14, y-rect.height()/2);

		pnt.setPen (Qt::NoPen);
		pnt.setBrush (QColor (255, 255, 255, 200));
		pnt.drawRoundedRect (rect, 3, 3);
		pnt.setPen (QColor (40, 40, 40));
		pnt.drawText (rect, Qt::AlignCenter, txt);
	}

	pnt.restore ();
}

//---------------------------------------------------------------------
void VirtualBoat::drawRubberBand (QPainter &pnt, Projection *proj,
                                  double lon, double lat) const
{
	if (waypoints.empty())
		return;

	pnt.save ();
	pnt.setRenderHint (QPainter::Antialiasing, true);

	Waypoint cursor;
	cursor.lon = normalizeLon (lon);
	cursor.lat = lat;

	QPen pen (QColor (200, 30, 60, 170));
	pen.setWidthF (1.6);
	pen.setStyle (Qt::DotLine);
	pnt.setPen (pen);
	pnt.setBrush (Qt::NoBrush);
	drawLeg (pnt, proj, waypoints.back(), cursor);

	// Distance and course of the leg being drawn, next to the cursor.
	Orthodromie ortho (waypoints.back().lon, waypoints.back().lat,
	                   cursor.lon, cursor.lat);
	QString txt = QString::number (ortho.getAzimutDeg(), 'f', 0) + "°  "
	            + Util::formatDistance (static_cast<float>(ortho.getDistance()));

	int x, y;
	proj->map2screen (cursor.lon, cursor.lat, &x, &y);
	QFontMetrics fmet (pnt.font());
	QRect rect = fmet.boundingRect (txt);
	rect.adjust (-4, -2, 4, 2);
	rect.moveTo (x+16, y+10);

	pnt.setPen (Qt::NoPen);
	pnt.setBrush (QColor (255, 255, 255, 200));
	pnt.drawRoundedRect (rect, 3, 3);
	pnt.setPen (QColor (40, 40, 40));
	pnt.drawText (rect, Qt::AlignCenter, txt);

	pnt.restore ();
}

//=====================================================================
void VirtualBoat::writeSettings () const
{
	// Stored as a QStringList: QSettings uses the comma as its own list
	// separator in .ini files, so a hand-joined "lon,lat;..." string comes
	// back mangled. One "lon lat" element per waypoint avoids that.
	QStringList lst;
	for (int i=0; i<countWaypoints(); i++)
		lst << QString("%1 %2").arg(waypoints[i].lon, 0, 'f', 6)
		                       .arg(waypoints[i].lat, 0, 'f', 6);

	Util::setSetting ("virtualBoatRoute",   lst);
	Util::setSetting ("virtualBoatSpeed",   speedKt);
	Util::setSetting ("virtualBoatStart",   static_cast<qlonglong>(startDate));
	Util::setSetting ("virtualBoatVisible", visible);
	Util::setSetting ("virtualBoatName",    name);
}
//---------------------------------------------------------------------
void VirtualBoat::readSettings ()
{
	waypoints.clear ();

	const QStringList lst =
	        Util::getSetting ("virtualBoatRoute", QStringList()).toStringList();
	for (int i=0; i<lst.size(); i++) {
		const QStringList xy = lst.at(i).simplified().split (" ", Qt::SkipEmptyParts);
		if (xy.size() != 2)
			continue;
		bool ok1=false, ok2=false;
		double lon = xy.at(0).toDouble (&ok1);
		double lat = xy.at(1).toDouble (&ok2);
		if (ok1 && ok2)
			addWaypoint (lon, lat);
	}

	speedKt   = Util::getSetting ("virtualBoatSpeed", 8.0).toDouble();
	if (speedKt <= 0.01)
		speedKt = 8.0;
	startDate = static_cast<time_t>(
	                Util::getSetting ("virtualBoatStart", 0).toLongLong());
	visible   = Util::getSetting ("virtualBoatVisible", true).toBool();
	name      = Util::getSetting ("virtualBoatName", "").toString();
}
