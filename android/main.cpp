/**********************************************************************
MAKGrib для Android.

Смысл приложения — скачать погоду, пока есть интернет, и смотреть её
потом. Поэтому карта во весь экран, под ней шкала времени и четыре
кнопки: листать срок назад и вперёд, настроить и скачать. Настройки
прячутся в шторку: их трогают редко.
***********************************************************************/
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QTimeZone>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "AppData.h"
#include "LayerBar.h"
#include "ScaleBar.h"
#include "MapView.h"
#include "TimeBar.h"
#include "Wheel.h"

#include "ForecastSource.h"
#include "SourceRegistry.h"
#include "DataDefines.h"
#include "Font.h"
#include "Projection.h"
#include "Settings.h"
#include "Util.h"

//---------------------------------------------------------------------
// Ход загрузки — в полосу и в табличку поверх карты.
class BarProgress : public ForecastProgress
{
	public:
		BarProgress (QProgressBar *b, QLabel *l) : bar (b), lab (l) {}
		void message (const QString &t) override
		{
			lab->setText (t);
			lab->adjustSize ();
			QApplication::processEvents ();
		}
		void step (int done, int total, qint64 bytes) override
		{
			if (total > 0)
				bar->setValue ((100*done)/total);
			lab->setText (QStringLiteral("%1 КБ").arg (bytes/1024));
			lab->adjustSize ();
			QApplication::processEvents ();
		}
		bool canceled () override { return false; }
	private:
		QProgressBar *bar;
		QLabel       *lab;
};

//---------------------------------------------------------------------
// Шапка шторки: за неё её и закрывают — пальцем вниз или касанием по
// треугольнику. Треугольник рисуем сами: готовые знаки вроде «▼» на
// Android норовят стать цветным эмодзи.
class DrawerHandle : public QWidget
{ Q_OBJECT
	public:
		explicit DrawerHandle (QWidget *parent = nullptr) : QWidget (parent)
		{
			setFixedHeight (46);
		}

	signals:
		void closeAsked ();

	protected:
		void paintEvent (QPaintEvent *) override
		{
			QPainter p (this);
			p.setRenderHint (QPainter::Antialiasing, true);
			double cx = width()/2.0, cy = height()/2.0;
			p.setPen (Qt::NoPen);
			p.setBrush (QColor (0xc4, 0xd0, 0xdc));
			p.drawRoundedRect (QRectF (cx-34, cy-11, 68, 5), 3, 3);
			QPolygonF tri;
			tri << QPointF (cx-12, cy+1) << QPointF (cx+12, cy+1)
			    << QPointF (cx, cy+13);
			p.setBrush (QColor (0x6b, 0x7c, 0x8d));
			p.drawPolygon (tri);
		}

		void mousePressEvent (QMouseEvent *e) override
		{
			from = e->position().y ();
			moved = false;
		}

		void mouseMoveEvent (QMouseEvent *e) override
		{
			if (!moved && e->position().y() - from > 40) {
				moved = true;
				emit closeAsked ();
			}
		}

		void mouseReleaseEvent (QMouseEvent *) override
		{
			if (!moved)
				emit closeAsked ();     // просто касание
		}

	private:
		double from = 0;
		bool   moved = false;
};

