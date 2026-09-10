/**********************************************************************
MAKGrib для Android — цветовая шкала у правого края.
***********************************************************************/
#include "ScaleBar.h"

#include <cmath>

#include <QPainter>

#include "DataDefines.h"
#include "Util.h"

// Ширина цветной полосы; остальное занимают подписи.
static const int STRIP = 14;

//---------------------------------------------------------------------
ScaleBar::ScaleBar (QWidget *parent)
	: QWidget (parent), type (0), lo (0), hi (0)
{
	setFixedWidth (WIDTH);
	setAttribute (Qt::WA_TransparentForMouseEvents);
}

//---------------------------------------------------------------------
void ScaleBar::setLayer (int dataType, double lo_, double hi_,
                         std::function<QColor(double)> c)
{
	type  = dataType;
	lo    = lo_;
	hi    = hi_;
	color = c;
	update ();
}

//---------------------------------------------------------------------
// Внутри всё в единицах GRIB — кельвины, метры в секунду. Здесь перевод
// в те единицы, что стоят в настройках.
double ScaleBar::toShow (double raw) const
{
	switch (type) {

	case GRB_TEMP:
	case GRB_WTMP: {
		QString u = Util::getSetting ("unitsTemp",
		                              QStringLiteral("°C")).toString();
		if (u == QStringLiteral("°F"))
			return (raw - 273.15) * 9.0/5.0 + 32.0;
		if (u == "K")
			return raw;
		return raw - 273.15;
	}

	case GRB_PRV_DIFF_TEMPDEW: {
		// Разность: в кельвинах и градусах Цельсия она одна и та же.
		QString u = Util::getSetting ("unitsTemp",
		                              QStringLiteral("°C")).toString();
		return (u == QStringLiteral("°F")) ? raw * 9.0/5.0 : raw;
	}

	case GRB_PRV_WIND_XY2D:
	case GRB_WIND_SPEED:
	case GRB_WIND_GUST: {
		QString u = Util::getSetting ("unitsWindSpeed", "km/h").toString();
		if (u == "m/s")    return raw;
		if (u == "m/min")  return raw * 60.0;
		if (u == "km/h")   return raw * 3.6;
		return raw * 3.6 / 1.852;          // узлы
	}

	default:
		return raw;
	}
}

//---------------------------------------------------------------------
// Шаг засечек подбираем не по числу подписей, а по расстоянию между ними
// на экране: цифр нужно как можно больше, но так, чтобы не слипались.
// Двадцать пять точек — это примерно две высоты строки.
double ScaleBar::tickStep (double pixels, double span) const
{
	static const double nice[] = {0.2, 0.5, 1, 2, 2.5, 5, 10, 20, 25, 50,
	                              100, 200, 250, 500, 1000, 2000};
	if (span <= 0 || pixels <= 0)
		return span;
	for (double s : nice) {
		if (pixels * s / span >= 25.0)
			return s;
	}
	return span/4.0;
}

//---------------------------------------------------------------------
void ScaleBar::paintEvent (QPaintEvent *)
{
	if (type == 0 || hi <= lo || !color)
		return;
	QPainter p (this);
	p.setRenderHint (QPainter::Antialiasing, true);
	p.setPen (Qt::NoPen);
	p.setBrush (QColor (20, 40, 60, 215));
	p.drawRoundedRect (QRectF (0, 0, width()+14, height()), 14, 14);

	QFont f = font ();
	f.setPixelSize (11);
	p.setFont (f);

	// Единица измерения — сверху: она одна на всю шкалу.
	p.setPen (QColor (0xc8, 0xd6, 0xe4));
	p.drawText (QRectF (0, 3, width(), 15), Qt::AlignCenter,
	            Util::getDataUnit (type));

	double top = 22, bot = height() - 8;
	QRectF strip (6, top, STRIP, bot - top);

	// Полосу рисуем построчно: палитра нелинейная, промежуточные цвета
	// берём у неё же, а не досочиняем градиентом.
	for (int y = 0; y < int (strip.height()); ++y) {
		double v = hi - (hi - lo) * y / strip.height();
		p.setPen (color (v));
		p.drawLine (QPointF (strip.left(), strip.top()+y),
		            QPointF (strip.right(), strip.top()+y));
	}
	p.setPen (QColor (0x88, 0x99, 0xaa));
	p.setBrush (Qt::NoBrush);
	p.drawRect (strip);

	// Засечки считаем в единицах показа: круглый шаг в кельвинах даёт на
	// экране «−3,3» и «6,9», и шкалой становится невозможно пользоваться.
	double slo = toShow (lo), shi = toShow (hi);
	if (shi <= slo)
		return;
	double stepv = tickStep (strip.height(), shi - slo);
	// Знаков после запятой ровно столько, сколько требует шаг: иначе при
	// дробном шаге соседние подписи округляются в одно число, и шкала
	// показывает одну цифру дважды подряд.
	int dec = (std::fabs (stepv - std::floor (stepv + 0.5)) > 1e-9
	           || stepv < 1.0) ? 1 : 0;
	double first = std::ceil (slo/stepv) * stepv;

	// Мелкие штрихи между подписями: по ним глаз делит промежуток, не
	// читая каждую цифру.
	for (double v = first - stepv/2; v <= shi + 1e-6; v += stepv) {
		if (v < slo)
			continue;
		double y = strip.bottom() - strip.height() * (v - slo) / (shi - slo);
		p.setPen (QColor (0x88, 0x99, 0xaa));
		p.drawLine (QPointF (strip.right(), y), QPointF (strip.right()+2, y));
	}
	for (double v = first; v <= shi + 1e-6; v += stepv) {
		double y = strip.bottom() - strip.height() * (v - slo) / (shi - slo);
		if (y < strip.top() - 1 || y > strip.bottom() + 1)
			continue;
		p.setPen (QColor (0xc8, 0xd6, 0xe4));
		p.drawLine (QPointF (strip.right(), y), QPointF (strip.right()+4, y));
		p.drawText (QRectF (strip.right()+5, y-8, width()-strip.right()-6, 16),
		            Qt::AlignLeft | Qt::AlignVCenter,
		            QString::number (v, 'f', dec));
	}
}
