/**********************************************************************
MAKGrib для Android — карта под палец.

Настольная Terrain здесь не годится: она вся построена вокруг мыши —
нажал, потянул, отпустил, колесо, правая кнопка. На телефоне жесты
другие и, главное, инерционные: палец отпущен, а карта ещё едет.
Поэтому свой виджет, а рисование берём готовое, MapDrawer.
***********************************************************************/
#ifndef MAPVIEW_H
#define MAPVIEW_H

#include <memory>

#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class QTouchEvent;
class GshhsReader;
class MapDrawer;
class Projection;
class GriddedPlotter;

class MapView : public QWidget
{ Q_OBJECT
	public:
		explicit MapView (QWidget *parent = nullptr);
		~MapView () override;

		// Готовы ли карты: без них рисовать нечего.
		bool  hasMaps () const   { return drawer != nullptr; }
		void  loadMaps ();

		// Показать прогноз (файл GRIB). Пустая строка — убрать.
		bool  setForecast (const QString &path);

		// На какой момент показан прогноз и до какого он есть. Нужно,
		// чтобы вчерашний прогноз не выдавать за сегодняшний.
		bool  forecastTimes (QDateTime *shown, QDateTime *last) const;

		// Подложка карты: какое поле красить цветом. Уровень можно не
		// указывать — возьмём тот, что есть в файле.
		bool  hasField (int dataType, int levelType = -1,
		                int levelValue = -1) const;
		void  setColorMap (int dataType, int levelType = -1,
		                   int levelValue = -1);

		// Цветовая шкала текущей подложки: код поля, границы и цвет
		// значения — для полоски у правого края.
		int   layerType () const   { return curType; }
		bool  layerRange (double *lo, double *hi) const;
		QColor layerColor (double v) const;

		// Сроки прогноза для шкалы времени и листания.
		bool  hasForecast () const   { return plot != nullptr; }
		QList<QDateTime> forecastSteps () const;
		int   forecastIndex () const;
		void  showForecastStep (int index);
		void  showForecastNear (const QDateTime &moment);

		void  setCenter (double lon, double lat);
		void  zoomBy (double factor);
		Projection *projection () const  { return proj; }

	signals:
		// Куда сейчас смотрим — для надписи сверху.
		void  viewChanged (double lon, double lat, double scale);
		// Показан другой срок прогноза.
		void  forecastTimeChanged ();

	protected:
		// Касания ловим на уровне всего приложения: Qt отдаёт второй
		// палец лишь в тот кадр, когда его прижали, а дальше адресует
		// его кому-то другому. По номерам точек мы собираем пальцы
		// обратно, кому бы Qt их ни слал.
		bool  eventFilter (QObject *o, QEvent *e) override;
		void  paintEvent (QPaintEvent *) override;
		void  resizeEvent (QResizeEvent *) override;
		bool  event (QEvent *e) override;

	private slots:
		void  glide ();          // докатывание после отпускания пальца
		void  settleNow ();      // перерисовать начисто
		void  renderDone ();     // фоновая отрисовка закончилась

	private:
		void  rebuild ();        // заказать перерисовку подложки
		void  waitRender ();     // дождаться фоновой отрисовки
		void  panBy (double dx, double dy);
		void  announce ();
		// Идёт ли жест. Перерисовка стоит 400 мс на слабом телефоне и
		// держит главный поток; если затеять её посреди щипка, телефон
		// перестаёт слышать пальцы, и жест пропадает целиком.
		bool  busy () const;
		void  trackFingers (QTouchEvent *t);
		void  handleFingers ();

		std::shared_ptr<GshhsReader> gshhs;
		MapDrawer      *drawer;
		Projection     *proj;
		GriddedPlotter *plot;

		// Картинка рисуется с запасом за краями экрана: когда палец
		// тянет карту, из-под края выезжает готовое изображение, а не
		// пустота, которую потом дорисовывают рывком.
		static const int MARGIN = 320;
		QPixmap  buffer;         // экран плюс поля с четырёх сторон
		bool     bufferValid;
		// Рисование берега занимает на слабом телефоне около полусекунды
		// и держало бы главный поток: карта замирала бы под пальцем. Тут
		// оно идёт в стороне, а на экране пока живёт прежняя картинка.
		QFutureWatcher<QImage> watcher;
		bool     rendering;
		bool     pending;        // пока рисовали, вид опять изменился
		double   renderScale;    // масштаб и сдвиг на момент заказа
		QPointF  renderShift;
		// Пока палец ведёт, картинку не перерисовываем, а сдвигаем:
		// берег из GSHHS рисуется небыстро, и на слабом телефоне это
		// разница между «карта едет за пальцем» и «карта вязнет».
		QPointF  shift;          // на сколько уехали от того, что в буфере
		QPointF  residual;       // дробный остаток сдвига
		double   bufferScale;    // масштаб, при котором нарисован буфер
		QTimer   settle;         // перерисовать, когда всё успокоилось

		// Прокрутка пальцем.
		bool     dragging;
		QPointF  lastTouch;
		QPointF  velocity;       // пикселей в секунду
		QElapsedTimer sinceMove;
		QTimer   glideTimer;

		// Щипок считаем сами, по расстоянию между двумя пальцами.
		// Распознаватель Qt здесь непригоден: он завершает жест на любом
		// событии, где точек не ровно две, а Qt посреди щипка изредка
		// присылает одну — жест разваливался, и карта вместо масштаба
		// уезжала броском.
		int      curType;        // какое поле красит подложку
		int      curLevelType, curLevelValue;

		QHash<int, QPointF> fingers;   // палец → где он сейчас
		bool     pinching;
		double   pinchDist;      // расстояние между пальцами на прошлом шаге
};

#endif