//---------------------------------------------------------------------
class Main : public QWidget
{ Q_OBJECT
	public:
		Main ()
		{
			map = new MapView;

			// Единственная надпись поверх карты — срок показанного
			// прогноза. Координаты тут висели зря: в море и так видно,
			// где ты, а вот на какой час нарисован ветер — вопрос
			// первый.
			stamp = new QLabel (map);
			stamp->setAlignment (Qt::AlignCenter);
			stamp->setAttribute (Qt::WA_TransparentForMouseEvents);
			say (QString(), false);
			stamp->hide ();
			// Узкая шторка слоёв у левого края. Открывается язычком —
			// сама она закрывает часть карты, а на карту смотрят чаще,
			// чем меняют слой.
			layers = new LayerBar (map);
			layers->hide ();
			connect (layers, &LayerBar::chosen, this, &Main::chooseLayer);
			tab = new QPushButton (QStringLiteral("|||"), map);
			tab->setFixedSize (34, 92);
			tab->setStyleSheet (
			    "font-size: 15px; font-weight: bold; color: white;"
			    "border: none; background: rgba(20,40,60,190);"
			    "border-top-right-radius: 12px;"
			    "border-bottom-right-radius: 12px;");
			connect (tab, &QPushButton::clicked, this, &Main::toggleLayers);

			// Цветовая шкала у правого края: без неё цвет на карте
			// ничего не значит.
			scale = new ScaleBar (map);
			scale->hide ();
			map->installEventFilter (this);

			days = new Wheel (QStringLiteral("Глубина"), 1, 10, 3,
			                  QStringLiteral("сут"));
			step = new Wheel (QStringLiteral("Шаг"), 1, 12, 3,
			                  QStringLiteral("ч"));

			// Сроки в GRIB всегда по Гринвичу. По умолчанию показываем в
			// поясе телефона, но в рейсе он может быть чужим — скажем,
			// судовое время отличается от того, что стоит в аппарате.
			int here = QTimeZone::systemTimeZone()
			               .offsetFromUtc (QDateTime::currentDateTime()) / 3600;
			tz = Settings::getUserSetting ("displayTimeZone", here).toInt();
			if (tz < -12 || tz > 14)
				tz = here;
			zone = new Wheel (QStringLiteral("Часовой пояс"), -12, 14, tz,
			                  QString());
			zone->setFormatter ([](int h) {
				if (h == 0)  return QStringLiteral("UTC");
				return QStringLiteral("UTC%1%2")
				           .arg (h < 0 ? QStringLiteral("−")
				                       : QStringLiteral("+"))
				           .arg (qAbs (h));
			});
			connect (zone, &Wheel::valueChanged, this, &Main::setZone);

			bar = new QProgressBar;    bar->setRange (0, 100);
			bar->setTextVisible (false);
			bar->setMaximumHeight (6);
			bar->hide ();

			line = new TimeBar;
			connect (line, &TimeBar::moved, this, &Main::goToStep);

			// Стрелки берём буквенные, а не «▶»: у геометрических
			// треугольников на Android эмодзи-начертание, и кнопка
			// получается оранжевым квадратом.
			back    = button (QStringLiteral("←"), false);
			forward = button (QStringLiteral("→"), false);
			setup   = button (QStringLiteral("Настроить"), false);
			load    = button (QStringLiteral("Скачать"), true);
			// Пока прогноза нет, листать нечего.
			back->setEnabled (false);
			forward->setEnabled (false);
			connect (back,    &QPushButton::clicked, this, [this]() { shift (-1); });
			connect (forward, &QPushButton::clicked, this, [this]() { shift (+1); });
			connect (setup,   &QPushButton::clicked, this, &Main::togglePanel);
			connect (load,    &QPushButton::clicked, this, &Main::download);

			buttons = new QWidget;
			QHBoxLayout *row = new QHBoxLayout (buttons);
			row->setContentsMargins (0, 0, 0, 0);
			row->setSpacing (1);
			row->addWidget (back,    2);
			row->addWidget (setup,   3);
			row->addWidget (load,    3);
			row->addWidget (forward, 2);

			// Шторка: колёса и шапка, за которую её закрывают. Пока она
			// открыта, кнопки внизу не нужны — шторка встаёт на их место.
			panel = new QWidget;
			panel->setStyleSheet ("background: #f2f5f8;");
			DrawerHandle *handle = new DrawerHandle;
			connect (handle, &DrawerHandle::closeAsked,
			         this, &Main::togglePanel);
			QHBoxLayout *wheels = new QHBoxLayout;
			wheels->setSpacing (10);
			wheels->addWidget (days, 1);
			wheels->addWidget (step, 1);
			wheels->addWidget (zone, 1);
			QVBoxLayout *pl = new QVBoxLayout (panel);
			pl->setContentsMargins (12, 0, 12, 10);
			pl->setSpacing (4);
			pl->addWidget (handle);
			pl->addLayout (wheels);
			panel->hide ();

			QVBoxLayout *lay = new QVBoxLayout (this);
			lay->setContentsMargins (0, 0, 0, 0);
			lay->setSpacing (0);
			lay->addWidget (map, 1);
			lay->addWidget (bar);
			lay->addWidget (panel);
			lay->addWidget (line);
			lay->addWidget (buttons);

			connect (map, &MapView::forecastTimeChanged,
			         this, &Main::showStamp);

			// Карты лежат в APK; раскладываем при первом запуске.
			if (!AppData::ready()) {
				say (QStringLiteral("Раскладываю карты…"), false);
				load->setEnabled (false);
				bar->show ();
				QApplication::processEvents ();
				AppData::unpack ([this](int pc) {
					bar->setValue (pc);
					QApplication::processEvents ();
				});
				bar->hide ();
				load->setEnabled (true);
			}
			Settings::findAppDataDir ();
			// Единицы измерения: ветер в метрах в секунду, давление в
			// миллиметрах ртутного столба, температура в градусах.
			Settings::setUserSetting ("unitsWindSpeed", "m/s");
			Settings::setUserSetting ("unitsPressure", "mmHg");
			Settings::setUserSetting ("unitsTemp", QStringLiteral("°C"));
			// Названия городов на телефоне мельче, чем на мониторе, а
			// смотрят на них с вытянутой руки и на качке.
			Settings::setUserSetting ("FONT_MapCity_1",
			        QFont ("Liberation Sans", 15, QFont::Bold));
			Settings::setUserSetting ("FONT_MapCity_2",
			        QFont ("Liberation Sans", 14, QFont::Bold));
			Settings::setUserSetting ("FONT_MapCity_3",
			        QFont ("Liberation Sans", 13, QFont::Normal));
			Font::loadAllFonts ();
			// Настройки карты читаются один раз, при создании рисовалки,
			// поэтому задаём их до загрузки карт. Своего окна настроек
			// на телефоне нет и не нужно: выбор сделан за штурмана.
			Settings::setUserSetting ("showCountriesBorders", true);
			Settings::setUserSetting ("showRivers", true);
			Settings::setUserSetting ("showCitiesNamesLevel", 2);
			// Белые стрелки поверх поля скорости не читаются.
			Settings::setUserSetting ("windArrowsColorForced", "#141414");
			map->loadMaps ();
			openLast ();
		}

