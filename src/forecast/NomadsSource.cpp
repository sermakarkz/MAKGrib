/**********************************************************************
MAKGrib: meteorological GRIB file viewer
Copyright (C) 2026 - MAKGrib contributors

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

#include <QObject>

#include "NomadsSource.h"
#include "NomadsLoader.h"

//---------------------------------------------------------------------
// Прослойка между двумя видами прогресса: NomadsLoader знает свой,
// программа снаружи — общий. Ничего, кроме переадресации.
namespace {
class Bridge : public NomadsProgress
{
	public:
		explicit Bridge (ForecastProgress *p) : outer (p) {}
		void message (const QString &t) override
		    { if (outer) outer->message (t); }
		void step (int done, int total, qint64 bytes) override
		    { if (outer) outer->step (done, total, bytes); }
		bool canceled () override
		    { return outer && outer->canceled(); }
	private:
		ForecastProgress *outer;
};

ForecastSource::Result convert (const NomadsLoader::Outcome &o)
{
	ForecastSource::Result r;
	r.ok        = o.ok;
	r.error     = o.error;
	r.run       = o.run;
	r.name      = o.name;
	r.hours     = o.hours;
	r.truncated = o.truncated;
	return r;
}
}

//---------------------------------------------------------------------
QString NomadsSource::name () const
{
	return QObject::tr("NOAA NOMADS");
}

//---------------------------------------------------------------------
QList<ForecastSource::Model> NomadsSource::models () const
{
	// Глубина и шаг — то, что NOAA действительно публикует: до +384 ч,
	// ежечасно до +120, дальше через три часа.
	QList<Model> lst;
	lst << Model {"gfs_p25_", QObject::tr("GFS 0.25° (атмосфера)"),
	              false, 10, 1}
	    << Model {"ww3_p50_", QObject::tr("GFS-Wave 0.25° (волнение)"),
	              true, 8, 1};
	return lst;
}

//---------------------------------------------------------------------
ForecastSource::Result NomadsSource::fetch (const Request &req, QByteArray *out,
                                            ForecastProgress *progress)
{
	if (!hasModel (req.modelId)) {
		Result r;
		r.error = QObject::tr("%1 не публикует эту модель").arg (name());
		return r;
	}
	Bridge bridge (progress);
	return convert (NomadsLoader::fetchInto (out, req.modelId,
	                req.x0, req.y0, req.x1, req.y1, req.days, req.interval,
	                progress ? &bridge : nullptr));
}

//---------------------------------------------------------------------
ForecastSource::Result NomadsSource::fetchToFile (const Request &req,
                                                  const QString &destDir,
                                                  QString *path,
                                                  ForecastProgress *progress)
{
	if (path != nullptr)
		path->clear ();
	if (!hasModel (req.modelId)) {
		Result r;
		r.error = QObject::tr("%1 не публикует эту модель").arg (name());
		return r;
	}
	Bridge bridge (progress);
	NomadsLoader::Outcome o = NomadsLoader::fetch (req.modelId,
	        req.x0, req.y0, req.x1, req.y1, req.days, req.interval, destDir,
	        progress ? &bridge : nullptr);
	if (path != nullptr)
		*path = o.path;
	return convert (o);
}
