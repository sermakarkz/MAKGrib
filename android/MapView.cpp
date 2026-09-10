/**********************************************************************
MAKGrib для Android — карта под палец.
***********************************************************************/
#include "MapView.h"

#include <algorithm>
#include <cmath>

#include <QtConcurrent>
#include <QTimeZone>
#include <QApplication>
#include <QPainter>
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
// Выполняется в стороннем потоке. Трогать здесь можно только то, что
// главный поток в это время не трогает: рисовалку, прогноз и свою
// копию проекции. Виджет — нельзя.
static QImage renderMap (MapDrawer *drawer, GriddedPlotter *plot,
                         Projection *p, int w, int h)
{
	QImage img (w, h, QImage::Format_RGB32);
	QPainter pnt (&img);
	// Без прогноза — только берег и сетка координат: рисовалка данных
	// не проверяет, что данных нет, и падает на пустом указателе.
	if (plot != nullptr)
		drawer->draw_GSHHS_and_GriddedData (pnt, true, false, p, plot, false);
	else
		drawer->draw_GSHHS (pnt, true, false, p);
	pnt.end ();
	delete p;
	return img;
}

//---------------------------------------------------------------------
MapView::MapView (QWidget *parent)
	: QWidget (parent), drawer (nullptr), proj (nullptr), plot (nullptr),
	  bufferValid (false), rendering (false), pending (false),
	  renderScale (0), renderShift (0, 0),
	  shift (0, 0), residual (0, 0), bufferScale (0),
	  dragging (false), pinching (false), pinchDist (0)
{
	settle.setSingleShot (true);
	settle.setInterval (150);
	connect (&settle, &QTimer::timeout, this, &MapView::settleNow);
	connect (&watcher, &QFutureWatcher<QImage>::finished,
	         this, &MapView::renderDone);
	setAttribute (Qt::WA_AcceptTouchEvents);
	qApp->installEventFilter (this);
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
	waitRender ();
	delete plot;
	delete drawer;
	delete proj;
}

//---------------------------------------------------------------------
void MapView::loadMaps ()
{
	if (drawer != nullptr)
		return;
	waitRender ();
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
	waitRender ();
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
		if (p->isReaderOk()) {
			// Показываем ближайший к текущему времени срок, а не первый
			// в файле: иначе на экране висит момент выпуска прогноза.
			p->setCurrentDateClosestFromNow ();
			plot = p;
		}
		else
			delete p;
	}
	bufferValid = false;
	update ();
	return plot != nullptr;
}

//---------------------------------------------------------------------
bool MapView::forecastTimes (QDateTime *shown, QDateTime *last) const
{
	if (plot == nullptr || !plot->isReaderOk())
		return false;
	std::set<time_t> dates = plot->getReader()->getListDates ();
	if (dates.empty())
		return false;
	if (shown != nullptr)
		*shown = QDateTime::fromSecsSinceEpoch (plot->getCurrentDate(),
		                                        QTimeZone::UTC);
	if (last != nullptr)
		*last  = QDateTime::fromSecsSinceEpoch (*dates.rbegin(),
		                                        QTimeZone::UTC);
	return true;
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
	if (rendering) {
		pending = true;         // рисуем уже; перерисуем следом
		return;
	}
	// Рисуем в картинку больше экрана: когда палец тянет карту, из-под
	// края выезжает готовое изображение, а не пустота.
	const int m = MARGIN;
	Projection *p = proj->clone ();
	p->setScreenSize (width() + 2*m, height() + 2*m);

	renderScale = proj->getScale ();
	renderShift = shift;
	rendering   = true;
	bufferValid = true;         // заказ отдан, повторно не заказываем
	watcher.setFuture (QtConcurrent::run (renderMap, drawer, plot, p,
	                                      width() + 2*m, height() + 2*m));
}

//---------------------------------------------------------------------
void MapView::renderDone ()
{
	// Сигнал от прежнего заказа мог остаться в очереди после того, как
	// мы дождались его вручную и заказали новый. Тогда result() ждал бы
	// ещё не готовую картинку и подвесил бы главный поток.
	if (!watcher.isFinished())
		return;
	buffer      = QPixmap::fromImage (watcher.result());
	// Пока рисовали, карту могли утянуть и раздуть. Новая картинка
	// отвечает тому, что было на момент заказа, — остаток сдвига и
	// разницу масштаба учитываем при выводе.
	shift      -= renderShift;
	bufferScale = renderScale;
	rendering   = false;
	if (pending) {
		pending = false;
		rebuild ();
	}
	update ();
}

//---------------------------------------------------------------------
void MapView::waitRender ()
{
	// Рисовалка и прогноз — общие с фоновым потоком; прежде чем их
	// менять или удалять, надо дождаться, пока он их отпустит.
	if (rendering) {
		watcher.waitForFinished ();
		rendering = false;
	}
}

//---------------------------------------------------------------------
void MapView::trackFingers (QTouchEvent *t)
{
	for (const QEventPoint &p : t->points()) {
		if (p.state() == QEventPoint::Released)
			fingers.remove (p.id());
		else
			fingers[p.id()] = mapFromGlobal (p.globalPosition());
	}
	if (t->type() == QEvent::TouchCancel)
		fingers.clear ();
}

