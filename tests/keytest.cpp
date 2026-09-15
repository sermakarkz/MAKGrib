// Имена плиток по обе стороны договора.
//
// Нарезалка (tools/dwd-tiles.py) даёт файлу имя по юго-западному углу
// квадрата, а приложение считает то же имя само, по углам экрана, и по
// нему просит файл. Разойдись они хоть на ведущем нуле — приложение
// искало бы то, чего нет, и молча показывало «в этом районе пусто».
// Ошибка без единого сообщения, самая дорогая порода.
//
// Программа печатает имена, а сверяет их с питоновской стороной
// tests/keytest.py: держать два независимых счётчика в одном языке
// смысла нет, договор именно межъязыковой.
#include <cstdio>
#include <clocale>
#include <QCoreApplication>
#include "DwdTiles.h"

int main (int argc, char **argv)
{
	QCoreApplication app (argc, argv);
	// Qt при создании QCoreApplication переводит локаль на системную, и
	// printf начинает печатать дробные с запятой. Разбор на той стороне
	// на этом ломается — уже наступали.
	setlocale (LC_ALL, "C");

	for (int tile : {5, 20})
		for (double lat = -85; lat <= 85; lat += 2.5)
			for (double lon = -180; lon < 180; lon += 2.5)
				printf ("%d %.4f %.4f %s\n", tile, lon, lat,
				        DwdTiles::key (lon, lat, tile).toUtf8().constData());
	return 0;
}
