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

#ifndef NOMADSLOADER_H
#define NOMADSLOADER_H

#include <QByteArray>
#include <QObject>
#include <QString>

class QProgressDialog;

//===================================================================
// Where a download reports to while it runs. The multi-model download
// drives a QProgressDialog; the ordinary download dialog has a message
// line and a progress bar of its own. From in here they look alike.
//===================================================================
class NomadsProgress
{
	public:
		virtual ~NomadsProgress () {}
		// What is happening now, for a label or a status line.
		virtual void message (const QString &text) = 0;
		// done of total forecast hours fetched, and the bytes so far.
		virtual void step (int done, int total, qint64 bytes) = 0;
		// True once the user has asked to stop.
		virtual bool canceled () = 0;
};

//-------------------------------------------------------------------
// Drives a QProgressDialog, advancing from baseValue to baseValue+100.
class NomadsDialogProgress : public NomadsProgress
{
	public:
		NomadsDialogProgress (QProgressDialog *dialog, int baseValue,
		                      const QString &label);
		void message (const QString &text) override;
		void step (int done, int total, qint64 bytes) override;
		bool canceled () override;
	private:
		QProgressDialog *dlg;
		int      base;
		QString  name;
};

//===================================================================
// Builds a GRIB file straight from NOAA's NOMADS service, without
// going through the OpenGribs server.
//
// NOMADS has no equivalent of getmygribs2.php: there is no "prepare
// one file for me" step. Each forecast hour is its own request, and
// the file is made by concatenating the answers - a GRIB2 file is
// simply its messages one after another, so appending them is all
// that a valid file needs. Appending a wave model to an atmospheric
// one works for the same reason.
//
// This covers only what NOAA itself publishes, which is GFS for the
// atmosphere and GFS-Wave (WAVEWATCH III) for the sea. The European
// models have no counterpart here.
//===================================================================
class NomadsLoader : public QObject
{ Q_OBJECT
	public:
		struct Outcome {
			bool    ok;         // something usable was produced
			QString path;       // the file written, empty for fetchInto()
			QString name;       // bare file name, to suggest when saving
			QString error;      // why it stopped: the whole reason when
			                    // ok is false, the reason it fell short
			                    // when truncated is set
			QString run;        // the run used, e.g. "2026-09-08 06z"
			int     hours;      // deepest forecast hour actually obtained
			bool    truncated;  // the run ran out before the depth asked for
			Outcome () : ok(false), hours(0), truncated(false) {}
		};

		// True when NOMADS can stand in for this model code.
		static bool covers (const QString &modelCode);

		// Downloads into destDir.
		static Outcome fetch (const QString &modelCode,
		                      double x0, double y0, double x1, double y1,
		                      int days, int interval, const QString &destDir,
		                      NomadsProgress *progress = nullptr);

		// Same, but built in memory and appended to *out, so the caller
		// can hand the bytes straight on and ask where to save later.
		// Appending twice puts an atmospheric and a wave model in one
		// file, which is what the download dialog asks for.
		static Outcome fetchInto (QByteArray *out, const QString &modelCode,
		                          double x0, double y0, double x1, double y1,
		                          int days, int interval,
		                          NomadsProgress *progress = nullptr);
};

#endif
