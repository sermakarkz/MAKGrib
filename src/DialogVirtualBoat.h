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

#ifndef DIALOGVIRTUALBOAT_H
#define DIALOGVIRTUALBOAT_H

#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

#include "DialogBoxBase.h"
#include "GriddedPlotter.h"
#include "VirtualBoat.h"

//===================================================================
// Route editor for the virtual boat.
//===================================================================
class DialogVirtualBoat : public DialogBoxBase
{ Q_OBJECT
	public:
		DialogVirtualBoat (VirtualBoat *boat, GriddedPlotter *plotter,
		                   QWidget *parent=nullptr);

	signals:
		void signalBoatChanged ();

	public slots:
		void slotBtOK ();
		void slotBtCancel ();
		void slotBtAddWaypoint ();
		void slotBtRemoveWaypoint ();
		void slotBtAddLeg ();
		void slotBtClear ();
		void slotValueChanged ();
		void slotCellChanged (int row, int col);

	private:
		VirtualBoat    *boat;
		GriddedPlotter *plotter;
		bool            updating;

		QTableWidget   *tableWaypoints;
		QDateTimeEdit  *edStartDate;
		QDoubleSpinBox *edSpeed;
		QDoubleSpinBox *edCourse;
		QDoubleSpinBox *edLegDistance;
		QLabel         *lbSummary;

		QPushButton *btAdd, *btRemove, *btAddLeg, *btClear, *btOK, *btCancel;

		void fillTable ();
		void updateSummary ();
		// Reads the table back into the boat, so the summary and the map
		// always show what the user actually typed.
		void applyToBoat ();
};

//===================================================================
// Weather the boat meets along its route, one row per GRIB step.
//===================================================================
class BoatTrackWindow : public QWidget
{ Q_OBJECT
	public:
		BoatTrackWindow (VirtualBoat *boat, GriddedPlotter *plotter,
		                 QWidget *parent=nullptr);

		// The plotter is replaced whenever another GRIB file is opened.
		void setPlotter (GriddedPlotter *p)  {plotter = p;}

	public slots:
		void refresh ();

	private:
		VirtualBoat    *boat;
		GriddedPlotter *plotter;
		QTableWidget   *table;
		QLabel         *lbHeader;
};

#endif
