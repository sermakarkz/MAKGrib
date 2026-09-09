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

#ifndef SOURCEREGISTRY_H
#define SOURCEREGISTRY_H

#include <QList>
#include <QString>

#include "ForecastSource.h"

//===================================================================
// Список источников в порядке предпочтения.
//
// Сейчас в нём один NOMADS. Когда поднимем свой сервер, он встанет
// первым: он умеет то, чего у NOAA нет — ICON, ARPEGE, GWAM с
// волнением на Каспии, — а NOMADS останется запасным на случай, когда
// свой сервер молчит. Именно того запасного пути и не хватало
// OpenGribs, из-за чего пятого сентября всё встало разом.
//
// Владение источниками остаётся здесь; наружу отдаются указатели.
//===================================================================
class SourceRegistry
{
	public:
		static SourceRegistry &instance ();
		~SourceRegistry ();

		// Все источники, лучший первым.
		QList<ForecastSource *> sources () const;

		// Все модели ото всех источников, в том же порядке. Для списка
		// на экране: человек выбирает модель, а не сервер.
		struct Offer {
			ForecastSource        *source;
			ForecastSource::Model  model;
		};
		QList<Offer> offers () const;

		// Кто отдаст эту модель. nullptr — никто.
		ForecastSource *sourceFor (const QString &modelId) const;

	private:
		SourceRegistry ();
		SourceRegistry (const SourceRegistry &) = delete;
		SourceRegistry &operator= (const SourceRegistry &) = delete;
		QList<ForecastSource *> list;
};

#endif
