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

#ifndef DIALOGFORECASTPROGRESS_H
#define DIALOGFORECASTPROGRESS_H

#include "ForecastSource.h"

class QProgressDialog;

//===================================================================
// Ход загрузки — в QProgressDialog. Полоса идёт от baseValue до
// baseValue+100, чтобы несколько загрузок подряд складывались в одну
// шкалу. На телефоне вместо этого будет свой класс: интерфейс тот же.
//===================================================================
class DialogForecastProgress : public ForecastProgress
{
	public:
		DialogForecastProgress (QProgressDialog *dialog, int baseValue,
		                        const QString &label);
		void message (const QString &text) override;
		void step (int done, int total, qint64 bytes) override;
		bool canceled () override;
	private:
		QProgressDialog *dlg;
		int      base;
		QString  name;
};

#endif
