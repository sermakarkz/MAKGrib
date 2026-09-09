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

#include "SourceRegistry.h"
#include "NomadsSource.h"

//---------------------------------------------------------------------
SourceRegistry &SourceRegistry::instance ()
{
	static SourceRegistry one;
	return one;
}
//---------------------------------------------------------------------
SourceRegistry::SourceRegistry ()
{
	// Свой сервер встанет сюда, перед NOMADS.
	list << new NomadsSource ();
}
//---------------------------------------------------------------------
SourceRegistry::~SourceRegistry ()
{
	qDeleteAll (list);
	list.clear ();
}
//---------------------------------------------------------------------
QList<ForecastSource *> SourceRegistry::sources () const
{
	return list;
}
//---------------------------------------------------------------------
QList<SourceRegistry::Offer> SourceRegistry::offers () const
{
	QList<Offer> all;
	for (ForecastSource *s : list)
		for (const ForecastSource::Model &m : s->models())
			all << Offer {s, m};
	return all;
}
//---------------------------------------------------------------------
ForecastSource *SourceRegistry::sourceFor (const QString &modelId) const
{
	for (ForecastSource *s : list)
		if (s->hasModel (modelId))
			return s;
	return nullptr;
}