	protected:
		bool eventFilter (QObject *o, QEvent *e) override
		{
			if (o == map && e->type() == QEvent::Resize) {
				placeStamp ();
				placeLayers ();
				placeScale ();
			}
			return QWidget::eventFilter (o, e);
		}

	private slots:
		void toggleLayers ()
		{
			layers->setVisible (!layers->isVisible());
			placeLayers ();
		}

		// Слой меняется мгновенно: все поля уже лежат в скачанном файле,
		// докачивать нечего.
		void chooseLayer (int i)
		{
			if (i < 0 || i >= layers->count())
				return;
			const LayerBar::Item &it = layers->item (i);
			map->setColorMap (it.type, it.levelType, it.levelValue);
			Settings::setUserSetting ("phoneLayer", it.type);
			layers->hide ();
			placeLayers ();
			showScale ();
		}

		void togglePanel ()
		{
			// Пока настраивают прогноз, ни кнопки, ни шкала времени не
			// нужны: место лучше отдать карте.
			bool show = !panel->isVisible ();
			panel->setVisible (show);
			buttons->setVisible (!show);
			line->setVisible (!show);
		}

		// Листание срока: стрелками по одному шагу.
		void shift (int d)
		{
			int i = map->forecastIndex ();
			if (i < 0)
				return;
			goToStep (i + d);
		}

		void goToStep (int i)
		{
			// Срок выбрали вручную — прежнее показание часов больше не
			// та точка, от которой считать перевод пояса.
			wall = QDateTime ();
			map->showForecastStep (i);
			line->setIndex (map->forecastIndex());
		}

		// Пояс меняется в шторке, отзываться должно всё сразу.
		void setZone (int h)
		{
			QDateTime shown;
			// Показание часов запоминаем до первого поворота колеса и
			// дальше считаем только от него. Если считать шаг за шагом,
			// каждый час округляется к ближайшему сроку и возвращается
			// на место: при трёхчасовом прогнозе колесо крутится, а
			// картинка стоит.
			if (!wall.isValid() && map->forecastTimes (&shown, nullptr))
				wall = shown.addSecs (tz * 3600);

			tz = h;
			Settings::setUserSetting ("displayTimeZone", h);
			line->setZone (tz*3600);

			// Час на экране остаётся прежним, а показывается тот срок,
			// который в новом поясе читается так же. Точного попадания
			// может не быть: между сроками прогноза ничего нет, и берём
			// ближайший.
			if (wall.isValid() && map->hasForecast()) {
				map->showForecastNear (wall.addSecs (-tz * 3600));
				line->setIndex (map->forecastIndex());
			}
			showStamp ();
		}

