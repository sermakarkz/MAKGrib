/**********************************************************************
MAKGrib для Android — карта под палец.
***********************************************************************/
#include "MapView.h"

#include <cmath>

#include <QGestureEvent>
#include <QPainter>
#include <QPinchGesture>
#include <QElapsedTimer>
#include <QTouchEvent>

#include "GribPlot.h"
#include "GribReader.h"
#include "zuFile.h"
#include "GshhsReader.h"
#include "MapDrawer.h"
#include "Projection.h"
#include "Util.h"

// Ниже этого карта считается стоящей: докатывание прекращаем.
static const double STOP_SPEED = 25.0;      // пикселей в секунду
// За сколько докатывание гаснет вдвое.
static const double GLIDE_HALFLIFE_MS = 180.0;

//---------------------------------------------------------------------
MapView::MapView (QWidget *parent)
	: QWidget (parent), drawer (nullptr), proj (nullptr), plot (nullptr),
	  bufferValid (false), shift (0, 0), liveZoom (1.0),
	  dragging (false), pinching (false), pinchStart (1)
{
	settle.setSingleShot (true);
	settle.setInterval (120);
	connect (&settle, &QTimer::timeout, this, &MapView::settleNow);
	setAttribute (Qt::WA_AcceptTouchEvents);
	grabGesture (Qt::PinchGesture);
	setAutoFillBackground (false);

	// Каспий как начальный вид: там, где ходят.
	proj = new Projection_MERCATOR_Simple (width()>0?width():1080,
	                                       height()>0?height():1800,
	                                       50.5, 42.0, 40.0);
	glideTimer.setInterval (16);            // примерно 60 кадров в секунду
	connect (&glideTimer, &QTimer::timeout, this, &MapView::glide);
}

//---------------------------------------------------------------------
MapView::~MapView ()
{
	delete plot;
	delete drawer;
	delete proj;
}

//---------------------------------------------------------------------
void MapView::loadMaps ()
{
	if (drawer != nullptr)
		return;
	// Качество 1: не самая мелкая сетка, но на телефоне разницы не видно,
	// а памяти и времени уходит заметно меньше.
	gshhs  = std::make_shared<GshhsReader> (Util::pathGshhs(), 1);
	drawer = new MapDrawer (gshhs);
	bufferValid = false;
	update ();
}

//---------------------------------------------------------------------
bool MapView::setForecast (const QString &path)
{
	delete plot;
	plot = nullptr;
	if (!path.isEmpty()) {
		// Ридеру надо заранее сказать, сколько в файле записей: он
		// распределяет под них место одним куском. Ноль означает, что
		// читать нечего, и файл молча не открывается.
		int nbrecs = 0;
		ZUFILE *zf = zu_open (qPrintable(path), "rb", ZU_COMPRESS_AUTO);
		if (zf != nullptr) {
			GribReader r;
			nbrecs = r.countGribRecords (zf);
			zu_close (zf);
		}
		GribPlot *p = new GribPlot ();
		p->loadFile (path, nullptr, nbrecs);
		if (p->isReaderOk())
			plot = p;
		else
			delete p;
	}
	bufferValid = false;
	update ();
	return plot != nullptr;
}

//---------------------------------------------------------------------
void MapView::setCenter (double lon, double lat)
{
	// Своего «поставить центр» у Projection нет — есть общее «положить
	// точку карты в точку экрана»; центр это её частный случай.
	proj->setMapPointInScreen (lon, lat, width()/2, height()/2);
	bufferValid = false;
	announce ();
	update ();
}

//---------------------------------------------------------------------
void MapView::zoomBy (double factor)
{
	proj->setScale (proj->getScale() * factor);
	bufferValid = false;
	announce ();
	update ();
}

//---------------------------------------------------------------------
void MapView::announce ()
{
	double lon, lat;
	proj->screen2map (width()/2, height()/2, &lon, &lat);
	emit viewChanged (lon, lat, proj->getScale());
}

//---------------------------------------------------------------------
void MapView::resizeEvent (QResizeEvent *)
{
	proj->setScreenSize (width(), height());
	bufferValid = false;
	announce ();
}

//---------------------------------------------------------------------
void MapView::rebuild ()
{
	if (drawer == nullptr)
		return;
	QElapsedTimer t; t.start ();
	buffer = QPixmap (size());
	QPainter p (&buffer);
	// Без прогноза — только берег и сетка координат: рисовалка данных
	// не проверяет, что данных нет, и падает на пустом указателе.
	if (plot != nullptr)
		drawer->draw_GSHHS_and_GriddedData (p, true, false, proj, plot, false);
	else
		drawer->draw_GSHHS (p, true, false, proj);
	bufferValid = true;
	if (t.elapsed() > 200)
		qWarning ("перерисовка карты заняла %lld мс", (long long) t.elapsed());
}

//---------------------------------------------------------------------
void MapView::settleNow ()
{
	shift    = QPointF (0, 0);
	liveZoom = 1.0;
	bufferValid = false;
	announce ();
	update ();
}

