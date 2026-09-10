/**********************************************************************
MAKGrib для Android — колесо выбора числа.
***********************************************************************/
#include "Wheel.h"

#include <cmath>

#include <QMouseEvent>
#include <QPainter>

// Высота одного деления: под палец и так, чтобы в окошко попадали
// соседние значения — по ним видно, что колесо крутится.
static const int ITEM    = 46;
static const int VISIBLE = 3;
static const int CAPTION = 22;
static const double STOP_SPEED = 40.0;

//---------------------------------------------------------------------
Wheel::Wheel (const QString &title_, int lo_, int hi_, int start,
              const QString &suffix_, QWidget *parent)
	: QWidget (parent), title (title_), suffix (suffix_),
	  lo (lo_), hi (hi_), val (start), offset (0), velocity (0),
	  dragging (false), lastY (0)
{
	setFixedHeight (CAPTION + VISIBLE*ITEM);
	// Втроём в строку: на узком экране каждому достаётся около ста
	// точек, и запас в 140 не давал бы им встать рядом.
	setMinimumWidth (100);
	timer.setInterval (16);
	connect (&timer, &QTimer::timeout, this, &Wheel::animate);
}

//---------------------------------------------------------------------
void Wheel::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.setRenderHint (QPainter::Antialiasing, true);
	p.setRenderHint (QPainter::TextAntialiasing, true);

	QRectF box (0, CAPTION, width(), VISIBLE*ITEM);
	p.setPen (Qt::NoPen);
	p.setBrush (QColor (0xe9, 0xee, 0xf4));
	p.drawRoundedRect (box, 14, 14);

	// Подпись — над окошком, мелко и незаметно: главное здесь число.
	p.setPen (QColor (0x55, 0x66, 0x77));
	QFont f = font ();  f.setPixelSize (14);
	p.setFont (f);
	p.drawText (QRectF (4, 0, width()-8, CAPTION),
	            Qt::AlignLeft | Qt::AlignVCenter, title);

	// Окошко выбранного значения.
	double cy = box.center().y ();
	QRectF slot (4, cy - ITEM/2.0, width()-8, ITEM);
	p.setBrush (QColor (0xd2, 0xdf, 0xec));
	p.drawRoundedRect (slot, 10, 10);

	p.setClipRect (box);
	for (int k = lo; k <= hi; ++k) {
		double y = cy + (k - val)*ITEM + offset;
		double d = std::fabs (y - cy) / ITEM;
		if (d > VISIBLE/2.0 + 0.6)
			continue;
		// Чем дальше от окошка, тем бледнее: получается ощущение
		// барабана, уходящего за край.
		int alpha = int (255 * std::max (0.0, 1.0 - d*0.75));
		bool here = d < 0.5;
		f.setPixelSize (here ? 26 : 20);
		f.setBold (here);
		p.setFont (f);
		p.setPen (QColor (0x1a, 0x2a, 0x3a, here ? 255 : alpha));
		QString s = fmt ? fmt (k)
		                : (here ? QStringLiteral("%1 %2").arg (k).arg (suffix)
		                        : QString::number (k));
		p.drawText (QRectF (0, y - ITEM/2.0, width(), ITEM),
		            Qt::AlignCenter, s);
	}
}

//---------------------------------------------------------------------
void Wheel::setFormatter (std::function<QString(int)> f)
{
	fmt = f;
	update ();
}

//---------------------------------------------------------------------
void Wheel::mousePressEvent (QMouseEvent *e)
{
	timer.stop ();
	dragging = true;
	velocity = 0;
	lastY    = e->position().y ();
	sinceMove.restart ();
}

//---------------------------------------------------------------------
void Wheel::mouseMoveEvent (QMouseEvent *e)
{
	if (!dragging)
		return;
	double y  = e->position().y ();
	double dy = y - lastY;
	lastY = y;
	offset += dy;
	qint64 ms = sinceMove.restart ();
	if (ms > 0) {
		double v = dy*1000.0/ms;
		velocity = velocity*0.6 + v*0.4;
	}
	normalize ();
	update ();
}

//---------------------------------------------------------------------
void Wheel::mouseReleaseEvent (QMouseEvent *)
{
	dragging = false;
	// Палец стоял перед отрывом — не бросаем, просто доводим до деления.
	if (sinceMove.elapsed() > 100)
		velocity = 0;
	timer.start ();
}

//---------------------------------------------------------------------
void Wheel::normalize ()
{
	int was = val;
	while (offset > ITEM/2.0) {
		if (val <= lo)  { offset = ITEM*0.3; velocity = 0; break; }
		--val;  offset -= ITEM;
	}
	while (offset < -ITEM/2.0) {
		if (val >= hi)  { offset = -ITEM*0.3; velocity = 0; break; }
		++val;  offset += ITEM;
	}
	if (val != was)
		emit valueChanged (val);
}

//---------------------------------------------------------------------
void Wheel::animate ()
{
	const double dt = 0.016;
	if (std::fabs (velocity) > STOP_SPEED) {
		offset += velocity*dt;
		velocity *= std::pow (0.5, (dt*1000.0)/170.0);
		normalize ();
	}
	else {
		// Докатились — мягко сажаем на деление.
		velocity = 0;
		offset  *= 0.55;
		if (std::fabs (offset) < 0.5) {
			offset = 0;
			timer.stop ();
		}
	}
	update ();
}
