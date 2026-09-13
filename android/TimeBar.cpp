/**********************************************************************
MAKGrib для Android — шкала времени прогноза.
***********************************************************************/
#include "TimeBar.h"

#include <QMouseEvent>
#include <QPainter>

// Поля по краям: под палец у самого края экрана попасть трудно, да и
// бегунок не должен выходить за границы шкалы.
static const int SIDE  = 26;
static const int KNOB  = 15;

//---------------------------------------------------------------------
TimeBar::TimeBar (QWidget *parent) : QWidget (parent), cur (0), zone (0)
{
	setFixedHeight (62);
}

//---------------------------------------------------------------------
void TimeBar::setSteps (const QList<QDateTime> &s)
{
	steps = s;
	if (cur >= steps.size())
		cur = steps.isEmpty() ? 0 : steps.size() - 1;
	update ();
}

//---------------------------------------------------------------------
void TimeBar::setZone (int secondsFromUtc)
{
	zone = secondsFromUtc;
	update ();
}

//---------------------------------------------------------------------
void TimeBar::setIndex (int i)
{
	if (i < 0 || i >= steps.size() || i == cur)
		return;
	cur = i;
	update ();
}

//---------------------------------------------------------------------
double TimeBar::xOf (int i) const
{
	if (steps.size() < 2)
		return SIDE;
	return SIDE + (width() - 2.0*SIDE) * i / (steps.size() - 1);
}

//---------------------------------------------------------------------
int TimeBar::indexAt (double x) const
{
	if (steps.size() < 2)
		return 0;
	double f = (x - SIDE) / (width() - 2.0*SIDE);
	int i = int (f * (steps.size() - 1) + 0.5);
	if (i < 0) i = 0;
	if (i >= steps.size()) i = steps.size() - 1;
	return i;
}

//---------------------------------------------------------------------
void TimeBar::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.setRenderHint (QPainter::Antialiasing, true);
	p.fillRect (rect(), QColor (0xf2, 0xf5, 0xf8));

	if (steps.isEmpty()) {
		p.setPen (QColor (0x88, 0x99, 0xaa));
		QFont f = font ();  f.setPixelSize (15);  p.setFont (f);
		p.drawText (rect(), Qt::AlignCenter,
		            tr("прогноз не загружен"));
		return;
	}

	double y = 22;
	// Дорожка целиком и пройденная часть — по ней сразу видно, много ли
	// прогноза осталось впереди.
	p.setPen (Qt::NoPen);
	p.setBrush (QColor (0xd2, 0xdf, 0xec));
	p.drawRoundedRect (QRectF (SIDE, y-4, width()-2*SIDE, 8), 4, 4);
	p.setBrush (QColor (0x2d, 0x6e, 0xa8));
	p.drawRoundedRect (QRectF (SIDE, y-4, xOf(cur)-SIDE, 8), 4, 4);

	// Засечки по началу суток, с подписью числа.
	QFont f = font ();  f.setPixelSize (13);  p.setFont (f);
	double lastLabel = -1000;
	for (int i = 1; i < steps.size(); ++i) {
		// Подписываем только начала суток. Первый срок — не начало дня,
		// а просто край прогноза, и его подпись только путала бы.
		QDateTime d = steps.at(i).toOffsetFromUtc (zone);
		if (steps.at(i-1).toOffsetFromUtc(zone).date() == d.date())
			continue;
		double x = xOf (i);
		// Первые сутки прогноза часто начинаются под вечер, и подпись
		// нового дня встаёт вплотную к началу шкалы — такие пропускаем.
		if (x - lastLabel < 66)
			continue;
		lastLabel = x;
		p.setPen (QColor (0xa8, 0xb6, 0xc4));
		p.drawLine (QPointF (x, y+8), QPointF (x, y+14));
		p.setPen (QColor (0x55, 0x66, 0x77));
		p.drawText (QRectF (x-30, y+16, 60, 18), Qt::AlignCenter,
		            d.toString ("dd.MM"));
	}

	// Бегунок.
	double x = xOf (cur);
	p.setPen (QPen (QColor (0x2d, 0x6e, 0xa8), 3));
	p.setBrush (Qt::white);
	p.drawEllipse (QPointF (x, y), KNOB, KNOB);
}

//---------------------------------------------------------------------
void TimeBar::mousePressEvent (QMouseEvent *e)
{
	int i = indexAt (e->position().x());
	if (i != cur) {
		cur = i;
		update ();
		emit moved (cur);
	}
}

//---------------------------------------------------------------------
void TimeBar::mouseMoveEvent (QMouseEvent *e)
{
	mousePressEvent (e);
}
