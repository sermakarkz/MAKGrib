/**********************************************************************
MAKGrib для Android.

Один экран: карта во всю площадь и выдвижная полоса снизу. Настольных
меню, панелей и одиннадцати диалогов здесь нет и не будет — на телефоне
в них не попасть пальцем.
***********************************************************************/
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include "AppData.h"
#include "MapView.h"

#include "ForecastSource.h"
#include "SourceRegistry.h"
#include "Font.h"
#include "Projection.h"
#include "Settings.h"
#include "Util.h"

//---------------------------------------------------------------------
// Ход загрузки — в полосу и надпись внизу экрана.
class BarProgress : public ForecastProgress
{
	public:
		BarProgress (QProgressBar *b, QLabel *l) : bar (b), lab (l) {}
		void message (const QString &t) override
		{
			lab->setText (t);
			QApplication::processEvents ();
		}
		void step (int done, int total, qint64 bytes) override
		{
			if (total > 0)
				bar->setValue ((100*done)/total);
			lab->setText (QStringLiteral("%1 КБ").arg (bytes/1024));
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

			// Координаты — плавающей табличкой поверх карты, а не
			// полосой над ней: полоса во всю ширину читается как часть
			// карты и сбивает с толку, когда карта едет, а она стоит.
			where = new QLabel (map);
			where->setStyleSheet (
			    "background: rgba(20,40,60,190); color: white;"
			    "padding: 8px 16px; font-size: 16px; border-radius: 14px;");
			where->setAlignment (Qt::AlignCenter);
			where->setAttribute (Qt::WA_TransparentForMouseEvents);

			days = new QSpinBox;      days->setRange (1, 10);  days->setValue (3);
			days->setSuffix (QStringLiteral(" сут"));
			step = new QSpinBox;      step->setRange (1, 12);  step->setValue (3);
			step->setSuffix (QStringLiteral(" ч"));
			for (QSpinBox *s : {days, step}) {
				s->setMinimumHeight (64);
				s->setStyleSheet ("font-size: 18px;");
			}

			load = new QPushButton (QStringLiteral("Скачать на эту область"));
			load->setMinimumHeight (76);
			load->setStyleSheet ("font-size: 18px;");
			connect (load, &QPushButton::clicked, this, &Main::download);

			bar = new QProgressBar;    bar->setRange (0, 100);
			bar->setTextVisible (false);
			bar->setMaximumHeight (8);
			note = new QLabel;
			note->setStyleSheet ("font-size: 15px;");

			QHBoxLayout *row = new QHBoxLayout;
			row->addWidget (new QLabel (QStringLiteral("Глубина")));
			row->addWidget (days, 1);
			row->addWidget (new QLabel (QStringLiteral("Шаг")));
			row->addWidget (step, 1);

			QVBoxLayout *lay = new QVBoxLayout (this);
			lay->setContentsMargins (0, 0, 0, 0);
			lay->setSpacing (0);
			lay->addWidget (map, 1);
			QWidget *panel = new QWidget;
			QVBoxLayout *pl = new QVBoxLayout (panel);
			pl->addLayout (row);
			pl->addWidget (load);
			pl->addWidget (bar);
			pl->addWidget (note);
			lay->addWidget (panel);

			connect (map, &MapView::viewChanged, this, &Main::showWhere);

			// Карты лежат в APK; раскладываем при первом запуске.
			if (!AppData::ready()) {
				where->setText (QStringLiteral("Раскладываю карты…"));
				load->setEnabled (false);
				QApplication::processEvents ();
				AppData::unpack ([this](int pc) {
					bar->setValue (pc);
					QApplication::processEvents ();
				});
				load->setEnabled (true);
			}
			Settings::findAppDataDir ();
			Font::loadAllFonts ();
			map->loadMaps ();
			showWhere (50.5, 42.0, 0);
			note->setText (QStringLiteral("Тяните карту пальцем, "
			                              "щипком меняйте масштаб."));
		}

	private slots:
		void showWhere (double lon, double lat, double)
		{
			where->setText (QStringLiteral("%1  %2")
			        .arg (Util::formatLongitude (lon))
			        .arg (Util::formatLatitude (lat)));
			where->adjustSize ();
			// Держим по центру сверху, с отступом от края экрана.
			where->move ((map->width() - where->width())/2, 14);
			where->raise ();
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
			BarProgress pr (bar, note);
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

			if (data.size() > 100 && data.startsWith ("GRIB")) {
				QString path = AppData::dataDir() + "/forecast.grb2";
				QFile f (path);
				if (f.open (QIODevice::WriteOnly)) {
					f.write (data);
					f.close ();
				}
				bool ok = map->setForecast (path);
				note->setText (ok
				    ? QStringLiteral("%1 КБ за %2 с")
				          .arg (data.size()/1024)
				          .arg (t0.secsTo (QDateTime::currentDateTime()))
				    : QStringLiteral("файл получен, но не прочитался"));
			}
			else {
				note->setText (trouble.isEmpty()
				    ? QStringLiteral("данных нет")
				    : trouble.first());
			}
			bar->setValue (0);
			load->setEnabled (true);
		}

	private:
		MapView      *map;
		QLabel       *where;
		QLabel       *note;
		QSpinBox     *days;
		QSpinBox     *step;
		QPushButton  *load;
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
