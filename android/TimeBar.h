/**********************************************************************
MAKGrib для Android — шкала времени прогноза.

Показывает, где мы сейчас среди сроков: слева начало прогноза, справа
конец, засечки по суткам. Пальцем можно вести прямо по шкале — это
быстрее, чем щёлкать стрелками через каждые три часа.
***********************************************************************/
#ifndef TIMEBAR_H
#define TIMEBAR_H

#include <QDateTime>
#include <QList>
#include <QWidget>

class TimeBar : public QWidget
{ Q_OBJECT
	public:
		explicit TimeBar (QWidget *parent = nullptr);

		void setSteps (const QList<QDateTime> &steps);
		void setZone (int secondsFromUtc);
		void setIndex (int i);
		int  index () const   { return cur; }
		int  count () const   { return steps.size(); }

	signals:
		void moved (int index);

	protected:
		void paintEvent (QPaintEvent *) override;
		void mousePressEvent (QMouseEvent *) override;
		void mouseMoveEvent (QMouseEvent *) override;

	private:
		double xOf (int i) const;
		int    indexAt (double x) const;

		QList<QDateTime> steps;
		int  cur;
		int  zone;               // сдвиг подписей от UTC, в секундах
};

#endif
