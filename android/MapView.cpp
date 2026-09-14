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

#include "ColorScale.h"
#include "DataDefines.h"
#include "DataPointInfo.h"
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
	  stampAlarm (false),
	  ownOk (false), own (0, 0), ownAcc (0),
	  probing (false),
	  boatOn (false), boatAt (0, 0), boatCourse (0),
	  routing (NoRoute), dragPoint (-1), holdPoint (-1),
	  tapAt (0, 0), tapMs (0),
	  curType (0), curLevelType (0), curLevelValue (0),
	  dragging (false), pinching (false), pinchDist (0)
{
	settle.setSingleShot (true);
	settle.setInterval (150);
	connect (&settle, &QTimer::timeout, this, &MapView::settleNow);
	connect (&watcher, &QFutureWatcher<QImage>::finished,
	         this, &MapView::renderDone);
	// Долгое нажатие на точку удаляет её: место под кнопку в ряду уже
	// кончилось, а без удаления правка неполная.
	holdTimer.setSingleShot (true);
	holdTimer.setInterval (600);
	connect (&holdTimer, &QTimer::timeout, this, &MapView::holdFired);
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
// Ветер красится по служебному коду: самого «поля скорости» в файле нет,
// оно считается из двух составляющих.
static int lookupType (int dataType)
{
	// Служебных полей в файле нет: скорость ветра считается из двух
	// составляющих, а «температура минус точка росы» — из двух полей.
	if (dataType == GRB_PRV_WIND_XY2D)
		return GRB_WIND_VX;
	if (dataType == GRB_PRV_DIFF_TEMPDEW)
		return GRB_DEWPOINT;
	return dataType;
}

//---------------------------------------------------------------------
bool MapView::hasField (int dataType, int levelType, int levelValue) const
{
	if (plot == nullptr || !plot->isReaderOk())
		return false;
	int look = lookupType (dataType);
	for (const DataCode &d : plot->getReader()->getAllDataCode()) {
		if (d.dataType != look)
			continue;
		if (levelType >= 0 && d.levelType != levelType)
			continue;
		if (levelValue >= 0 && d.levelValue != levelValue)
			continue;
		return true;
	}
	return false;
}

//---------------------------------------------------------------------
void MapView::setColorMap (int dataType, int levelType, int levelValue)
{
	if (drawer == nullptr)
		return;
	DataCode dtc (dataType, (levelType >= 0) ? levelType : LV_GND_SURF,
	              (levelValue >= 0) ? levelValue : 0);
	if (dataType == GRB_PRV_WIND_XY2D)
		dtc = DataCode (GRB_PRV_WIND_XY2D, LV_ABOV_GND, 10);
	else if (dataType == GRB_PRV_DIFF_TEMPDEW)
		dtc = DataCode (GRB_PRV_DIFF_TEMPDEW, LV_ABOV_GND, 2);
	else if (plot != nullptr && plot->isReaderOk()) {
		// Уровень берём из файла: у облачности он «вся атмосфера», у
		// осадков — поверхность, и угадывать их незачем.
		for (const DataCode &d : plot->getReader()->getAllDataCode()) {
			if (d.dataType != dataType)
				continue;
			if (levelType >= 0 && d.levelType != levelType)
				continue;
			if (levelValue >= 0 && d.levelValue != levelValue)
				continue;
			dtc = d;
			break;
		}
	}
	// Рисовалку трогает фоновый поток; ждём, пока отпустит.
	waitRender ();
	drawer->setColorMapData (dtc);
	curType       = dtc.dataType;
	curLevelType  = dtc.levelType;
	curLevelValue = dtc.levelValue;
	bufferValid = false;
	update ();
}

//---------------------------------------------------------------------
// Границы цветовой шкалы берём у самой палитры: она задана таблицей
// «от какого значения до какого какой цвет», и её края — это и есть
// концы шкалы.
bool MapView::layerRange (double *lo, double *hi) const
{
	if (plot == nullptr || curType == 0)
		return false;
	ColorScale *cs = plot->getColorScale (
	                     DataCode (curType, curLevelType, curLevelValue));
	if (cs == nullptr || cs->colors.empty())
		return false;
	if (lo != nullptr)
		*lo = cs->colors.front()->vmin;
	if (hi != nullptr)
		*hi = cs->colors.back()->vmax;
	return true;
}

//---------------------------------------------------------------------
QColor MapView::layerColor (double v) const
{
	if (plot == nullptr || curType == 0)
		return QColor (Qt::transparent);
	return QColor (plot->getDataCodeColor (
	                   DataCode (curType, curLevelType, curLevelValue),
	                   v, true));
}

//---------------------------------------------------------------------
void MapView::setRouteMode (int mode)
{
	routing   = mode;
	dragPoint = -1;
	update ();
}

//---------------------------------------------------------------------
// Расстояние от точки до отрезка — по нему решаем, попал ли палец в
// участок маршрута, чтобы вставить туда новую точку.
static double gapToLeg (const QPointF &p, const QPointF &a, const QPointF &b)
{
	double vx = b.x()-a.x(), vy = b.y()-a.y();
	double len = vx*vx + vy*vy;
	double t = (len > 0) ? ((p.x()-a.x())*vx + (p.y()-a.y())*vy) / len : 0;
	t = qBound (0.0, t, 1.0);
	return std::hypot (p.x() - (a.x()+t*vx), p.y() - (a.y()+t*vy));
}

//---------------------------------------------------------------------
// Ближайшая точка маршрута под пальцем, иначе -1.
int MapView::pointAt (const QPointF &screen) const
{
	int best = -1;
	double bestGap = 34.0;              // палец накрывает примерно столько
	for (int i = 0; i < way.size(); ++i) {
		int x, y;
		proj->map2screen (way.at(i).x(), way.at(i).y(), &x, &y);
		double g = std::hypot (screen.x()-x, screen.y()-y);
		if (g < bestGap) {
			bestGap = g;
			best = i;
		}
	}
	return best;
}

//---------------------------------------------------------------------
// Участок, по которому попали, — новая точка встанет в его середину.
int MapView::legAt (const QPointF &screen) const
{
	int best = -1;
	double bestGap = 26.0;
	for (int i = 1; i < way.size(); ++i) {
		int x1, y1, x2, y2;
		proj->map2screen (way.at(i-1).x(), way.at(i-1).y(), &x1, &y1);
		proj->map2screen (way.at(i).x(),   way.at(i).y(),   &x2, &y2);
		double g = gapToLeg (screen, QPointF (x1, y1), QPointF (x2, y2));
		if (g < bestGap) {
			bestGap = g;
			best = i;
		}
	}
	return best;
}

//---------------------------------------------------------------------
void MapView::insertRoutePoint (int before, const QPointF &lonLat)
{
	if (before < 0 || before > way.size())
		return;
	way.insert (before, lonLat);
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::moveRoutePoint (int i, const QPointF &lonLat)
{
	if (i < 0 || i >= way.size())
		return;
	way[i] = lonLat;
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::removeRoutePoint (int i)
{
	if (i < 0 || i >= way.size())
		return;
	way.removeAt (i);
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::holdFired ()
{
	if (holdPoint < 0) {
		// Палец не на точке маршрута — значит, смотрим погоду.
		probing    = true;
		dragging   = false;
		probeAt    = tapAt;
		probeLines = pointInfo (tapAt).split ('\n');
		update ();
		return;
	}
	removeRoutePoint (holdPoint);
	holdPoint = -1;
	dragPoint = -1;
}

//---------------------------------------------------------------------
void MapView::addRoutePoint (const QPointF &lonLat)
{
	way << lonLat;
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::dropLastPoint ()
{
	if (way.isEmpty())
		return;
	way.removeLast ();
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::clearRoute ()
{
	if (way.isEmpty())
		return;
	way.clear ();
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
void MapView::setRoute (const QList<QPointF> &pts)
{
	way = pts;
	emit routeChanged ();
	update ();
}

//---------------------------------------------------------------------
// Длина одного участка в морских милях и курс на нём.
static double legMiles (const QPointF &a, const QPointF &b)
{
	const double R = 3440.065;
	double la1 = a.y() * M_PI/180.0, la2 = b.y() * M_PI/180.0;
	double dla = la2 - la1;
	double dlo = (b.x() - a.x()) * M_PI/180.0;
	double h = std::sin(dla/2)*std::sin(dla/2)
	         + std::cos(la1)*std::cos(la2)*std::sin(dlo/2)*std::sin(dlo/2);
	return 2.0 * R * std::asin (std::sqrt (h));
}

//---------------------------------------------------------------------
static double legCourse (const QPointF &a, const QPointF &b)
{
	double la1 = a.y() * M_PI/180.0, la2 = b.y() * M_PI/180.0;
	double dlo = (b.x() - a.x()) * M_PI/180.0;
	double y = std::sin(dlo) * std::cos(la2);
	double x = std::cos(la1)*std::sin(la2)
	         - std::sin(la1)*std::cos(la2)*std::cos(dlo);
	double c = std::atan2 (y, x) * 180.0/M_PI;
	return (c < 0) ? c + 360.0 : c;
}

//---------------------------------------------------------------------
void MapView::setStamp (const QString &text, bool alarm)
{
	stamp      = text;
	stampAlarm = alarm;
	update ();
}

//---------------------------------------------------------------------
void MapView::setOwnPos (double lon, double lat, double accuracy)
{
	own    = QPointF (lon, lat);
	ownAcc = accuracy;
	ownOk  = true;
	update ();
}

//---------------------------------------------------------------------
// Погода в точке на показанный срок. Значения достаёт DataPointInfo —
// тот же, которым пользуется настольная версия: он знает, где что лежит,
// и сам говорит, чего в файле нет.
QString MapView::pointInfo (const QPointF &screen) const
{
	if (plot == nullptr || !plot->isReaderOk())
		return tr("прогноз не загружен");

	double lon, lat;
	proj->screen2map (int(screen.x()), int(screen.y()), &lon, &lat);
	DataPointInfo pi (plot->getReader(), lon, lat, plot->getCurrentDate());

	QStringList out;
	out << Util::formatLongitude (lon) + "  " + Util::formatLatitude (lat);

	float sp = 0, dir = 0;
	if (pi.getWindValues (Altitude (LV_ABOV_GND, 10), &sp, &dir))
		out << tr("Ветер %1 %2")
		        .arg (Util::formatSpeed_Wind (sp, true))
		        .arg (Util::formatDirection (dir, true));
	if (pi.hasGUSTsfc())
		out << tr("Порывы %1")
		        .arg (Util::formatSpeed_Wind (
		            pi.getDataValue (DataCode (GRB_WIND_GUST, LV_GND_SURF, 0)),
		            true));
	float ht = 0, per = 0, wdir = 0;
	if (pi.getWaveValues (GRB_PRV_WAV_SIG, &ht, &per, &wdir))
		out << tr("Волна %1 м").arg (ht, 0, 'f', 1);
	if (pi.hasPressureMSL())
		out << tr("Давление %1")
		        .arg (Util::formatPressure (
		            pi.getDataValue (DataCode (GRB_PRESSURE_MSL, LV_MSL, 0)),
		            true, 0));
	if (pi.hasTemp())
		out << tr("Воздух %1")
		        .arg (Util::formatTemperature (
		            pi.getDataValue (DataCode (GRB_TEMP, LV_ABOV_GND, 2)), true));
	if (pi.hasWaterTemp())
		out << tr("Вода %1")
		        .arg (Util::formatTemperature (
		            pi.getDataValue (DataCode (GRB_WTMP, LV_GND_SURF, 0)), true));
	if (pi.hasCloudTotal())
		out << tr("Облачность %1")
		        .arg (Util::formatPercentValue (
		            pi.getDataValue (DataCode (GRB_CLOUD_TOT, LV_ATMOS_ALL, 0))));
	if (pi.hasRain())
		out << tr("Осадки %1")
		        .arg (Util::formatRain (
		            pi.getDataValue (DataCode (GRB_PRECIP_TOT, LV_GND_SURF, 0))));

	if (out.size() == 1)
		out << tr("здесь данных нет");
	return out.join ("\n");
}

//---------------------------------------------------------------------
// Судно на маршруте: идём по участкам, пока не наберём нужные мили.
void MapView::setBoatMiles (double miles)
{
	if (miles < 0 || way.size() < 2) {
		if (boatOn) {
			boatOn = false;
			update ();
		}
		return;
	}
	double left = miles;
	QPointF pos = way.first ();
	double  crs = legCourse (way.at(0), way.at(1));
	for (int i = 1; i < way.size(); ++i) {
		double d = legMiles (way.at(i-1), way.at(i));
		crs = legCourse (way.at(i-1), way.at(i));
		if (left <= d || i == way.size()-1) {
			double f = (d > 0) ? qBound (0.0, left/d, 1.0) : 0.0;
			pos = QPointF (way.at(i-1).x() + (way.at(i).x()-way.at(i-1).x())*f,
			               way.at(i-1).y() + (way.at(i).y()-way.at(i-1).y())*f);
			break;
		}
		left -= d;
	}
	boatOn     = true;
	boatAt     = pos;
	boatCourse = crs;
	update ();
}

//---------------------------------------------------------------------
// Длина маршрута в морских милях, по дуге большого круга: на каспийских
// расстояниях разница с плоской прикидкой невелика, но она копится, а
// мили потом делятся на скорость.
double MapView::routeMiles () const
{
	const double R = 3440.065;          // радиус Земли в морских милях
	double sum = 0;
	for (int i = 1; i < way.size(); ++i) {
		double la1 = way.at(i-1).y() * M_PI/180.0;
		double la2 = way.at(i).y()   * M_PI/180.0;
		double dla = la2 - la1;
		double dlo = (way.at(i).x() - way.at(i-1).x()) * M_PI/180.0;
		double a = std::sin(dla/2)*std::sin(dla/2)
		         + std::cos(la1)*std::cos(la2)*std::sin(dlo/2)*std::sin(dlo/2);
		sum += 2.0 * R * std::asin (std::sqrt (a));
	}
	return sum;
}

//---------------------------------------------------------------------
QList<QDateTime> MapView::forecastSteps () const
{
	QList<QDateTime> out;
	if (plot == nullptr || !plot->isReaderOk())
		return out;
	for (time_t t : plot->getReader()->getListDates())
		out << QDateTime::fromSecsSinceEpoch (t, QTimeZone::UTC);
	return out;
}

//---------------------------------------------------------------------
int MapView::forecastIndex () const
{
	if (plot == nullptr || !plot->isReaderOk())
		return -1;
	int i = 0;
	for (time_t t : plot->getReader()->getListDates()) {
		if (t == plot->getCurrentDate())
			return i;
		++i;
	}
	return -1;
}

//---------------------------------------------------------------------
void MapView::showForecastStep (int index)
{
	if (plot == nullptr || !plot->isReaderOk() || index < 0)
		return;
	std::set<time_t> dates = plot->getReader()->getListDates ();
	if (index >= int (dates.size()))
		return;
	std::set<time_t>::const_iterator it = dates.begin ();
	std::advance (it, index);
	if (*it == plot->getCurrentDate())
		return;
	// Прогноз читает фоновый поток; менять срок, пока он рисует, нельзя.
	waitRender ();
	plot->setCurrentDate (*it);
	bufferValid = false;
	emit forecastTimeChanged ();
	update ();
}

//---------------------------------------------------------------------
// Ближайший к заданному моменту срок. Точного попадания ждать не стоит:
// сроки идут через час или три, и между ними ничего нет.
void MapView::showForecastNear (const QDateTime &moment)
{
	if (plot == nullptr || !plot->isReaderOk())
		return;
	std::set<time_t> dates = plot->getReader()->getListDates ();
	time_t want = moment.toSecsSinceEpoch ();
	int best = -1, i = 0;
	qint64 bestGap = 0;
	for (time_t t : dates) {
		qint64 gap = qAbs (qint64 (t) - qint64 (want));
		if (best < 0 || gap < bestGap) {
			best = i;
			bestGap = gap;
		}
		++i;
	}
	if (best >= 0)
		showForecastStep (best);
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
void MapView::setView (double lon, double lat, double sc)
{
	if (sc > 0)
		proj->setScale (sc);
	proj->setMapPointInScreen (lon, lat, width()/2, height()/2);
	bufferValid = false;
	announce ();
	update ();
}

//---------------------------------------------------------------------
double MapView::scale () const
{
	return proj->getScale ();
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
		              tr("Карты ещё раскладываются…"));
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

	// Маршрут рисуем поверх картинки и по текущей проекции, а не по той,
	// в которой нарисован буфер: точки должны держаться за карту даже
	// когда подложка ещё не перерисована.
	if (!way.isEmpty()) {
		QPolygonF line;
		for (const QPointF &q : way) {
			int i, j;
			proj->map2screen (q.x(), q.y(), &i, &j);
			line << QPointF (i, j);
		}
		pnt.setRenderHint (QPainter::Antialiasing, true);
		pnt.setPen (QPen (QColor (255, 255, 255, 200), 6,
		                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		pnt.drawPolyline (line);
		pnt.setPen (QPen (QColor (200, 30, 30), 3,
		                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		pnt.drawPolyline (line);
		QFont f = pnt.font ();  f.setPixelSize (11);  f.setBold (true);
		pnt.setFont (f);
		double r = (routing == EditRoute) ? 13 : 9;
		for (int k = 0; k < line.size(); ++k) {
			pnt.setPen (QPen (Qt::white, 2));
			pnt.setBrush (QColor (k == dragPoint ? QColor (30, 120, 60)
			                                     : QColor (200, 30, 30)));
			pnt.drawEllipse (line.at(k), r, r);
			pnt.setPen (Qt::white);
			pnt.drawText (QRectF (line.at(k).x()-r, line.at(k).y()-r, 2*r, 2*r),
			              Qt::AlignCenter, QString::number (k+1));
		}
	}

	// Табличка вверху. Длинную подсказку переносим по словам, чтобы она
	// не вылезала за края экрана.
	if (!stamp.isEmpty()) {
		QFont f = pnt.font ();
		f.setPixelSize (17);
		f.setBold (true);
		pnt.setFont (f);
		QFontMetrics fm (f);
		const int pad = 9;
		int maxw = width() - 40;
		QRect need = fm.boundingRect (QRect (0, 0, maxw - 2*pad, 1000),
		                              Qt::AlignHCenter | Qt::TextWordWrap,
		                              stamp);
		int bw = need.width() + 2*pad + 14;
		int bh = need.height() + 2*pad;
		int bx = (width() - bw)/2;
		pnt.setRenderHint (QPainter::Antialiasing, true);
		pnt.setPen (Qt::NoPen);
		pnt.setBrush (stampAlarm ? QColor (150, 45, 35, 220)
		                         : QColor (20, 40, 60, 210));
		pnt.drawRoundedRect (QRectF (bx, 12, bw, bh), 13, 13);
		pnt.setPen (Qt::white);
		pnt.drawText (QRect (bx + pad, 12 + pad, bw - 2*pad, bh - 2*pad),
		              Qt::AlignCenter | Qt::TextWordWrap, stamp);
	}

	// Окошко с погодой в точке: ставим его так, чтобы не закрывал палец
	// — сверху, если хватает места, иначе снизу, и в пределах экрана.
	if (probing && !probeLines.isEmpty()) {
		QFont f = pnt.font ();
		f.setPixelSize (15);
		QFont bold = f;  bold.setBold (true);
		QFontMetrics fm (f), fb (bold);
		const int pad = 12;
		int tw = 0;
		for (int i = 0; i < probeLines.size(); ++i)
			tw = qMax (tw, (i == 0 ? fb : fm).horizontalAdvance (probeLines.at(i)));
		int bw = tw + 2*pad;
		int bh = probeLines.size()*fm.lineSpacing() + 2*pad;
		int bx = int (probeAt.x()) - bw/2;
		int by = int (probeAt.y()) - bh - 60;
		if (by < 8)
			by = int (probeAt.y()) + 60;
		by = qBound (8, by, qMax (8, height() - bh - 8));
		bx = qBound (8, bx, qMax (8, width() - bw - 8));

		pnt.setRenderHint (QPainter::Antialiasing, true);
		pnt.setPen (Qt::NoPen);
		pnt.setBrush (QColor (20, 40, 60, 230));
		pnt.drawRoundedRect (QRectF (bx, by, bw, bh), 14, 14);
		pnt.setPen (Qt::white);
		for (int i = 0; i < probeLines.size(); ++i) {
			pnt.setFont (i == 0 ? bold : f);
			pnt.drawText (bx + pad,
			              by + pad + fm.ascent() + i*fm.lineSpacing(),
			              probeLines.at(i));
		}
	}

	// Своё место: точка и круг точности. Круг рисуем настоящего
	// размера — по нему видно, верить показанию или нет.
	if (ownOk) {
		int i, j;
		proj->map2screen (own.x(), own.y(), &i, &j);
		if (ownAcc > 1.0) {
			// Метры переводим в градусы широты, а их — в точки экрана.
			double degs = ownAcc / 1852.0 / 60.0;
			int i2, j2;
			proj->map2screen (own.x(), own.y() + degs, &i2, &j2);
			double r = std::fabs (j2 - j);
			if (r > 3 && r < 400) {
				pnt.setPen (QPen (QColor (40, 120, 220, 150), 2));
				pnt.setBrush (QColor (40, 120, 220, 40));
				pnt.drawEllipse (QPointF (i, j), r, r);
			}
		}
		pnt.setPen (QPen (Qt::white, 3));
		pnt.setBrush (QColor (30, 110, 230));
		pnt.drawEllipse (QPointF (i, j), 9, 9);
	}

	// Судно: где оно окажется к показанному сроку прогноза. Рисуем
	// стрелкой по курсу — так видно не только место, но и куда идём.
	if (boatOn) {
		int i, j;
		proj->map2screen (boatAt.x(), boatAt.y(), &i, &j);
		pnt.save ();
		pnt.setRenderHint (QPainter::Antialiasing, true);
		pnt.translate (i, j);
		pnt.rotate (boatCourse);
		QPolygonF hull;
		hull << QPointF (0, -17) << QPointF (11, 12)
		     << QPointF (0, 6)   << QPointF (-11, 12);
		pnt.setPen (QPen (QColor (20, 40, 60), 2.5));
		pnt.setBrush (QColor (255, 214, 64));
		pnt.drawPolygon (hull);
		pnt.restore ();
	}
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
			tapAt     = t->points().first().position ();
			tapMs     = QDateTime::currentMSecsSinceEpoch ();
			// В правке палец, легший на точку, тащит её, а не карту.
			dragPoint = (routing == EditRoute) ? pointAt (tapAt) : -1;
			if (dragPoint >= 0) {
				holdPoint = dragPoint;
				holdTimer.start ();
				update ();
				return true;
			}
			// Держим палец на карте — покажем погоду в этой точке. Отсчёт
			// тот же, что у удаления точки: сработает одно из двух, смотря
			// попали в маршрут или нет.
			if (routing == NoRoute)
				holdTimer.start ();
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
		if (probing && !t->points().isEmpty()) {
			probeAt    = t->points().first().position ();
			probeLines = pointInfo (probeAt).split ('\n');
			update ();
			return true;
		}
		if (dragPoint >= 0 && !t->points().isEmpty()) {
			QPointF at = t->points().first().position ();
			// Повели пальцем — значит тащат, а не удаляют.
			if (std::hypot (at.x()-tapAt.x(), at.y()-tapAt.y()) > 10.0) {
				holdTimer.stop ();
				holdPoint = -1;
			}
			double lon, lat;
			proj->screen2map (int(at.x()), int(at.y()), &lon, &lat);
			moveRoutePoint (dragPoint, QPointF (lon, lat));
			return true;
		}
		// Повели пальцем до срабатывания отсчёта — значит, тащат карту.
		if (!t->points().isEmpty() && holdTimer.isActive()) {
			QPointF at = t->points().first().position ();
			if (std::hypot (at.x()-tapAt.x(), at.y()-tapAt.y()) > 12.0)
				holdTimer.stop ();
		}
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
		holdTimer.stop ();
		holdPoint = -1;
		if (probing) {
			probing  = false;
			dragging = false;
			probeLines.clear ();
			update ();
			return true;
		}
		if (dragPoint >= 0) {
			dragPoint = -1;
			settleNow ();
			return true;
		}
		if (pinching || !dragging)
			return true;            // щипок закрывает фильтр
		dragging = false;
		// Короткое касание без сдвига — это работа с маршрутом, а не
		// бросок карты.
		if (routing != NoRoute) {
			QTouchEvent *t = static_cast<QTouchEvent *>(e);
			QPointF now = t->points().isEmpty()
			                  ? tapAt : t->points().first().position ();
			double moved = std::hypot (now.x()-tapAt.x(), now.y()-tapAt.y());
			if (moved < 14.0
			 && QDateTime::currentMSecsSinceEpoch() - tapMs < 500) {
				double lon, lat;
				proj->screen2map (int(now.x()), int(now.y()), &lon, &lat);
				if (routing == DrawRoute)
					addRoutePoint (QPointF (lon, lat));
				else {
					// В правке касание по участку вставляет в него точку;
					// мимо маршрута — ничего, чтобы случайное касание не
					// портило проложенное.
					int leg = legAt (now);
					if (leg > 0)
						insertRoutePoint (leg, QPointF (lon, lat));
					else if (way.size() < 2)
						// Точек меньше двух — участков нет, вставлять
						// некуда, и портить нечего: просто добавляем.
						// Иначе, удалив одну из двух, выйти из правки
						// было бы нечем, кроме как через «Закончить».
						addRoutePoint (QPointF (lon, lat));
				}
				return true;
			}
		}
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
