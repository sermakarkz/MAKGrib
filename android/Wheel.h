/**********************************************************************
MAKGrib для Android — колесо выбора числа.

Кнопки со стрелками пришлось бы делать в палец величиной, и они съедали
пол-экрана. Колесо занимает столько же места при любом числе значений:
крутишь пальцем, оно докатывается и встаёт на ближайшее деление.
***********************************************************************/
#ifndef WHEEL_H
#define WHEEL_H

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class Wheel : public QWidget
{ Q_OBJECT
	public:
		Wheel (const QString &title, int lo, int hi, int start,
		       const QString &suffix, QWidget *parent = nullptr);

		int  value () const  { return val; }

	signals:
		void valueChanged (int v);

	protected:
		void paintEvent (QPaintEvent *) override;
		void mousePressEvent (QMouseEvent *) override;
		void mouseMoveEvent (QMouseEvent *) override;
		void mouseReleaseEvent (QMouseEvent *) override;

	private slots:
		void animate ();

	private:
		void normalize ();       // перевести накопленный сдвиг в деления

		QString  title, suffix;
		int      lo, hi, val;
		double   offset;         // сдвиг в точках от текущего деления
		double   velocity;       // точек в секунду
		bool     dragging;
		double   lastY;
		QElapsedTimer sinceMove;
		QTimer   timer;
};

#endif
