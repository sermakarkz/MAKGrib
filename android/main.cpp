/**********************************************************************
MAKGrib для Android.

Смысл приложения — скачать погоду, пока есть интернет, и смотреть её
потом. Поэтому на виду ровно одна кнопка, «Скачать», а глубина и шаг
спрятаны в шторку: их трогают редко. Последний скачанный прогноз
открывается сам при следующем запуске — в море интернета может уже и
не быть.
***********************************************************************/
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "AppData.h"
#include "MapView.h"
#include "Wheel.h"

#include "ForecastSource.h"
#include "SourceRegistry.h"
#include "Font.h"
#include "Projection.h"
#include "Settings.h"
#include "Util.h"

// Круглые кнопки поверх карты: палец уверенно попадает примерно в
// сантиметр, на плотном экране телефона это около семидесяти точек.
static const int OVER = 76;
static const int GAP  = 16;

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
class Main : public QWidget
{ Q_OBJECT
	public:
		Main ()
		{
			map = new MapView;

			// Координаты и состояние прогноза — плавающими табличками
			// поверх карты: полоса во всю ширину читалась бы как часть
			// карты и сбивала с толку, когда карта едет, а она стоит.
			where = chip ("background: rgba(20,40,60,190); color: white;");
			stamp = chip ("background: rgba(20,40,60,190); color: white;");
			stamp->hide ();

			// Единственная кнопка поверх карты — шторка. Масштаб делает
			// щипок, отдельные «+» и «−» оказались лишними.
			toggle = overlay ("▲");
			connect (toggle, &QPushButton::clicked, this, &Main::togglePanel);
			map->installEventFilter (this);

			days = new Wheel (QStringLiteral("Глубина"), 1, 10, 3,
			                  QStringLiteral("сут"));
			step = new Wheel (QStringLiteral("Шаг"), 1, 12, 3,
			                  QStringLiteral("ч"));

			// Кнопка стоит там, где раньше висела подсказка: место у
			// нижнего края — единственное, куда палец дотягивается не
			// перехватывая телефон.
			load = new QPushButton (QStringLiteral("Скачать погоду"));
			load->setMinimumHeight (84);
			load->setStyleSheet ("font-size: 20px; font-weight: bold;"
			                     "color: white; background: #2d6ea8;"
			                     "border: none;");
			connect (load, &QPushButton::clicked, this, &Main::download);

			bar = new QProgressBar;    bar->setRange (0, 100);
			bar->setTextVisible (false);
			bar->setMaximumHeight (6);
			bar->hide ();

			// Шторка: только колёса и только когда её открыли.
			panel = new QWidget;
			panel->setStyleSheet ("background: #f2f5f8;");
			QHBoxLayout *pl = new QHBoxLayout (panel);
			pl->setContentsMargins (12, 8, 12, 10);
			pl->setSpacing (12);
			pl->addWidget (days, 1);
			pl->addWidget (step, 1);
			panel->hide ();

			QVBoxLayout *lay = new QVBoxLayout (this);
			lay->setContentsMargins (0, 0, 0, 0);
			lay->setSpacing (0);
			lay->addWidget (map, 1);
			lay->addWidget (bar);
			lay->addWidget (panel);
			lay->addWidget (load);

			connect (map, &MapView::viewChanged, this, &Main::showWhere);

			// Карты лежат в APK; раскладываем при первом запуске.
			if (!AppData::ready()) {
				where->setText (QStringLiteral("Раскладываю карты…"));
				where->adjustSize ();
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
			Font::loadAllFonts ();
			map->loadMaps ();
			showWhere (50.5, 42.0, 0);
			openLast ();
		}

	protected:
		// Кнопки лежат на карте и её же дети, поэтому раскладывать их
		// приходится самим — и заново всякий раз, когда карта меняет
		// размер: при открытии шторки она становится ниже.
		bool eventFilter (QObject *o, QEvent *e) override
		{
			if (o == map && e->type() == QEvent::Resize)
				placeOverlays ();
			return QWidget::eventFilter (o, e);
		}

	private slots:
		void showWhere (double lon, double lat, double)
		{
			where->setText (QStringLiteral("%1  %2")
			        .arg (Util::formatLongitude (lon))
			        .arg (Util::formatLatitude (lat)));
			where->adjustSize ();
			placeChips ();
		}

		void togglePanel ()
		{
			bool show = !panel->isVisible ();
			panel->setVisible (show);
			// Треугольники берём крупные (U+25B2/25BC): мелких вариантов
			// нет в шрифте телефона, вместо них рисуется пустой квадрат.
			toggle->setText (show ? QStringLiteral("▼")
			                      : QStringLiteral("▲"));
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
				showStamp (QStringLiteral("%1 КБ за %2 с · ")
				               .arg (data.size()/1024)
				               .arg (t0.secsTo (QDateTime::currentDateTime())));
				if (panel->isVisible())
					togglePanel ();
			}
			else {
				say (trouble.isEmpty() ? QStringLiteral("данных нет")
				                       : trouble.first(), true);
			}
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
			if (QFile::exists (path) && map->setForecast (path))
				showStamp (QString());
		}

		// Прогноз без даты опаснее, чем никакого: вчерашний ветер
		// выглядит на карте точно так же, как сегодняшний.
		void showStamp (const QString &prefix)
		{
			QDateTime shown, last;
			if (!map->forecastTimes (&shown, &last)) {
				stamp->hide ();
				return;
			}
			bool stale = last < QDateTime::currentDateTimeUtc ();
			say (stale
			     ? QStringLiteral("прогноз устарел · был до %1")
			           .arg (last.toLocalTime().toString ("dd.MM HH:mm"))
			     : prefix + QStringLiteral("на %1 · есть до %2")
			           .arg (shown.toLocalTime().toString ("dd.MM HH:mm"))
			           .arg (last.toLocalTime().toString ("dd.MM HH:mm")),
			     stale);
		}

		void say (const QString &text, bool alarm)
		{
			stamp->setStyleSheet (QStringLiteral(
			    "background: rgba(%1, 200); color: white;"
			    "padding: 6px 14px; font-size: 15px; border-radius: 12px;")
			    .arg (alarm ? "150,45,35" : "20,40,60"));
			stamp->setText (text);
			stamp->adjustSize ();
			stamp->show ();
			placeChips ();
		}

		QLabel *chip (const QString &colors)
		{
			QLabel *l = new QLabel (map);
			l->setStyleSheet (colors + "padding: 6px 14px;"
			                           "font-size: 15px; border-radius: 12px;");
			l->setAlignment (Qt::AlignCenter);
			l->setAttribute (Qt::WA_TransparentForMouseEvents);
			return l;
		}

		void placeChips ()
		{
			where->move ((map->width() - where->width())/2, 12);
			stamp->move ((map->width() - stamp->width())/2,
			             12 + where->height() + 6);
			where->raise ();
			stamp->raise ();
		}

		QPushButton *overlay (const QString &text)
		{
			QPushButton *b = new QPushButton (text, map);
			b->setFixedSize (OVER, OVER);
			b->setStyleSheet (
			    "font-size: 32px; font-weight: bold; color: white;"
			    "background: rgba(20,40,60,170);"
			    "border: none; border-radius: 38px;");
			return b;
		}

		void placeOverlays ()
		{
			toggle->move (GAP, map->height() - OVER - GAP);
			toggle->raise ();
			placeChips ();
		}

		MapView      *map;
		QLabel       *where;
		QLabel       *stamp;
		Wheel        *days;
		Wheel        *step;
		QPushButton  *load;
		QPushButton  *toggle;
		QWidget      *panel;
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
