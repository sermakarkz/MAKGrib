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

#ifndef MULTIMODELLOADER_H
#define MULTIMODELLOADER_H

#include <QList>
#include <QObject>
#include <QString>

class QProgressDialog;

//===================================================================
// Downloads every forecast model the server can deliver for one area,
// so they can all be compared without downloading them one by one.
//
// Deliberately synchronous: the whole point is a single "fetch
// everything" action, and linear code is far easier to test than a
// chain of network callbacks.
//===================================================================
class MultiModelLoader : public QObject
{ Q_OBJECT
	public:
		struct ModelDef {
			QString label;       // shown to the user
			QString code;        // server model identifier
			bool    isWave;      // wave models travel in their own field
			int     maxDays;     // deepest forecast this model publishes
			int     minInterval; // finest time step it publishes, hours
		};

		struct Result {
			QString label;
			QString fileName;   // empty when nothing was downloaded
			QString note;       // why it failed, or a remark
			bool    ok;
			bool    widened;    // request had to be moved west, see below
			int     days;       // what was actually asked for
			int     interval;
			int     hours;      // depth actually obtained, in hours
		};

		explicit MultiModelLoader (QObject *parent=nullptr);

		static QList<ModelDef> defaultModels ();

		// x0,y0 - x1,y1 : the area, in degrees. destDir : where files go.
		// Every model is fetched at its own maximum depth and finest step,
		// from the latest run available; dayCap trims that when set.
		QList<Result> run (double x0, double y0, double x1, double y1,
		                   const QString &destDir,
		                   QProgressDialog *progress = nullptr,
		                   int dayCap = 0);

	private:
		// One model. Returns the result, downloading into destDir.
		Result fetch (const ModelDef &m,
		              double x0, double y0, double x1, double y1,
		              int days, int interval, const QString &destDir,
		              QProgressDialog *progress);

		// Asks the server to prepare a file; returns its URL or "".
		QString askServer (const ModelDef &m,
		                   double x0, double y0, double x1, double y1,
		                   int days, int interval, QString *error,
		                   QProgressDialog *progress = nullptr);

		// progress/baseValue let the dialog advance while the bytes come
		// in, instead of standing still for the whole of a large file.
		bool download (const QString &url, const QString &path,
		               QProgressDialog *progress, int baseValue,
		               const QString &label);
};

#endif
