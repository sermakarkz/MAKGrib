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

#include <QElapsedTimer>
#include <QPixmap>
#include <QPointF>
#include <QTimer>
#include <QWidget>

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

		void  setCenter (double lon, double lat);
		void  zoomBy (double factor);
		Projection *projection () const  { return proj; }

	signals:
		// Куда сейчас смотрим — для надписи сверху.
		void  viewChanged (double lon, double lat, double scale);

	protected:
		void  paintEvent (QPaintEvent *) override;
		void  resizeEvent (QResizeEvent *) override;
		bool  event (QEvent *e) override;

	private slots:
		void  glide ();          // докатывание после отпускания пальца

	private:
		void  rebuild ();        // перерисовать подложку в буфер
		void  panBy (double dx, double dy);
		void  announce ();

		std::shared_ptr<GshhsReader> gshhs;
		MapDrawer      *drawer;
		Projection     *proj;
		GriddedPlotter *plot;

		QPixmap  buffer;         // готовая картинка карты
		bool     bufferValid;

		// Прокрутка пальцем.
		bool     dragging;
		QPointF  lastTouch;
		QPointF  velocity;       // пикселей в секунду
		QElapsedTimer sinceMove;
		QTimer   glideTimer;

		// Щипок.
		bool     pinching;
		double   pinchStart;
};

#endif
