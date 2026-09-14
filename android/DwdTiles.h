/**********************************************************************
Прогнозы DWD, нарезанные плитками.

У NOAA есть программа на сервере, которая режет прогноз по присланному
прямоугольнику: запросил свой экран — получил ровно его. Повторить нечем,
своего сервера нет. Поэтому квадраты нарезаны заранее (tools/dwd-tiles.py),
а здесь мы считаем, какие из них накрывают экран, сколько они весят и
забираем их. Сходятся они в одну сетку уже в движке — GribReader::mergeTiles.

Вес считается по описи, где записан точный размер каждого файла. Это не
прикидка: человек видит, во что обойдётся загрузка, до того как её начал.
У большинства связь мобильная в роуминге или спутниковая с лимитом.
***********************************************************************/
#ifndef DWDTILES_H
#define DWDTILES_H

#include <functional>

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

class DwdTiles
{
	public:
		struct Set
		{
			QString id;          // eu-wind, world-wave, ...
			QString title;       // «Ветер ICON-EU, 7 км»
			QString model;       // «DWD ICON-EU»
			QString kind;        // wind или wave
			QStringList fields;  // что внутри: ветер, зыбь...
			QString run;         // выпуск, 20260914T18:00Z
			double  west, east, south, north;
			int     tile;        // сторона квадрата в градусах
			int     days;        // на сколько суток нарезано
			int     interval;    // шаг по времени, часов
			QString index;       // файл описи размеров
			// Плитка -> размер по суткам, в байтах. Пусто, пока опись
			// не прочитана: её тянем только для тех наборов, что вообще
			// попадают в район.
			QHash<QString, QList<qint64>> sizes;
			bool    detailed = false;
		};

		explicit DwdTiles (QNetworkAccessManager *net);

		// Перечитать опись. Чаще раза в час незачем: плитки обновляются
		// дважды в сутки.
		bool  refresh (bool force = false);
		const QList<Set> &sets () const   { return list; }
		// Не только для чтения: опись размеров подтягивается лениво,
		// прямо в набор, когда о его весе впервые спросили.
		QList<Set> &sets ()               { return list; }
		QString lastError () const        { return trouble; }

		// Есть ли этот набор в таком участке.
		bool  covers (const Set &s, double x0, double y0,
		              double x1, double y1) const;
		// Сколько байт займёт, если брать на столько суток.
		qint64 weigh (Set &s, double x0, double y0, double x1, double y1,
		              int days);
		// Забрать плитки и дописать к out. Возвращает, сколько взято.
		int   fetch (Set &s, double x0, double y0, double x1, double y1,
		             int days, QByteArray *out,
		             const std::function<void(int,int,qint64)> &tell);

		// Имя квадрата по его юго-западному углу: N45E045.
		static QString key (double lon, double lat, int tile);

	private:
		QByteArray get (const QString &url);
		bool  loadIndex (Set &s);
		QList<QString> tilesFor (const Set &s, double x0, double y0,
		                         double x1, double y1) const;

		QNetworkAccessManager *net;
		QList<Set>   list;
		QDateTime    fetched;
		QString      trouble;
};

#endif