		void download ()
		{
			// Область — то, что сейчас на экране. Ничего выделять не надо:
			// на телефоне это и есть самый естественный выбор.
			double x0, y0, x1, y1;
			map->projection()->getVisibleArea (&x0, &y0, &x1, &y1);
			if (x0 > x1) std::swap (x0, x1);
			if (y0 > y1) std::swap (y0, y1);

			load->setEnabled (false);
			bar->show ();
			stamp->show ();
			BarProgress pr (bar, stamp);
			QByteArray data;
			QDateTime t0 = QDateTime::currentDateTime ();
			QStringList trouble;

			for (const char *model : {"gfs_p25_", "ww3_p50_"}) {
				ForecastSource *src = SourceRegistry::instance().sourceFor (model);
				if (src == nullptr)
					continue;
				ForecastSource::Request rq {model, x0, y0, x1, y1,
				                            days->value(), step->value()};
				ForecastSource::Result r = src->fetch (rq, &data, &pr);
				if (!r.ok)
					trouble << r.error;
			}

			bool ok = false;
			if (data.size() > 100 && data.startsWith ("GRIB")) {
				// Пишем во временный файл и лишь потом подменяем прежний:
				// оборванная запись не должна съесть последний прогноз,
				// который до сих пор показывался.
				QString path = lastPath ();
				QString tmp  = path + ".part";
				QFile f (tmp);
				if (f.open (QIODevice::WriteOnly)) {
					f.write (data);
					f.close ();
					if (map->setForecast (tmp)) {
						map->setForecast (QString());
						QFile::remove (path);
						ok = QFile::rename (tmp, path) && map->setForecast (path);
					}
				}
				QFile::remove (tmp);
			}

			bar->hide ();
			bar->setValue (0);
			load->setEnabled (true);
			if (ok) {
				fillLayers ();
				fillLine ();
				showStamp ();
				if (panel->isVisible())
					togglePanel ();
			}
			else {
				say (trouble.isEmpty() ? QStringLiteral("данных нет")
				                       : trouble.first(), true);
			}
		}

		// Прогноз без даты опаснее, чем никакого: вчерашний ветер
		// выглядит на карте точно так же, как сегодняшний.
		void showStamp ()
		{
			QDateTime shown, last;
			if (!map->forecastTimes (&shown, &last)) {
				stamp->hide ();
				return;
			}
			bool stale = last < QDateTime::currentDateTimeUtc ();
			// Пояс подписываем всегда: время без пояса на судне — повод
			// разойтись с прогнозом на несколько часов.
			say (shown.toOffsetFromUtc (tz*3600).toString ("dd.MM  HH:mm")
			     + QStringLiteral("  ") + zoneName()
			     + (stale ? QStringLiteral("  · устарел") : QString()),
			     stale);
		}

	private:
		QString lastPath () const
		{
			return AppData::dataDir() + "/forecast.grb2";
		}

		// Прошлый прогноз открывается сам: в море интернета может уже
		// не быть, а последняя скачанная карта лучше пустого экрана.
		void openLast ()
		{
			QString path = lastPath ();
			if (QFile::exists (path) && map->setForecast (path)) {
				fillLayers ();
				fillLine ();
				showStamp ();
			}
		}

		QString zoneName () const
		{
			if (tz == 0)
				return QStringLiteral("UTC");
			return QStringLiteral("UTC%1%2")
			           .arg (tz < 0 ? QStringLiteral("−") : QStringLiteral("+"))
			           .arg (qAbs (tz));
		}

		// Предлагаем только то, что есть в файле: пустой слой на экране
		// выглядит как поломка, а не как отсутствие данных.
		void fillLayers ()
		{
			struct Cand { int type, lt, lv; const char *name; };
			// Уровни взяты из того, что реально приходит от NOAA:
			// температура поверхности читается как «температура воды»,
			// облачность лежит на уровне всей атмосферы.
			static const Cand all[] = {
				{ GRB_PRV_WIND_XY2D,    LV_ABOV_GND, 10, "Ветер"  },
				{ GRB_WIND_GUST,        -1,          -1, "Порыв"  },
				{ GRB_PRV_WAV_SIG,      -1,          -1, "Волны"  },
				{ GRB_TEMP,             LV_ABOV_GND,  2, "Воздух" },
				{ GRB_WTMP,             -1,          -1, "Вода"   },
				{ GRB_PRV_DIFF_TEMPDEW, LV_ABOV_GND,  2, "Туман"  },
				{ GRB_HUMID_REL,        LV_ABOV_GND,  2, "Влаж."  },
				{ GRB_CLOUD_TOT,        -1,          -1, "Облака" },
				{ GRB_PRECIP_TOT,       -1,          -1, "Дождь"  },
				{ GRB_CAPE,             -1,          -1, "Гроза"  },
			};
			QList<LayerBar::Item> have;
			for (const Cand &c : all) {
				int type = (c.type == GRB_PRV_WAV_SIG) ? GRB_WAV_SIG_HT
				                                       : c.type;
				if (map->hasField (type, c.lt, c.lv))
					have << LayerBar::Item {c.type, c.lt, c.lv,
					                        QString::fromUtf8 (c.name)};
			}
			layers->setItems (have);

			// Возвращаем слой, выбранный в прошлый раз.
			int want = Settings::getUserSetting ("phoneLayer",
			                                     GRB_PRV_WIND_XY2D).toInt();
			for (int i = 0; i < have.size(); ++i) {
				if (have.at(i).type != want)
					continue;
				layers->setCurrent (i);
				map->setColorMap (have.at(i).type, have.at(i).levelType,
				                  have.at(i).levelValue);
				break;
			}
			placeLayers ();
			showScale ();
		}

