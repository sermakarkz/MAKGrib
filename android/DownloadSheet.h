/**********************************************************************
Окно выбора прогноза.

Кнопка «Скачать» открывает не загрузку, а выбор: какие источники взять,
на сколько суток и с каким шагом. Вес пересчитывается на каждое касание,
и на самой кнопке стоит не слово, а число мегабайт.

Так сделано не для красоты. У большинства связь мобильная в роуминге или
спутниковая с лимитом; скачать прогноз, не зная заранее его веса, — это
как заправляться, не глядя на счётчик.
***********************************************************************/
#ifndef DOWNLOADSHEET_H
#define DOWNLOADSHEET_H

#include <functional>

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class SourceRow;
class Wheel;

class DownloadSheet : public QWidget
{ Q_OBJECT
	public:
		// Один источник в списке. Вес считает тот, кто знает, как:
		// у немецких плиток он точный, из описи, у NOAA — оценка.
		struct Source
		{
			QString id;        // noaa, eu-wind, world-wave...
			QString title;     // «Ветер ICON-EU, 7 км»
			QString about;     // «ветер, порывы, давление»
			QString run;       // «выпуск 6 ч назад» или пусто
			bool    here;      // есть ли в этом районе
			bool    exact;     // вес точный или прикинутый
			bool    on;        // взят
		};

		explicit DownloadSheet (QWidget *parent = nullptr);

		void  setSources (const QList<Source> &src);
		// Кто считает вес: по опознавателю источника и числу суток.
		void  setWeigher (std::function<qint64(const QString &, int)> f);
		QStringList chosen () const;
		int   days () const;
		int   hourStep () const;
		void  setDepth (int days, int hourStep);

	signals:
		void  go ();
		void  dropped ();      // передумал

	private slots:
		void  recount ();

	private:
		static QString mb (qint64 bytes);

		QVBoxLayout *rows;
		QList<SourceRow *> items;
		Wheel       *deep;
		Wheel       *every;
		QPushButton *start;
		QPushButton *cancel;
		QLabel      *hint;
		std::function<qint64(const QString &, int)> weigher;
};

#endif
