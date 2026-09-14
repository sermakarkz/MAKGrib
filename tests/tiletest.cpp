// Склейка плиток: прогноз, нарезанный квадратами, должен читаться так же,
// как тот же участок одним куском.
//
// Резать под экран, как это делает NOAA у себя на сервере, нам нечем —
// своего сервера нет. Поэтому квадраты нарезаются заранее, телефон берёт
// те, что накрывают экран, и склеивает их у себя. Движок же на каждый
// срок держит ровно одну запись: getRecord() возвращает первую подходящую
// и остальные плитки не заметил бы вовсе. Сводит их GribReader::mergeTiles.
//
// Образцы готовит tests/maketiles.py из настоящего поля ICON-EU.
#include <cstdio>
#include <cmath>
#include <set>
#include <QApplication>
#include <QString>
#include "GribReader.h"
#include "DataDefines.h"

static int bad = 0;

// Допуск на сверку значений. Ноль тут поставить нельзя, и дело не в
// склейке: цельный кусок паковался десятью битами на свой разброс
// значений, а каждая плитка — на свой, поменьше. Шаг квантования выходит
// разный, и одно и то же число записано чуть иначе. Для ветра это сотые
// доли метра в секунду — на порядки меньше, чем врёт сам прогноз.
static const double PACK_EPS = 0.02;

static void check (const char *what, bool ok, const QString &d = "")
{
	printf ("  [%s] %s%s\n", ok ? " OK " : "FAIL", what,
	        d.isEmpty() ? "" : (" -> " + d).toUtf8().constData());
	if (!ok) bad++;
}

// Ветер по меридиану на 10 м — это то, что лежит в образцах.
static const DataCode VWIND (GRB_WIND_VY, LV_ABOV_GND, 10);

int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	QString dir = argc > 1 ? QString (argv[1])
	                       : QString ("tests/data");

	GribReader whole, tiled, holed;
	whole.openFile (dir + "/whole.grb2", 1000);
	tiled.openFile (dir + "/tiles.grb2", 1000);
	holed.openFile (dir + "/tiles-hole.grb2", 1000);

	printf ("\n=== образцы прочитаны ===\n");
	check ("цельный участок открылся", whole.isOk());
	check ("плитки открылись",         tiled.isOk());
	check ("плитки с дыркой открылись", holed.isOk());
	if (bad) return bad;

	printf ("       сроков: цельный %d, плитки %d\n",
	        whole.getNumberOfDates(), tiled.getNumberOfDates());
	check ("сроков столько же, сколько в цельном",
	       whole.getNumberOfDates() == tiled.getNumberOfDates()
	       && whole.getNumberOfDates() == 2,
	       QString ("%1 против %2").arg (whole.getNumberOfDates())
	                               .arg (tiled.getNumberOfDates()));

	printf ("\n=== из четырёх плиток вышла одна сетка ===\n");
	std::set<time_t> dates = whole.getListDates();
	bool sameGrid = true, sameValues = true, anyChecked = false;
	double worst = 0;
	int compared = 0;

	for (time_t d : dates) {
		GribRecord *w = whole.getRecord (VWIND, d);
		GribRecord *t = tiled.getRecord (VWIND, d);
		if (w == nullptr || t == nullptr) {
			check ("запись найдена на оба среза", false);
			continue;
		}
		anyChecked = true;
		if (w->getNi() != t->getNi() || w->getNj() != t->getNj()
		 || fabs (w->getXmin() - t->getXmin()) > 1e-6
		 || fabs (w->getYmin() - t->getYmin()) > 1e-6
		 || fabs (w->getXmax() - t->getXmax()) > 1e-6
		 || fabs (w->getYmax() - t->getYmax()) > 1e-6)
		{
			sameGrid = false;
			printf ("       цельный %dx%d [%.4f..%.4f, %.4f..%.4f]\n",
			        w->getNi(), w->getNj(), w->getXmin(), w->getXmax(),
			        w->getYmin(), w->getYmax());
			printf ("       склейка %dx%d [%.4f..%.4f, %.4f..%.4f]\n",
			        t->getNi(), t->getNj(), t->getXmin(), t->getXmax(),
			        t->getYmin(), t->getYmax());
			continue;
		}
		for (int j = 0; j < w->getNj(); j++)
			for (int i = 0; i < w->getNi(); i++) {
				if (!w->hasValue (i, j))
					continue;
				if (!t->hasValue (i, j)) { sameValues = false; continue; }
				double e = fabs (w->getValue (i,j) - t->getValue (i,j));
				if (e > worst) worst = e;
				if (e > PACK_EPS) sameValues = false;
				compared++;
			}
	}
	check ("сетка совпала с цельной", sameGrid);
	check ("все точки на месте", anyChecked && compared > 10000,
	       QString ("сверено %1 точек").arg (compared));
	check ("значения те же с точностью упаковки", sameValues,
	       QString ("наибольшее расхождение %1 при допуске %2")
	           .arg (worst, 0, 'f', 4).arg (PACK_EPS));

	printf ("\n=== пропущенная плитка не портит соседей ===\n");
	// Северо-восточного квадрата в образце нет: там должна быть дырка,
	// а остальные три четверти — как в цельном участке.
	bool holeEmpty = true, restOk = true;
	int holePoints = 0, restPoints = 0;
	for (time_t d : dates) {
		GribRecord *w = whole.getRecord (VWIND, d);
		GribRecord *h = holed.getRecord (VWIND, d);
		if (w == nullptr || h == nullptr) { restOk = false; continue; }
		for (int j = 0; j < w->getNj(); j++)
			for (int i = 0; i < w->getNi(); i++) {
				double lon, lat;
				w->getXY (i, j, &lon, &lat);
				double x, y;
				h->lonLat2XY (lon, lat, &x, &y);
				int hi = (int) floor (x + 0.5), hj = (int) floor (y + 0.5);
				// Строго внутри выпавшего квадрата, без общих краёв:
				// края принадлежат и соседям тоже.
				if (lon > 45.0 && lat > 40.0) {
					if (h->hasValue (hi, hj)) holeEmpty = false;
					holePoints++;
				}
				else if (w->hasValue (i, j)) {
					if (!h->hasValue (hi, hj)
					 || fabs (h->getValue (hi,hj) - w->getValue (i,j)) > PACK_EPS)
						restOk = false;
					restPoints++;
				}
			}
	}
	check ("на месте пропущенной плитки пусто", holeEmpty,
	       QString ("точек в дырке %1").arg (holePoints));
	check ("остальные плитки не сдвинулись", restOk,
	       QString ("сверено %1 точек").arg (restPoints));

	printf ("\n%s\n", bad == 0 ? "всё сошлось" : "есть расхождения");
	return bad;
}
