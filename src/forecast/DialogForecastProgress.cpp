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

#include <QApplication>
#include <QObject>
#include <QProgressDialog>

#include "DialogForecastProgress.h"

//---------------------------------------------------------------------
DialogForecastProgress::DialogForecastProgress (QProgressDialog *dialog,
                                                int baseValue,
                                                const QString &label)
	: dlg (dialog), base (baseValue), name (label)
{
}
//---------------------------------------------------------------------
void DialogForecastProgress::message (const QString &text)
{
	if (dlg == nullptr)
		return;
	dlg->setLabelText (name.isEmpty() ? text : name + " — " + text);
	QApplication::processEvents ();
}
//---------------------------------------------------------------------
void DialogForecastProgress::step (int done, int total, qint64 bytes)
{
	if (dlg == nullptr || total <= 0)
		return;
	dlg->setValue (base + (100*done)/total);
	// Мегабайты тут важны: глубокая загрузка — это десятки отдельных
	// запросов, и без них медленную не отличить от зависшей.
	dlg->setLabelText (QString("%1 %2  %3 MB")
	        .arg (QObject::tr("Downloading from NOAA")).arg (name)
	        .arg (bytes/(1024.0*1024.0), 0, 'f', 1));
	QApplication::processEvents ();
}
//---------------------------------------------------------------------
bool DialogForecastProgress::canceled ()
{
	return dlg != nullptr && dlg->wasCanceled ();
}
