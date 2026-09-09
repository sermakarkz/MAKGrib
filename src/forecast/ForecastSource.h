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

#ifndef FORECASTSOURCE_H
#define FORECASTSOURCE_H

#include <QByteArray>
#include <QList>
#include <QString>

//===================================================================
// Откуда берётся прогноз.
//
// Сегодня источник один — NOAA NOMADS, потому что он единственный
// умеет вырезать нужный кусок на своей стороне. Завтра появится свой
// сервер, и с ним ICON, ARPEGE, GWAM и остальное, чего у NOAA нет:
// волнение на Каспии, например, считает только GWAM.
//
// Поэтому вся программа разговаривает не с загрузчиком, а с этим
// интерфейсом. Добавить сервер потом — это новый класс рядом с
// NomadsSource и одна строка в реестре, а не переделка интерфейса.
//===================================================================

//-------------------------------------------------------------------
// Ход загрузки: сообщения и возможность остановиться.
class ForecastProgress
{
	public:
		virtual ~ForecastProgress () {}
		// Что происходит сейчас — строкой, для показа человеку.
		virtual void message (const QString &text) = 0;
		// done из total шагов, и сколько байт уже пришло.
		virtual void step (int done, int total, qint64 bytes) = 0;
		// true, как только пользователь попросил прекратить.
		virtual bool canceled () = 0;
};

//-------------------------------------------------------------------
class ForecastSource
{
	public:
		// Модель прогноза в том виде, в каком её выбирает человек.
		struct Model {
			QString id;          // как источник её называет внутри себя
			QString label;       // как она называется на экране
			bool    wave;        // волнение, а не атмосфера
			int     maxDays;     // на сколько суток считает
			int     minInterval; // самый мелкий шаг, часы
		};

		// Что просим.
		struct Request {
			QString modelId;
			double  x0, y0, x1, y1;   // запад, юг, восток, север
			int     days;
			int     interval;         // часы
		};

		// Что получилось.
		struct Result {
			bool    ok;
			QString error;      // почему не вышло, либо чего не хватило
			QString run;        // расчёт, например «2026-09-09 06z»
			QString name;       // как назвать файл
			int     hours;      // до какого часа реально дотянули
			bool    truncated;  // расчёт кончился раньше, чем просили
			Result () : ok(false), hours(0), truncated(false) {}
		};

		virtual ~ForecastSource () {}

		// Как называется источник — это попадает в сообщения человеку,
		// чтобы было понятно, чьи данные и чей сервер молчит.
		virtual QString name () const = 0;

		// Что этот источник умеет отдать.
		virtual QList<Model> models () const = 0;

		// Есть ли у него такая модель.
		bool hasModel (const QString &modelId) const
		{
			for (const Model &m : models())
				if (m.id == modelId)
					return true;
			return false;
		}

		// Загрузка в память. Данные дописываются в конец *out, поэтому
		// два вызова подряд складывают атмосферу и волнение в один файл —
		// GRIB это допускает, он просто идёт сообщение за сообщением.
		virtual Result fetch (const Request &req, QByteArray *out,
		                      ForecastProgress *progress = nullptr) = 0;

		// То же, но сразу файлом в каталоге. Имя выбирает источник и
		// возвращает его в Result::name.
		virtual Result fetchToFile (const Request &req, const QString &destDir,
		                            QString *path,
		                            ForecastProgress *progress = nullptr) = 0;
};

#endif
