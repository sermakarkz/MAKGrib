/**********************************************************************
MAKGrib для Android — разведочная сборка.

Задача этого этапа не в красоте, а в том, чтобы доказать: движок
собирается под ARM, нативные библиотеки линкуются, сеть с шифрованием
работает, GRIB читается. Всё это видно на одном экране с бегущим
отчётом. Интерфейс под палец придёт следующим шагом, на этот же движок.
***********************************************************************/
#include <QApplication>
#include <QDateTime>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "ForecastSource.h"
#include "SourceRegistry.h"
#include "GribReader.h"
#include "Projection.h"
#include "Settings.h"
#include "Util.h"
#include "zuFile.h"

//---------------------------------------------------------------------
// Отчёт о ходе загрузки прямо в окно.
class ScreenProgress : public ForecastProgress
{
	public:
		explicit ScreenProgress (QPlainTextEdit *v) : view (v), last (-1) {}
		void message (const QString &t) override { say (t); }
		void step (int done, int total, qint64 bytes) override
		{
			int pc = (total > 0) ? (100*done)/total : 0;
			if (pc == last)
				return;                 // не сорим одинаковыми строками
			last = pc;
			// В QString::arg удвоенный процент не экранируется, поэтому
			// знак дописываем отдельно.
			say (QString("  %1%  %2 КБ").arg(pc).arg(bytes/1024));
		}
		bool canceled () override { return false; }
	private:
		void say (const QString &t)
		{
			view->appendPlainText (t);
			view->verticalScrollBar()->setValue (
			        view->verticalScrollBar()->maximum());
			QApplication::processEvents ();
		}
		QPlainTextEdit *view;
		int last;
};

//---------------------------------------------------------------------
class Probe : public QWidget
{
	public:
		Probe ()
		{
			view = new QPlainTextEdit;
			view->setReadOnly (true);
			QFont f = view->font ();
			f.setPointSize (11);
			view->setFont (f);

			bt = new QPushButton (QStringLiteral("Скачать прогноз с NOAA"));
			bt->setMinimumHeight (72);          // под палец, а не под мышь
			connect (bt, &QPushButton::clicked, this, &Probe::run);

			QVBoxLayout *lay = new QVBoxLayout (this);
			lay->addWidget (view, 1);
			lay->addWidget (bt);

			hello ();
		}

	private:
		void say (const QString &t)
		{
			view->appendPlainText (t);
			view->verticalScrollBar()->setValue (
			        view->verticalScrollBar()->maximum());
			QApplication::processEvents ();
		}

		void hello ()
		{
			say (QStringLiteral("MAKGrib — разведочная сборка под Android"));
			say (QStringLiteral("Qt %1, сборка %2")
			     .arg (qVersion()).arg (QSysInfo::buildAbi()));
			say (QStringLiteral("устройство: %1 %2")
			     .arg (QSysInfo::prettyProductName())
			     .arg (QSysInfo::currentCpuArchitecture()));
			say ("");

			// Проекция без PROJ — та, на которой будет карта.
			Projection_MERCATOR_Simple m (600, 800, 50.0, 42.0, 20.0);
			m.setVisibleArea (46, 36, 56, 48);
			int i, j;  double lon, lat;
			m.map2screen (50.0, 42.0, &i, &j);
			m.screen2map (i, j, &lon, &lat);
			say (QStringLiteral("Меркатор без PROJ: 50.0/42.0 -> %1,%2 -> %3/%4")
			     .arg(i).arg(j).arg(lon,0,'f',3).arg(lat,0,'f',3));

			for (const SourceRegistry::Offer &o : SourceRegistry::instance().offers())
				say (QStringLiteral("источник: %1 — %2")
				     .arg (o.source->name()).arg (o.model.label));
			say ("");
			say (QStringLiteral("Нажмите кнопку, чтобы проверить сеть и чтение GRIB."));
		}

		void run ()
		{
			bt->setEnabled (false);
			view->clear ();
			// Небольшой квадрат в открытом море: и атмосфера, и волнение.
			const double x0 = 96, y0 = -42, x1 = 104, y1 = -34;

			ScreenProgress pr (view);
			QByteArray data;
			QDateTime t0 = QDateTime::currentDateTime ();

			for (const char *model : {"gfs_p25_", "ww3_p50_"}) {
				ForecastSource *src = SourceRegistry::instance().sourceFor (model);
				if (src == nullptr) {
					say (QStringLiteral("нет источника для %1").arg (model));
					continue;
				}
				say (QStringLiteral("--- %1 ---").arg (model));
				ForecastSource::Request rq {model, x0, y0, x1, y1, 1, 12};
				ForecastSource::Result r = src->fetch (rq, &data, &pr);
				say (r.ok ? QStringLiteral("получено, расчёт %1").arg (r.run)
				          : QStringLiteral("не вышло: %1").arg (r.error));
			}

			say ("");
			say (QStringLiteral("всего %1 КБ за %2 с").arg (data.size()/1024)
			     .arg (t0.secsTo (QDateTime::currentDateTime())));

			if (data.size() > 100 && data.startsWith ("GRIB")) {
				// Пишем во внутреннюю память приложения: разрешений не надо.
				QString dir = QStandardPaths::writableLocation (
				        QStandardPaths::AppDataLocation);
				QDir().mkpath (dir);
				QString path = dir + "/probe.grb2";
				QFile f (path);
				if (f.open (QIODevice::WriteOnly)) {
					f.write (data);
					f.close ();
				}
				int n = 0;
				ZUFILE *zf = zu_open (qPrintable(path), "rb", ZU_COMPRESS_AUTO);
				if (zf != nullptr) {
					GribReader r;
					n = r.countGribRecords (zf);
					zu_close (zf);
				}
				say (QStringLiteral("файл: %1").arg (path));
				say (QStringLiteral("записей GRIB: %1").arg (n));
				say (n > 0 ? QStringLiteral("ЧТЕНИЕ РАБОТАЕТ")
				           : QStringLiteral("прочитать не удалось"));
			}
			else {
				say (QStringLiteral("данных нет"));
			}
			bt->setEnabled (true);
		}

		QPlainTextEdit *view;
		QPushButton    *bt;
};

//---------------------------------------------------------------------
int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	QCoreApplication::setOrganizationName ("MAKGrib");
	QCoreApplication::setApplicationName ("MAKGrib");
	Settings::initializeSettingsDir ();

	Probe w;
	w.setWindowTitle ("MAKGrib");
	w.showMaximized ();
	return app.exec ();
}
