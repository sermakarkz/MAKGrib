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

#ifndef NOMADSSOURCE_H
#define NOMADSSOURCE_H

#include "ForecastSource.h"

//===================================================================
// NOAA NOMADS: GFS для атмосферы и GFS-Wave для моря.
//
// Единственный из бесплатных источников, который вырезает нужный
// прямоугольник у себя, поэтому с телефона годится только он: всё
// остальное отдаёт поле целиком, мегабайт на параметр на срок.
//
// Волнения в закрытых морях — Каспийском, Чёрном, Азовском — у него
// нет: они вне сетки его волновой модели. Их даст GWAM, когда появится
// свой сервер.
//===================================================================
class NomadsSource : public ForecastSource
{
	public:
		QString name () const override;
		QList<Model> models () const override;

		Result fetch (const Request &req, QByteArray *out,
		              ForecastProgress *progress = nullptr) override;
		Result fetchToFile (const Request &req, const QString &destDir,
		                    QString *path,
		                    ForecastProgress *progress = nullptr) override;
};

#endif
