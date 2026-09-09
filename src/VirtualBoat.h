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

#ifndef VIRTUALBOAT_H
#define VIRTUALBOAT_H

#include <ctime>
#include <set>
#include <vector>

#include <QPainter>
#include <QString>

#include "Projection.h"

//===================================================================
// Virtual boat : dead reckoning along a route made of waypoints.
//
// The boat leaves the first waypoint at startDate and follows the
// great circle to each next waypoint at a constant speed. Its
// position can then be evaluated for any date, which is what makes
// it possible to read the weather the boat would actually meet.
//===================================================================

class VirtualBoat
{
	public:
		struct Waypoint {
			double lon, lat;
		};

		struct TrackPoint {
			time_t date;
			double lon, lat;
			double distanceDone;   // nautical miles since departure
			double course;         // true course at this point (degrees)
			bool   arrived;        // route completed at (or before) this date
		};

		VirtualBoat ();

		//-- route ------------------------------------------------
		void clear ();
		void setStart (double lon, double lat, time_t date);
		void addWaypoint (double lon, double lat);
		void setWaypoint (int index, double lon, double lat);
		void removeWaypoint (int index);
		void insertWaypoint (int index, double lon, double lat);
		// Dead reckoning : append a waypoint deduced from a course
		// and a distance run from the last waypoint.
		void addLegByCourse (double courseDeg, double distanceNM);

		//-- editing on the map -----------------------------------
		// Waypoint under the given screen point, or -1.
		int findWaypoint (Projection *proj, int x, int y, int tolerance=8) const;
		// Leg under the given screen point, or -1. When found, lon/lat
		// receive the point of the leg nearest to the click, which is
		// where a new waypoint should be inserted.
		int findLeg (Projection *proj, int x, int y, int tolerance,
		             double *lon, double *lat) const;
		// Waypoint drawn emphasised, to show what the mouse would grab.
		void setHighlight (int index)    {highlight = index;}
		int  getHighlight () const       {return highlight;}

		const std::vector<Waypoint> & getWaypoints () const {return waypoints;}
		int  countWaypoints () const {return static_cast<int>(waypoints.size());}

		//-- parameters -------------------------------------------
		void   setSpeed (double kt)      {speedKt = (kt>0.01) ? kt : 0.01;}
		double getSpeed () const         {return speedKt;}
		void   setStartDate (time_t t)   {startDate = t;}
		time_t getStartDate () const     {return startDate;}
		void   setName (const QString &s){name = s;}
		QString getName () const         {return name;}

		void   setVisible (bool b)       {visible = b;}
		bool   isVisible () const        {return visible;}
		// A route needs at least a departure and an arrival to be sailed.
		bool   isDefined () const        {return waypoints.size() >= 2;}

		//-- results ----------------------------------------------
		double getLegDistance (int index) const;   // NM, leg index -> index+1
		double getTotalDistance () const;          // NM
		double getTotalDuration () const;          // hours
		time_t getArrivalDate () const;

		// Position at a given date. Returns false when the date is
		// before departure or when the route is not sailable.
		bool positionAt (time_t date,
		                 double *lon, double *lat,
		                 double *distanceDone, double *course) const;

		// One track point per GRIB date, from departure to arrival.
		std::vector<TrackPoint> makeTrack (const std::set<time_t> &dates) const;

		//-- drawing ----------------------------------------------
		void draw (QPainter &pnt, Projection *proj, time_t currentDate) const;
		// Elastic segment from the last waypoint to the cursor, drawn
		// while the route is being clicked on the map.
		void drawRubberBand (QPainter &pnt, Projection *proj,
		                     double lon, double lat) const;

		//-- persistence ------------------------------------------
		void writeSettings () const;
		void readSettings ();

	private:
		std::vector<Waypoint> waypoints;
		double  speedKt;
		time_t  startDate;
		bool    visible;
		int     highlight;
		QString name;

		// Great circle course from waypoint index to index+1.
		double  getLegCourse (int index) const;

		void    drawLeg (QPainter &pnt, Projection *proj,
		                 const Waypoint &a, const Waypoint &b) const;
		// Draws a screen segment, dropping it when it wraps around
		// the antimeridian.
		static void drawSegment (QPainter &pnt, Projection *proj,
		                         double lon0, double lat0,
		                         double lon1, double lat1);
};

#endif
