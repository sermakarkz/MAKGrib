/**********************************************************************
MAKGrib для Android — кнопка со значком.
***********************************************************************/
#include "IconButton.h"

#include <cmath>

#include <QPainter>
#include <QPainterPath>

//---------------------------------------------------------------------
IconButton::IconButton (Kind k, bool a, QWidget *parent)
	: QPushButton (parent), kind (k), accent (a)
{
	setMinimumHeight (61);
	setFlat (true);
}

//---------------------------------------------------------------------
void IconButton::setKind (Kind k)
{
	kind = k;
	update ();
}

//---------------------------------------------------------------------
void IconButton::setAccent (bool on)
{
	accent = on;
	update ();
}

//---------------------------------------------------------------------
void IconButton::setRound (bool on)
{
	round = on;
	update ();
}

//---------------------------------------------------------------------
void IconButton::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.setRenderHint (QPainter::Antialiasing, true);

	QColor back = accent ? QColor (0x2d, 0x6e, 0xa8)
	                     : QColor (0xe4, 0xea, 0xf0);
	QColor ink  = accent ? Qt::white : QColor (0x1a, 0x2a, 0x3a);
	if (isDown())
		back = back.darker (112);
	if (!isEnabled())
		ink.setAlpha (70);
	if (round) {
		// Подложка светлее и меньше самой кнопки: тёмный круг во всю
		// кнопку закрывал карту и лез в глаза сильнее, чем нужно.
		back = QColor (20, 40, 60, isDown() ? 170 : 115);
		ink  = Qt::white;
		p.setPen (Qt::NoPen);
		p.setBrush (back);
		p.drawEllipse (rect().center(), width()/2 - 4, height()/2 - 4);
	}
	else
		p.fillRect (rect(), back);

	double s = qMin (width(), height()) * 0.46;
	QPointF c = QRectF (rect()).center ();
	double x = c.x() - s/2, y = c.y() - s/2;
	QPen pen (ink, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen (pen);
	p.setBrush (Qt::NoBrush);

	switch (kind) {

	case Back:
	case Forward: {
		double dir = (kind == Back) ? -1.0 : 1.0;
		p.drawLine (QPointF (c.x() - dir*s*0.42, c.y()),
		            QPointF (c.x() + dir*s*0.42, c.y()));
		p.drawLine (QPointF (c.x() + dir*s*0.12, c.y() - s*0.28),
		            QPointF (c.x() + dir*s*0.42, c.y()));
		p.drawLine (QPointF (c.x() + dir*s*0.12, c.y() + s*0.28),
		            QPointF (c.x() + dir*s*0.42, c.y()));
		break;
	}

	case Gear: {
		// Толстые короткие зубья по кольцу: тонкие лучи из середины
		// читаются как солнце, а не как шестерёнка.
		QPen tooth (ink, s*0.16, Qt::SolidLine, Qt::FlatCap);
		p.setPen (tooth);
		for (int k = 0; k < 8; ++k) {
			double a = k * M_PI/4.0;
			p.drawLine (QPointF (c.x() + std::cos(a)*s*0.30,
			                     c.y() + std::sin(a)*s*0.30),
			            QPointF (c.x() + std::cos(a)*s*0.44,
			                     c.y() + std::sin(a)*s*0.44));
		}
		p.setPen (QPen (ink, s*0.17));
		p.drawEllipse (c, s*0.24, s*0.24);
		break;
	}

	case Route: {
		// Две точки, соединённые ломаной, — так маршрут и выглядит.
		QPointF a (x + s*0.12, y + s*0.82);
		QPointF b (x + s*0.50, y + s*0.28);
		QPointF d (x + s*0.88, y + s*0.62);
		p.drawLine (a, b);
		p.drawLine (b, d);
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawEllipse (a, s*0.13, s*0.13);
		p.drawEllipse (d, s*0.13, s*0.13);
		break;
	}

	case Download: {
		p.drawLine (QPointF (c.x(), y + s*0.08),
		            QPointF (c.x(), y + s*0.62));
		p.drawLine (QPointF (c.x() - s*0.22, y + s*0.38),
		            QPointF (c.x(), y + s*0.62));
		p.drawLine (QPointF (c.x() + s*0.22, y + s*0.38),
		            QPointF (c.x(), y + s*0.62));
		p.drawLine (QPointF (x + s*0.10, y + s*0.88),
		            QPointF (x + s*0.90, y + s*0.88));
		break;
	}

	case Info: {
		p.drawEllipse (c, s*0.44, s*0.44);
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawEllipse (QPointF (c.x(), y + s*0.24), 1.9, 1.9);
		p.setPen (pen);
		p.drawLine (QPointF (c.x(), y + s*0.44),
		            QPointF (c.x(), y + s*0.76));
		break;
	}

	case Locate: {
		// Перекрестье с точкой: так «моё место» рисуют везде, и в
		// сантиметре оно читается.
		p.drawEllipse (c, s*0.30, s*0.30);
		p.drawLine (QPointF (c.x(), c.y()-s*0.50), QPointF (c.x(), c.y()-s*0.34));
		p.drawLine (QPointF (c.x(), c.y()+s*0.34), QPointF (c.x(), c.y()+s*0.50));
		p.drawLine (QPointF (c.x()-s*0.50, c.y()), QPointF (c.x()-s*0.34, c.y()));
		p.drawLine (QPointF (c.x()+s*0.34, c.y()), QPointF (c.x()+s*0.50, c.y()));
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawEllipse (c, s*0.10, s*0.10);
		break;
	}

	case Done: {
		p.drawLine (QPointF (x + s*0.12, y + s*0.52),
		            QPointF (x + s*0.42, y + s*0.80));
		p.drawLine (QPointF (x + s*0.42, y + s*0.80),
		            QPointF (x + s*0.90, y + s*0.20));
		break;
	}
	}
}