		// Шкала показывает то поле, которым сейчас крашена карта.
		void showScale ()
		{
			double lo = 0, hi = 0;
			if (!map->hasForecast() || !map->layerRange (&lo, &hi)) {
				scale->hide ();
				return;
			}
			MapView *m = map;
			scale->setLayer (map->layerType(), lo, hi,
			                 [m](double v) { return m->layerColor (v); });
			scale->show ();
			placeScale ();
		}

		void placeScale ()
		{
			int h = map->height() * 3 / 5;
			if (h < 240)
				h = 240;
			if (h > map->height() - 24)
				h = map->height() - 24;
			scale->resize (ScaleBar::WIDTH, h);
			scale->move (map->width() - ScaleBar::WIDTH,
			             (map->height() - h)/2);
			scale->raise ();
		}

		void placeLayers ()
		{
			layers->refit ();
			int y = (map->height() - layers->height())/2;
			if (y < 8)
				y = 8;
			layers->move (0, y);
			layers->raise ();
			tab->move (0, map->height()/2 - tab->height()/2);
			tab->setVisible (!layers->isVisible());
			tab->raise ();
		}

		void fillLine ()
		{
			wall = QDateTime ();
			line->setZone (tz*3600);
			line->setSteps (map->forecastSteps());
			line->setIndex (map->forecastIndex());
			bool has = map->hasForecast ();
			back->setEnabled (has);
			forward->setEnabled (has);
		}

		void say (const QString &text, bool alarm)
		{
			stamp->setStyleSheet (QStringLiteral(
			    "background: rgba(%1, 205); color: white;"
			    "padding: 7px 16px; font-size: 17px; font-weight: bold;"
			    "border-radius: 13px;")
			    .arg (alarm ? "150,45,35" : "20,40,60"));
			stamp->setText (text);
			stamp->adjustSize ();
			stamp->show ();
			placeStamp ();
		}

		void placeStamp ()
		{
			stamp->move ((map->width() - stamp->width())/2, 12);
			stamp->raise ();
		}

		QPushButton *button (const QString &text, bool accent)
		{
			QPushButton *b = new QPushButton (text);
			b->setMinimumHeight (61);
			b->setStyleSheet (QStringLiteral(
			    "font-size: %1px; font-weight: bold; border: none;"
			    "color: %2; background: %3;")
			    .arg (accent ? 18 : 20)
			    .arg (accent ? "white" : "#1a2a3a")
			    .arg (accent ? "#2d6ea8" : "#e4eaf0"));
			return b;
		}

		MapView      *map;
		QLabel       *stamp;
		TimeBar      *line;
		Wheel        *days;
		Wheel        *step;
		Wheel        *zone;
		int           tz;        // пояс показа, часов от UTC
		QDateTime     wall;      // показание часов, которое держим при
		                         // переводе пояса
		QPushButton  *back;
		QPushButton  *forward;
		QPushButton  *setup;
		QPushButton  *load;
		QWidget      *panel;
		QWidget      *buttons;
		LayerBar     *layers;
		ScaleBar     *scale;
		QPushButton  *tab;
		QProgressBar *bar;
};

#include "main.moc"

//---------------------------------------------------------------------
int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	QCoreApplication::setOrganizationName ("MAKGrib");
	QCoreApplication::setApplicationName ("MAKGrib");
	Settings::initializeSettingsDir ();

	Main w;
	w.setWindowTitle ("MAKGrib");
	w.showMaximized ();
	return app.exec ();
}
