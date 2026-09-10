/**********************************************************************
MAKGrib для Android — выбор слоя карты.
***********************************************************************/
#include "LayerBar.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "DataDefines.h"

//---------------------------------------------------------------------
LayerBar::LayerBar (QWidget *parent) : QWidget (parent), cur (0), cell (74)
{
	setFixedWidth (WIDTH);
}

//---------------------------------------------------------------------
void LayerBar::setItems (const QList<Item> &items)
{
	list = items;
	if (cur >= list.size())
		cur = 0;
	refit ();
}

//---------------------------------------------------------------------
// Ячейку подгоняем под экран: слоёв бывает и десяток, а на невысоком
// телефоне последний иначе уезжает за край. Считать приходится не раз
// при создании: при первом показе карта ещё не той высоты, что потом.
void LayerBar::refit ()
{
	cell = 74;
	if (parentWidget() != nullptr && !list.isEmpty()) {
		int room = (parentWidget()->height() - 24) / list.size();
		if (room < cell)
			cell = room;
		if (cell < 42)
			cell = 42;
	}
	setFixedHeight (cell * list.size() + 12);
	update ();
}

//---------------------------------------------------------------------
void LayerBar::setCurrent (int i)
{
	if (i < 0 || i >= list.size())
		return;
	cur = i;
	update ();
}

//---------------------------------------------------------------------
void LayerBar::paintEvent (QPaintEvent *)
{
	QPainter p (this);
	p.setRenderHint (QPainter::Antialiasing, true);
	p.setPen (Qt::NoPen);
	p.setBrush (QColor (20, 40, 60, 215));
	p.drawRoundedRect (QRectF (-14, 0, width()+14, height()), 16, 16);

	int icon = int (cell * 0.46);
	int fs   = int (cell * 0.19);
	if (fs < 9)  fs = 9;
	if (fs > 12) fs = 12;
	QFont f = font ();  f.setPixelSize (fs);
	for (int i = 0; i < list.size(); ++i) {
		QRectF box (0, 6 + i*cell, width(), cell);
		bool here = (i == cur);
		if (here) {
			p.setPen (Qt::NoPen);
			p.setBrush (QColor (0x2d, 0x6e, 0xa8));
			p.drawRoundedRect (box.adjusted (6, 2, -6, -2), 12, 12);
		}
		QColor ink = here ? Qt::white : QColor (0xc8, 0xd6, 0xe4);
		drawIcon (p, list.at(i).type,
		          QRectF (box.center().x() - icon/2.0, box.top() + 5,
		                  icon, icon), ink);
		p.setPen (ink);
		p.setFont (f);
		p.drawText (QRectF (0, box.top() + cell - fs - 6, width(), fs + 5),
		            Qt::AlignCenter, list.at(i).name);
	}
}

//---------------------------------------------------------------------
void LayerBar::mouseReleaseEvent (QMouseEvent *e)
{
	int i = int ((e->position().y() - 6) / cell);
	if (i >= 0 && i < list.size()) {
		cur = i;
		update ();
		emit chosen (i);
	}
}