//---------------------------------------------------------------------
void MapView::handleFingers ()
{
	if (fingers.size() >= 2) {
		// Берём два пальца с наименьшими номерами: если лягут третий и
		// четвёртый, масштаб не должен прыгать от их появления.
		QList<int> ids = fingers.keys ();
		std::sort (ids.begin(), ids.end());
		QPointF a = fingers.value (ids.at(0));
		QPointF b = fingers.value (ids.at(1));
		double  d = std::hypot (a.x() - b.x(), a.y() - b.y());
		if (d < 20.0)
			return;
		if (!pinching) {
			pinching    = true;
			dragging    = false;
			velocity    = QPointF (0, 0);
			pinchDist   = d;
			glideTimer.stop ();
			return;
		}
		if (pinchDist < 20.0) {
			pinchDist = d;
			return;
		}
		double f = d / pinchDist;
		// Скачок больше чем вдвое за шаг — это не палец, а подмена
		// точки: такой шаг пропускаем.
		if (f > 0.5 && f < 2.0)
			proj->setScale (proj->getScale() * f);
		pinchDist = d;
		update ();
		return;
	}

	if (pinching && fingers.size() < 2) {
		pinching = false;
		dragging = false;
		settleNow ();
	}
}

//---------------------------------------------------------------------
bool MapView::eventFilter (QObject *o, QEvent *e)
{
	switch (e->type()) {
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
	case QEvent::TouchCancel: {
		QTouchEvent *t = static_cast<QTouchEvent *>(e);
		trackFingers (t);
		handleFingers ();
		break;
	}
	default:
		break;
	}
	return QWidget::eventFilter (o, e);
}

//---------------------------------------------------------------------
bool MapView::busy () const
{
	return dragging || pinching || glideTimer.isActive ();
}

//---------------------------------------------------------------------
void MapView::settleNow ()
{
	residual    = QPointF (0, 0);
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
	bool wrongSize = buffer.width()  != width()  + 2*MARGIN
	              || buffer.height() != height() + 2*MARGIN;
	if (!bufferValid || (wrongSize && !busy()))
		rebuild ();                 // заказ уходит в сторонний поток
	if (buffer.isNull()) {
		// Самая первая картинка ещё не готова.
		pnt.fillRect (rect(), QColor(30,60,90));
		return;
	}

	// Во сколько раз вид отличается от нарисованного в буфере. Считаем
	// прямо от масштаба буфера: привязка к началу щипка врала, когда
	// Android не присылал начало жеста.
	double z = (bufferScale > 0) ? proj->getScale()/bufferScale : 1.0;

	pnt.save ();
	pnt.translate (shift);
	if (z != 1.0) {
		pnt.translate (width()/2.0, height()/2.0);
		pnt.scale (z, z);
		pnt.translate (-width()/2.0, -height()/2.0);
	}
	// Картинка шире экрана на MARGIN с каждой стороны, поэтому её левый
	// верхний угол лежит выше и левее нуля.
	pnt.drawPixmap (QPointF(-MARGIN, -MARGIN), buffer);
	pnt.restore ();
}

//---------------------------------------------------------------------
void MapView::panBy (double dx, double dy)
{
	// Двигаем не карту, а точку под пальцем: так ощущение, что тянешь
	// сам лист, а не управляешь чем-то через посредника.
	//
	// Проекция умеет сдвигаться только на целое число точек экрана, а
	// палец ходит дробно. Дробные остатки копим: иначе картинка едет за
	// пальцем чуть дальше, чем карта под ней, за секунду набегает
	// десяток точек — и на перерисовке всё отскакивает назад.
	residual += QPointF (dx, dy);
	int ix = int (residual.x());
	int iy = int (residual.y());
	if (ix == 0 && iy == 0) {
		if (!busy())
			settle.start ();
		return;
	}
	residual -= QPointF (ix, iy);

	double lon, lat;
	proj->screen2map (width()/2 - ix, height()/2 - iy, &lon, &lat);
	proj->setMapPointInScreen (lon, lat, width()/2, height()/2);
	shift += QPointF (ix, iy);
	if (std::fabs (shift.x()) > MARGIN*0.5
	 || std::fabs (shift.y()) > MARGIN*0.5)
		bufferValid = false;        // запас на исходе, пора дорисовать
	if (!busy())
		settle.start ();
	update ();
}

//---------------------------------------------------------------------
void MapView::glide ()
{
	double dt = 0.016;
	panBy (velocity.x()*dt, velocity.y()*dt);
	if (std::fabs (shift.x()) > MARGIN || std::fabs (shift.y()) > MARGIN) {
		// Уехали дальше нарисованного запаса, а новая картинка ещё не
		// готова: дальше пошла бы пустота.
		glideTimer.stop ();
		settleNow ();
		return;
	}
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

	case QEvent::TouchBegin: {
		QTouchEvent *t = static_cast<QTouchEvent *>(e);
		// Кнопки масштаба и шторки лежат поверх карты и её же дети.
		// Сами они касаний не принимают, и если мы возьмём касание себе,
		// Qt не сделает из него нажатие мышью — кнопка окажется мёртвой.
		if (!t->points().isEmpty()
		 && childAt (t->points().first().position().toPoint()) != nullptr)
			break;
		glideTimer.stop ();
		if (!pinching && t->points().size() == 1) {
			dragging  = true;
			lastTouch = t->points().first().position ();
			velocity  = QPointF (0, 0);
			sinceMove.restart ();
		}
		// Принимаем в любом случае: отказ Qt понимает как «виджету эта
		// последовательность не нужна» и перестаёт её слать.
		return true;
	}

	case QEvent::TouchUpdate: {
		QTouchEvent *t = static_cast<QTouchEvent *>(e);
		// Щипок считается в фильтре, здесь только перетаскивание.
		if (pinching || !dragging || t->points().size() != 1)
			return true;            // не наше дело, но событие принимаем
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
		if (pinching || !dragging)
			return true;            // щипок закрывает фильтр
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