//---------------------------------------------------------------------
void MapView::paintEvent (QPaintEvent *)
{
	QPainter pnt (this);
	if (drawer == nullptr) {
		pnt.fillRect (rect(), QColor(30,60,90));
		pnt.setPen (Qt::white);
		pnt.drawText (rect(), Qt::AlignCenter,
		              QStringLiteral("Карты ещё раскладываются…"));
		return;
	}
	if (!bufferValid || buffer.size() != size())
		rebuild ();

	if (shift.isNull() && liveZoom == 1.0) {
		pnt.drawPixmap (0, 0, buffer);
		return;
	}
	// Жест ещё идёт: показываем ту же картинку сдвинутой и растянутой.
	// По краям вылезает пустота — закрашиваем цветом моря, чтобы не
	// мигало чёрным.
	pnt.fillRect (rect(), QColor(0xB0, 0xD8, 0xE0));
	pnt.save ();
	pnt.translate (shift);
	if (liveZoom != 1.0) {
		pnt.translate (width()/2.0, height()/2.0);
		pnt.scale (liveZoom, liveZoom);
		pnt.translate (-width()/2.0, -height()/2.0);
	}
	pnt.drawPixmap (0, 0, buffer);
	pnt.restore ();
}

//---------------------------------------------------------------------
void MapView::panBy (double dx, double dy)
{
	// Двигаем не карту, а точку под пальцем: так ощущение, что тянешь
	// сам лист, а не управляешь чем-то через посредника.
	double lon, lat;
	proj->screen2map (width()/2 - int(dx), height()/2 - int(dy), &lon, &lat);
	proj->setMapPointInScreen (lon, lat, width()/2, height()/2);
	// Буфер не трогаем — просто запоминаем, на сколько уехали.
	shift += QPointF (dx, dy);
	settle.start ();
	update ();
}

//---------------------------------------------------------------------
void MapView::glide ()
{
	double dt = 0.016;
	panBy (velocity.x()*dt, velocity.y()*dt);
	// Затухание вдвое за GLIDE_HALFLIFE_MS — так остановка выходит
	// мягкой, без рывка в конце.
	double k = std::pow (0.5, (dt*1000.0)/GLIDE_HALFLIFE_MS);
	velocity *= k;
	if (std::hypot (velocity.x(), velocity.y()) < STOP_SPEED) {
		glideTimer.stop ();
		settleNow ();
	}
}

//---------------------------------------------------------------------
bool MapView::event (QEvent *e)
{
	switch (e->type()) {

	case QEvent::Gesture: {
		QGestureEvent *g = static_cast<QGestureEvent *>(e);
		if (QGesture *p = g->gesture (Qt::PinchGesture)) {
			QPinchGesture *pg = static_cast<QPinchGesture *>(p);
			if (pg->state() == Qt::GestureStarted) {
				pinching = true;
				glideTimer.stop ();
				pinchStart = proj->getScale ();
			}
			// Масштаб считаем от того, что был в начале щипка: так он не
			// уползает от накопления мелких погрешностей.
			proj->setScale (pinchStart * pg->totalScaleFactor());
			liveZoom = proj->getScale() / pinchStart;
			settle.start ();
			update ();
			if (pg->state() == Qt::GestureFinished
			 || pg->state() == Qt::GestureCanceled) {
				pinching = false;
				settleNow ();
			}
			return true;
		}
		break;
	}

	case QEvent::TouchBegin: {
		QTouchEvent *t = static_cast<QTouchEvent *>(e);
		if (t->points().size() != 1)
			break;
		glideTimer.stop ();
		dragging  = true;
		lastTouch = t->points().first().position ();
		velocity  = QPointF (0, 0);
		sinceMove.restart ();
		return true;
	}

	case QEvent::TouchUpdate: {
		QTouchEvent *t = static_cast<QTouchEvent *>(e);
		if (pinching || !dragging || t->points().size() != 1)
			break;
		QPointF now = t->points().first().position ();
		QPointF d   = now - lastTouch;
		panBy (d.x(), d.y());
		qint64 ms = sinceMove.restart ();
		if (ms > 0) {
			// Скорость сглаживаем: иначе последний дёрганый кадр
			// определял бы весь бросок.
			QPointF v (d.x()*1000.0/ms, d.y()*1000.0/ms);
			velocity = velocity*0.6 + v*0.4;
		}
		lastTouch = now;
		return true;
	}

	case QEvent::TouchEnd:
	case QEvent::TouchCancel: {
		if (!dragging)
			break;
		dragging = false;
		// Если палец перед отрывом стоял, бросок не начинаем.
		if (sinceMove.elapsed() < 100
		 && std::hypot (velocity.x(), velocity.y()) > STOP_SPEED)
			glideTimer.start ();
		else
			settleNow ();
		return true;
	}

	default:
		break;
	}
	return QWidget::event (e);
}