//---------------------------------------------------------------------
// Значки. Все рисуются в квадрате box, одним цветом: так они читаются и
// на подсвеченной ячейке, и на тёмной.
void LayerBar::drawIcon (QPainter &p, int type, const QRectF &b,
                         const QColor &ink)
{
	double w = b.width(), x = b.left(), y = b.top();
	QPen pen (ink, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.setPen (pen);
	p.setBrush (Qt::NoBrush);

	switch (type) {

	case GRB_PRV_WIND_XY2D: {          // стрелка
		p.drawLine (QPointF (x+4, y+w*0.5), QPointF (x+w-5, y+w*0.5));
		p.drawLine (QPointF (x+w-12, y+w*0.3), QPointF (x+w-5, y+w*0.5));
		p.drawLine (QPointF (x+w-12, y+w*0.7), QPointF (x+w-5, y+w*0.5));
		break;
	}
	case GRB_WIND_GUST: {              // стрелка с росчерками — порыв
		p.drawLine (QPointF (x+8, y+w*0.5), QPointF (x+w-5, y+w*0.5));
		p.drawLine (QPointF (x+w-12, y+w*0.3), QPointF (x+w-5, y+w*0.5));
		p.drawLine (QPointF (x+w-12, y+w*0.7), QPointF (x+w-5, y+w*0.5));
		QPen thin (ink, 1.8, Qt::SolidLine, Qt::RoundCap);
		p.setPen (thin);
		p.drawLine (QPointF (x+2, y+w*0.28), QPointF (x+w*0.45, y+w*0.28));
		p.drawLine (QPointF (x+2, y+w*0.72), QPointF (x+w*0.45, y+w*0.72));
		break;
	}
	case GRB_TEMP: {                   // термометр
		p.drawLine (QPointF (x+w*0.5, y+5), QPointF (x+w*0.5, y+w*0.62));
		p.setBrush (ink);
		p.drawEllipse (QPointF (x+w*0.5, y+w*0.76), 6.5, 6.5);
		p.setBrush (Qt::NoBrush);
		QPen thin (ink, 1.6);
		p.setPen (thin);
		p.drawLine (QPointF (x+w*0.62, y+10), QPointF (x+w*0.76, y+10));
		p.drawLine (QPointF (x+w*0.62, y+17), QPointF (x+w*0.76, y+17));
		break;
	}
	case GRB_HUMID_REL: {              // капля
		QPainterPath drop;
		drop.moveTo (x+w*0.5, y+3);
		drop.cubicTo (x+w*0.92, y+w*0.5, x+w*0.86, y+w-3, x+w*0.5, y+w-3);
		drop.cubicTo (x+w*0.14, y+w-3, x+w*0.08, y+w*0.5, x+w*0.5, y+3);
		p.drawPath (drop);
		break;
	}
	case GRB_CLOUD_TOT: {              // облако
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawEllipse (QPointF (x+w*0.34, y+w*0.52), 8.5, 8.5);
		p.drawEllipse (QPointF (x+w*0.58, y+w*0.44), 10.5, 10.5);
		p.drawRoundedRect (QRectF (x+3, y+w*0.52, w-8, w*0.24), 6, 6);
		break;
	}
	case GRB_PRECIP_TOT: {             // облако с дождём
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawEllipse (QPointF (x+w*0.36, y+w*0.36), 7.5, 7.5);
		p.drawEllipse (QPointF (x+w*0.60, y+w*0.30), 9.0, 9.0);
		p.drawRoundedRect (QRectF (x+4, y+w*0.36, w-10, w*0.2), 5, 5);
		p.setBrush (Qt::NoBrush);
		QPen thin (ink, 2.2, Qt::SolidLine, Qt::RoundCap);
		p.setPen (thin);
		for (int k = 0; k < 3; ++k) {
			double dx = x + w*(0.28 + 0.2*k);
			p.drawLine (QPointF (dx, y+w*0.66), QPointF (dx-3, y+w*0.88));
		}
		break;
	}
	case GRB_CAPE: {                   // молния
		QPolygonF bolt;
		bolt << QPointF (x+w*0.60, y+3)  << QPointF (x+w*0.30, y+w*0.55)
		     << QPointF (x+w*0.50, y+w*0.55) << QPointF (x+w*0.40, y+w-3)
		     << QPointF (x+w*0.74, y+w*0.42) << QPointF (x+w*0.52, y+w*0.42);
		p.setBrush (ink);
		p.setPen (Qt::NoPen);
		p.drawPolygon (bolt);
		break;
	}
	case GRB_WTMP: {                   // термометр над волной
		p.drawLine (QPointF (x+w*0.42, y+4), QPointF (x+w*0.42, y+w*0.45));
		p.setBrush (ink);
		p.drawEllipse (QPointF (x+w*0.42, y+w*0.56), 5.5, 5.5);
		p.setBrush (Qt::NoBrush);
		QPainterPath wave;
		wave.moveTo (x+2, y+w-5);
		wave.cubicTo (x+w*0.3, y+w-12, x+w*0.55, y+w+2, x+w-2, y+w-7);
		p.drawPath (wave);
		break;
	}
	case GRB_PRV_DIFF_TEMPDEW: {       // туман: полосы над волной
		QPen thin (ink, 2.2, Qt::SolidLine, Qt::RoundCap);
		p.setPen (thin);
		for (int k = 0; k < 3; ++k) {
			double yy = y + w*(0.26 + 0.18*k);
			p.drawLine (QPointF (x+3 + (k%2)*5, yy),
			            QPointF (x+w-4 - (k%2)*6, yy));
		}
		p.setPen (pen);
		QPainterPath wave;
		wave.moveTo (x+2, y+w-4);
		wave.cubicTo (x+w*0.3, y+w-11, x+w*0.55, y+w+3, x+w-2, y+w-6);
		p.drawPath (wave);
		break;
	}
	case GRB_PRV_WAV_SIG: {            // две волны
		for (int k = 0; k < 2; ++k) {
			QPainterPath wave;
			double yy = y + w*(0.38 + 0.3*k);
			wave.moveTo (x+2, yy);
			wave.cubicTo (x+w*0.3, yy-8, x+w*0.55, yy+8, x+w-2, yy-2);
			p.drawPath (wave);
		}
		break;
	}
	default: {                         // на всякий случай — точка
		p.setBrush (ink);
		p.drawEllipse (b.center(), 5, 5);
		break;
	}
	}
}
