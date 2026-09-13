/**********************************************************************
MAKGrib для Android.

Смысл приложения — скачать погоду, пока есть интернет, и смотреть её
потом. Поэтому карта во весь экран, под ней шкала времени и четыре
кнопки: листать срок назад и вперёд, настроить и скачать. Настройки
прячутся в шторку: их трогают редко.
***********************************************************************/
#include <functional>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFont>
#include <QGeoPositionInfoSource>
#include <QLocale>
#include <QTranslator>
#include <QPermissions>
#include <QFontMetrics>
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
#include "IconButton.h"
#include "RouteTime.h"
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
		// Табличку заполняет само окно: у неё своя выкладка, и писать в
		// неё мимо — значит получить пустой прямоугольник вместо текста.
		BarProgress (QProgressBar *b, std::function<void(QString)> tell)
			: bar (b), tell (tell) {}

		void message (const QString &t) override
		{
			tell (t);
			QApplication::processEvents ();
		}

		void step (int done, int total, qint64 bytes) override
		{
			if (total > 0)
				bar->setValue ((100*done)/total);
			tell (total > 0
			      ? QObject::tr("Качаю %1 из %2 · %3 КБ")
			            .arg (done).arg (total).arg (bytes/1024)
			      : QObject::tr("Качаю · %1 КБ").arg (bytes/1024));
			QApplication::processEvents ();
		}

		bool canceled () override { return false; }

	private:
		QProgressBar *bar;
		std::function<void(QString)> tell;
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

			// Надпись вверху рисует сама карта: координаты тут висели
			// зря, а вот на какой час нарисован ветер — вопрос первый.
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

			// Круглая кнопка «моё место» поверх карты. Появляется сразу,
			// но работает, когда GPS даст первое место.
			// Значок рисуется кодом и кнопка круглая — см. IconButton.
			locate = new IconButton (IconButton::Locate, false, map);
			locate->setFixedSize (52, 52);
			locate->setRound (true);
			connect (locate, &QPushButton::clicked, this, &Main::goToOwnPos);

			// Цветовая шкала у правого края: без неё цвет на карте
			// ничего не значит.
			scale = new ScaleBar (map);
			scale->hide ();
			map->installEventFilter (this);

			days = new Wheel (tr("Глубина"), 1, 10, 3,
			                  tr("сут"));
			step = new Wheel (tr("Шаг"), 1, 12, 3,
			                  tr("ч"));

			// Сроки в GRIB всегда по Гринвичу. По умолчанию показываем в
			// поясе телефона, но в рейсе он может быть чужим — скажем,
			// судовое время отличается от того, что стоит в аппарате.
			int here = QTimeZone::systemTimeZone()
			               .offsetFromUtc (QDateTime::currentDateTime()) / 3600;
			tz = Settings::getUserSetting ("displayTimeZone", here).toInt();
			if (tz < -12 || tz > 14)
				tz = here;
			zone = new Wheel (tr("Пояс"), -12, 14, tz,
			                  QString());
			lang = new Wheel (tr("Язык"), 0, langCount()-1, 0, QString());
			lang->setFormatter ([](int k) { return langName (k); });
			connect (lang, &Wheel::valueChanged, this, &Main::setLang);
			{   // ставим колесо на выбранный ранее язык
				QString have = Settings::getUserSetting ("language", "")
				                   .toString();
				for (int i = 0; i < langCount(); ++i)
					if (langCode (i) == have) {
						lang->setValue (i);
						break;
					}
			}

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
			bar->setFixedHeight (12);
			bar->setStyleSheet (
			    "QProgressBar { border: none; background: #d2dfec; }"
			    "QProgressBar::chunk { background: #2d6ea8; }");
			bar->hide ();

			line = new TimeBar;
			connect (line, &TimeBar::moved, this, &Main::goToStep);

			// Значки рисуются кодом: готовые знаки на Android
			// подменяются цветными эмодзи, а картинки пришлось бы
			// держать в нескольких разрешениях.
			back    = new IconButton (IconButton::Back,     false);
			forward = new IconButton (IconButton::Forward,  false);
			setup   = new IconButton (IconButton::Gear,     false);
			route   = new IconButton (IconButton::Route,    false);
			load    = new IconButton (IconButton::Download, true);
			info    = new IconButton (IconButton::Info,     false);
			// Пока прогноза нет, листать нечего.
			back->setEnabled (false);
			forward->setEnabled (false);
			connect (back,    &QPushButton::clicked, this, [this]() { shift (-1); });
			connect (forward, &QPushButton::clicked, this, [this]() { shift (+1); });
			connect (setup,   &QPushButton::clicked, this, &Main::togglePanel);
			connect (route,   &QPushButton::clicked, this, &Main::toggleRoute);
			connect (load,    &QPushButton::clicked, this, &Main::download);
			connect (info,    &QPushButton::clicked, this, &Main::showInfo);

			buttons = new QWidget;
			QHBoxLayout *row = new QHBoxLayout (buttons);
			row->setContentsMargins (0, 0, 0, 0);
			row->setSpacing (1);
			row->addWidget (back,    1);
			row->addWidget (setup,   1);
			row->addWidget (route,   1);
			row->addWidget (load,    1);
			row->addWidget (info,    1);
			row->addWidget (forward, 1);

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
			wheels->addWidget (lang, 1);
			QVBoxLayout *pl = new QVBoxLayout (panel);
			pl->setContentsMargins (12, 0, 12, 10);
			pl->setSpacing (4);
			pl->addWidget (handle);
			pl->addLayout (wheels);
			panel->hide ();

			// Панель маршрута: когда она открыта, настроек не видно и
			// наоборот — обе занимают одно и то же место внизу.
			// Дата, время и ход — каждое своим колесом. Час без минут и
			// узел без десятых на переходе в сотни миль дают заметную
			// ошибку в приходе, а без даты «старт через N часов» врёт,
			// как только переставишь срок прогноза.
			dayW  = new Wheel (tr("Число"), 1, 31, 1, QString());
			monW  = new Wheel (tr("Месяц"), 1, 12, 1, QString());
			// Названия месяцев берём у локали: переводить их руками на
			// десяток языков — лишняя работа и лишние ошибки.
			monW->setFormatter ([](int k) {
				return QLocale().monthName (qBound (1, k, 12),
				                            QLocale::ShortFormat);
			});
			hourW = new Wheel (tr("Час"), 0, 23, 0, QString());
			minW  = new Wheel (tr("Мин"), 0, 11, 0, QString());
			minW->setFormatter ([](int k) {
				return QStringLiteral("%1").arg (k*5, 2, 10, QChar('0'));
			});
			speedK = new Wheel (tr("Узлы"), 1, 30, 8, QString());
			speedT = new Wheel (tr("Доли"), 0, 9, 0, QString());
			speedT->setFormatter ([](int k) {
				return QStringLiteral(",%1").arg (k);
			});
			for (Wheel *w : {dayW, monW, hourW, minW, speedK, speedT})
				connect (w, &Wheel::valueChanged, this, &Main::showRoute);

			// Три действия в один ряд: проложить, править, очистить.
			draw = new QPushButton (tr("Проложить"));
			edit = new QPushButton (tr("Править"));
			wipe = new QPushButton (tr("Очистить"));
			for (QPushButton *b : {draw, edit, wipe}) {
				b->setMinimumHeight (60);
				b->setStyleSheet ("font-size: 16px; font-weight: bold;"
				                  "color: white; background: #2d6ea8;"
				                  "border: none; border-radius: 12px;");
			}
			wipe->setStyleSheet ("font-size: 16px; border: none;"
			                     "border-radius: 12px; background: #e4eaf0;"
			                     "color: #1a2a3a;");
			connect (draw, &QPushButton::clicked,
			         this, [this]() { setMode (MapView::DrawRoute); });
			connect (edit, &QPushButton::clicked,
			         this, [this]() { setMode (MapView::EditRoute); });
			connect (wipe, &QPushButton::clicked,
			         this, [this]() { map->clearRoute(); });

			// Пока работаем с маршрутом, внизу только она: экран нужен
			// целиком, а все прочие кнопки в это время бесполезны.
			finish = new QPushButton (tr("Закончить"));
			finish->setMinimumHeight (68);
			finish->setStyleSheet ("font-size: 17px; font-weight: bold;"
			                       "color: white; background: #b03028;"
			                       "border: none;");
			finish->hide ();
			connect (finish, &QPushButton::clicked,
			         this, [this]() { setMode (MapView::NoRoute); });

			plan = new QLabel;
			plan->setStyleSheet ("font-size: 15px; color: #33414f;");
			plan->setAlignment (Qt::AlignCenter);
			// Строка расчёта длинная, а обрезать в ней нечего: время
			// прихода — последнее, что там стоит, и оно нужнее всего.
			plan->setWordWrap (true);

			rpanel = new QWidget;
			rpanel->setStyleSheet ("background: #f2f5f8;");
			QVBoxLayout *rl = new QVBoxLayout (rpanel);
			rl->setContentsMargins (12, 0, 12, 10);
			rl->setSpacing (6);
			DrawerHandle *rhandle = new DrawerHandle;
			connect (rhandle, &DrawerHandle::closeAsked,
			         this, &Main::toggleRoute);
			rl->addWidget (rhandle);
			QHBoxLayout *rw = new QHBoxLayout;
			rw->setSpacing (5);
			rw->addWidget (dayW,   1);
			rw->addWidget (monW,   1);
			rw->addWidget (hourW,  1);
			rw->addWidget (minW,   1);
			rw->addWidget (speedK, 1);
			rw->addWidget (speedT, 1);
			rl->addLayout (rw);
			rl->addWidget (plan);
			QHBoxLayout *rb = new QHBoxLayout;
			rb->setSpacing (8);
			rb->addWidget (draw, 1);
			rb->addWidget (edit, 1);
			rb->addWidget (wipe, 1);
			rl->addLayout (rb);
			rpanel->hide ();

			connect (map, &MapView::routeChanged, this, &Main::showRoute);

			QVBoxLayout *lay = new QVBoxLayout (this);
			lay->setContentsMargins (0, 0, 0, 0);
			lay->setSpacing (0);
			lay->addWidget (map, 1);
			lay->addWidget (bar);
			lay->addWidget (panel);
			lay->addWidget (rpanel);
			lay->addWidget (finish);
			lay->addWidget (line);
			lay->addWidget (buttons);

			connect (map, &MapView::forecastTimeChanged,
			         this, &Main::showStamp);
			connect (map, &MapView::forecastTimeChanged,
			         this, &Main::updateBoat);

			// Карты лежат в APK; раскладываем при первом запуске.
			if (!AppData::ready()) {
				say (tr("Раскладываю карты…"), false);
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
			startGps ();
			loadRoute ();
			openLast ();
			loadStart ();
		}

	protected:
		bool eventFilter (QObject *o, QEvent *e) override
		{
			if (o == map && e->type() == QEvent::Resize) {
				placeLayers ();
				placeScale ();
				placeOwnButton ();
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

		// Маршрут и настройки делят одно место внизу.
		void toggleRoute ()
		{
			bool show = !rpanel->isVisible () && !finish->isVisible ();
			wantRoute = show;
			if (show && panel->isVisible())
				togglePanel ();
			// Дата выхода должна лежать внутри прогноза: иначе считать
			// нечего, а на экране получается «выход 01.01».
			if (show && !startInForecast())
				resetStart ();
			if (map->routeMode() != MapView::NoRoute)
				map->setRouteMode (MapView::NoRoute);
			rpanel->setVisible (show);
			finish->hide ();
			buttons->setVisible (!show);
			line->setVisible (!show);
			showRoute ();
		}

		// Работа с маршрутом: панель уходит, внизу одна кнопка.
		void setMode (int mode)
		{
			map->setRouteMode (mode);
			// Подсказку показываем табличкой поверх карты: на кнопке она
			// не помещается, а обрезанный текст хуже, чем никакой.
			if (mode == MapView::DrawRoute)
				say (tr("Касайтесь карты — ставьте точки"), false);
			else if (mode == MapView::EditRoute)
				say (tr("Тяните точки · долгое нажатие удалит"),
				     false);
			else
				showStamp ();
			bool busy = (mode != MapView::NoRoute);
			rpanel->setVisible (!busy && wantRoute);
			finish->setVisible (busy);
			buttons->setVisible (!busy && !wantRoute && !panel->isVisible());
			line->setVisible (buttons->isVisible());
			showRoute ();
		}

		// Длина и время в пути. Скорость судовая, отсчёт — от показанного
		// срока прогноза плюс задержка старта.
		void showRoute ()
		{
			int n = map->route().size ();
			if (n < 2) {
				QString hint = tr("Маршрут не проложен");
				if (map->routeMode() == MapView::DrawRoute)
					hint = tr("Касайтесь карты — ставьте точки");
				else if (map->routeMode() == MapView::EditRoute)
					hint = tr("Касайтесь карты — ставьте точки");
				plan->setText (hint);
				updateBoat ();
				setFinishText (n == 1
				               ? tr("Закончить · %n точка(и)", "", 1)
				               : tr("Закончить"));
				saveRoute ();
				return;
			}
			double miles = map->routeMiles ();
			double knots = speedK->value() + speedT->value()/10.0;
			double hours = miles / qMax (0.1, knots);
			QDateTime go = startMoment ();
			QDateTime in = go.addSecs (qint64 (hours * 3600));
			QString when = tr(" · выход %1 · приход %2")
			                   .arg (go.toOffsetFromUtc (tz*3600)
			                           .toString ("dd.MM HH:mm"))
			                   .arg (in.toOffsetFromUtc (tz*3600)
			                           .toString ("dd.MM HH:mm"));
			int mi = int (miles + 0.5);
			int hh = int (hours);
			int mm = int ((hours - hh) * 60 + 0.5);
			saveStart ();
			updateBoat ();
			// Склонение отдаём Qt: в русском три формы, в английском две,
			// в турецком одна — руками это не сложить.
			QString pts  = tr("%n точка(и)", "", n);
			QString mls  = tr("%n миля(и)", "", mi);
			QString went = hh > 0 ? tr("%1 ч %2 мин").arg(hh).arg(mm)
			                      : tr("%1 мин").arg(mm);
			setFinishText (tr("Закончить · %1 · %2").arg (pts).arg (mls));
			plan->setText (tr("%1 · %2 · %3%4")
			                   .arg (pts).arg (mls).arg (went).arg (when));
			saveRoute ();
		}

		// Где судно к показанному сроку: прошло столько миль, сколько
		// успело от выхода со своей скоростью.
		void updateBoat ()
		{
			QDateTime shown;
			if (map->route().size() < 2
			 || !map->forecastTimes (&shown, nullptr)) {
				map->setBoatMiles (-1);
				return;
			}
			double knots = speedK->value() + speedT->value()/10.0;
			double hours = startMoment().secsTo (shown) / 3600.0;
			// До выхода судно стоит в начальной точке, после прихода —
			// в конечной: лучше показать его на месте, чем убрать.
			map->setBoatMiles (qMax (0.0, hours) * knots);
		}

		// Момент выхода. Год берём от показанного срока прогноза: на
		// экране его нет и не нужно, а маршрут прокладывают на ближайшие
		// дни, не на следующий год.
		QDateTime startMoment () const
		{
			QDateTime shown = QDateTime::currentDateTimeUtc ();
			map->forecastTimes (&shown, nullptr);
			QDate base = shown.toOffsetFromUtc (tz*3600).date ();
			int year = yearForStart (base.year(), base.month(), base.day(),
			                         monW->value(), dayW->value());
			QDate d (year, monW->value(), qMin (dayW->value(),
			         QDate (year, monW->value(), 1).daysInMonth()));
			QDateTime go (d, QTime (hourW->value(), minW->value()*5),
			              QTimeZone::fromSecondsAheadOfUtc (tz*3600));
			return go.toUTC ();
		}

		// Лежит ли выбранный выход внутри загруженного прогноза.
		bool startInForecast () const
		{
			QList<QDateTime> all = map->forecastSteps ();
			if (all.isEmpty())
				return true;         // прогноза нет — проверять нечего
			QDateTime go = startMoment ();
			return go >= all.first().addSecs (-12*3600)
			    && go <= all.last();
		}

		// Ставим колёса на показанный срок: чаще всего от него и идут.
		void resetStart ()
		{
			QDateTime shown;
			if (!map->forecastTimes (&shown, nullptr))
				return;
			QDateTime t = shown.toOffsetFromUtc (tz*3600);
			dayW->setValue (t.date().day());
			monW->setValue (t.date().month());
			hourW->setValue (t.time().hour());
			minW->setValue (t.time().minute()/5);
			saveStart ();
		}

		// Выход и ход запоминаем: маршрут прокладывают на переход, а не
		// на запуск приложения.
		void saveStart ()
		{
			QStringList v;
			for (Wheel *w : {dayW, monW, hourW, minW, speedK, speedT})
				v << QString::number (w->value());
			Settings::setUserSetting ("routeStart", v.join (","));
		}

		void loadStart ()
		{
			QStringList v = Settings::getUserSetting ("routeStart", "")
			                    .toString().split (',');
			if (v.size() != 6) {
				resetStart ();
				return;
			}
			dayW  ->setValue (v.at(0).toInt());
			monW  ->setValue (v.at(1).toInt());
			hourW ->setValue (v.at(2).toInt());
			minW  ->setValue (v.at(3).toInt());
			speedK->setValue (v.at(4).toInt());
			speedT->setValue (v.at(5).toInt());
			if (!startInForecast())
				resetStart ();
		}

		// Надпись на кнопке не должна обрезаться: если полная не влезает
		// по ширине, оставляем короткую.
		void setFinishText (const QString &full)
		{
			QFontMetrics fm (finish->font());
			finish->setText (fm.horizontalAdvance (full) < finish->width() - 40
			                 ? full : tr("Закончить"));
		}

		// Своё место: спрашиваем разрешение, потом слушаем GPS.
		void startGps ()
		{
			QLocationPermission perm;
			perm.setAccuracy (QLocationPermission::Precise);
			qApp->requestPermission (perm, this, [this](const QPermission &p) {
				if (p.status() != Qt::PermissionStatus::Granted) {
					say (tr("Без разрешения на место GPS не работает"), true);
					return;
				}
				gps = QGeoPositionInfoSource::createDefaultSource (this);
				if (gps == nullptr) {
					say (tr("GPS в этом телефоне недоступен"), true);
					return;
				}
				connect (gps, &QGeoPositionInfoSource::positionUpdated,
				         this, &Main::gotPos);
				gps->setUpdateInterval (3000);
				gps->startUpdates ();
			});
		}

		void gotPos (const QGeoPositionInfo &info)
		{
			if (!info.isValid())
				return;
			QGeoCoordinate c = info.coordinate ();
			double acc = info.hasAttribute (QGeoPositionInfo::HorizontalAccuracy)
			             ? info.attribute (QGeoPositionInfo::HorizontalAccuracy)
			             : 0.0;
			map->setOwnPos (c.longitude(), c.latitude(), acc);
		}

		// Кнопка ставит своё место в середину экрана. Пока места нет,
		// говорим об этом прямо, а не молчим.
		void goToOwnPos ()
		{
			if (!map->hasOwnPos()) {
				say (tr("Место ещё не определено"), false);
				return;
			}
			map->setCenter (map->ownPos().x(), map->ownPos().y());
		}

		void showInfo ()
		{
			say (tr("Слои — язычком слева, срок — стрелками "
			        "или шкалой, маршрут — значком с точками."), false);
		}

		void togglePanel ()
		{
			// Пока настраивают прогноз, ни кнопки, ни шкала времени не
			// нужны: место лучше отдать карте.
			bool show = !panel->isVisible ();
			if (show && rpanel->isVisible())
				toggleRoute ();
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

		// Языки: первый — «как в телефоне», остальные по списку.
		static int langCount ()   { return 13; }

		static QString langCode (int i)
		{
			static const char *c[] = {"", "ru", "en", "de", "fr", "es", "it",
			                          "tr", "az", "fa", "tk", "ar", "zh"};
			return QString::fromLatin1 (c[qBound (0, i, langCount()-1)]);
		}

		static QString langName (int i)
		{
			static const char *n[] = {"авто", "Рус", "Eng", "Deu", "Fra",
			                          "Esp", "Ita", "Tür", "Aze", "فا",
			                          "Tkm", "عر", "中文"};
			return QString::fromUtf8 (n[qBound (0, i, langCount()-1)]);
		}

		// Язык меняется без перезапуска только частично: надписи, уже
		// нарисованные, перерисуются, а выложенные подписи колёс — нет.
		// Поэтому просим перезапустить.
		void setLang (int i)
		{
			Settings::setUserSetting ("language", langCode (i));
			say (tr("Язык сменится при следующем запуске"), false);
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
			BarProgress pr (bar, [this](const QString &t) { say (t, false); });
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
				say (trouble.isEmpty() ? tr("данных нет")
				                       : trouble.first(), true);
			}
		}

		// Прогноз без даты опаснее, чем никакого: вчерашний ветер
		// выглядит на карте точно так же, как сегодняшний.
		void showStamp ()
		{
			QDateTime shown, last;
			if (!map->forecastTimes (&shown, &last)) {
				say (QString(), false);
				return;
			}
			bool stale = last < QDateTime::currentDateTimeUtc ();
			// Пояс подписываем всегда: время без пояса на судне — повод
			// разойтись с прогнозом на несколько часов.
			say (shown.toOffsetFromUtc (tz*3600).toString ("dd.MM  HH:mm")
			     + QStringLiteral("  ") + zoneName()
			     + (stale ? tr("  · устарел") : QString()),
			     stale);
		}

	private:
		// Маршрут переживает выход из приложения: его прокладывают один
		// раз на переход, а не на запуск.
		void saveRoute ()
		{
			// Числа пишем через QString::number: arg(double) на русской
			// локали ставит десятичную запятую — ту же, что разделяла бы
			// долготу и широту, и при чтении строка рассыпалась.
			QStringList out;
			for (const QPointF &p : map->route())
				out << QStringLiteral("%1 %2")
				        .arg (QString::number (p.x(), 'f', 6),
				              QString::number (p.y(), 'f', 6));
			Settings::setUserSetting ("route", out.join (";"));
		}

		void loadRoute ()
		{
			QList<QPointF> pts;
			QString sv = Settings::getUserSetting ("route", "").toString();
			for (const QString &one : sv.split (';', Qt::SkipEmptyParts)) {
				QStringList xy = one.simplified().split (' ');
				if (xy.size() == 2)
					pts << QPointF (xy.at(0).toDouble(), xy.at(1).toDouble());
			}
			if (!pts.isEmpty())
				map->setRoute (pts);
		}

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

		void placeOwnButton ()
		{
			locate->move (map->width() - 52 - 14, map->height() - 52 - 14);
			locate->raise ();
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

		// Табличку вверху рисует карта: надпись со стилевым фоном поверх
		// неё жила в других координатах и обрезала текст.
		void say (const QString &text, bool alarm)
		{
			map->setStamp (text, alarm);
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
		TimeBar      *line;
		Wheel        *days;
		Wheel        *step;
		Wheel        *zone;
		Wheel        *lang;
		int           tz;        // пояс показа, часов от UTC
		QDateTime     wall;      // показание часов, которое держим при
		                         // переводе пояса
		IconButton   *back;
		IconButton   *forward;
		IconButton   *setup;
		IconButton   *route;
		IconButton   *load;
		IconButton   *info;
		Wheel        *dayW;
		Wheel        *monW;
		Wheel        *hourW;
		Wheel        *minW;
		Wheel        *speedK;
		Wheel        *speedT;
		QPushButton  *draw;
		QPushButton  *edit;
		QPushButton  *wipe;
		QPushButton  *finish;
		bool          wantRoute = false;
		QLabel       *plan;
		QWidget      *rpanel;
		QWidget      *panel;
		QWidget      *buttons;
		LayerBar     *layers;
		ScaleBar     *scale;
		QPushButton  *tab;
		IconButton   *locate;
		QGeoPositionInfoSource *gps = nullptr;
		QProgressBar *bar;
};

#include "main.moc"

//---------------------------------------------------------------------
// Переводы лежат рядом с картами, внутри APK. Их два: свой, для надписей
// приложения, и движка — из него приходят единицы вроде «м/с».
static void loadTranslations (QApplication &app)
{
	QString code = Settings::getUserSetting ("language", "").toString();
	if (code.isEmpty())
		code = QLocale::system().name().left (2);
	QLocale::setDefault (QLocale (code));

	QString dir = AppData::dataDir() + "/data/tr/";
	static QTranslator own, engine;
	if (own.load ("makgrib_" + code, dir))
		app.installTranslator (&own);
	if (engine.load ("xyGrib_" + code, dir))
		app.installTranslator (&engine);
}

//---------------------------------------------------------------------
int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	QCoreApplication::setOrganizationName ("MAKGrib");
	QCoreApplication::setApplicationName ("MAKGrib");
	Settings::initializeSettingsDir ();
	// Сначала доложить новые файлы из APK, потом читать переводы: иначе
	// после обновления приложения они останутся от прошлой сборки.
	if (AppData::ready())
		AppData::syncNew ();
	loadTranslations (app);

	Main w;
	w.setWindowTitle ("MAKGrib");
	w.showMaximized ();
	return app.exec ();
}
