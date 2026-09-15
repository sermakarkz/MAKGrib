#include "DwdTiles.h"

#include <cmath>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

// Плитки лежат в выпуске с постоянной меткой: ссылка не меняется, искать
// приложению ничего не надо.
static const char *BASE =
    "https://github.com/sermakarkz/MAKGrib/releases/download/dwd/";

//-------------------------------------------------------------------------
DwdTiles::DwdTiles (QNetworkAccessManager *n)
	: net (n)
{
}

//-------------------------------------------------------------------------
// Простое скачивание с ожиданием: запросов тут единицы, и все они мелкие.
QByteArray DwdTiles::get (const QString &url)
{
	QNetworkRequest rq { QUrl (url) };
	rq.setTransferTimeout (60000);
	rq.setAttribute (QNetworkRequest::RedirectPolicyAttribute,
	                 QNetworkRequest::NoLessSafeRedirectPolicy);
	QNetworkReply *r = net->get (rq);
	QEventLoop wait;
	QObject::connect (r, &QNetworkReply::finished, &wait, &QEventLoop::quit);
	wait.exec ();
	QByteArray body;
	if (r->error() == QNetworkReply::NoError)
		body = r->readAll ();
	else
		trouble = r->errorString ();
	r->deleteLater ();
	return body;
}

//-------------------------------------------------------------------------
bool DwdTiles::refresh (bool force)
{
	// Плитки обновляются дважды в сутки — чаще раза в час спрашивать
	// незачем, а в роуминге каждый лишний запрос виден в счёте.
	if (!force && !list.isEmpty() && fetched.isValid()
	 && fetched.secsTo (QDateTime::currentDateTimeUtc()) < 3600)
		return true;

	QByteArray body = get (QString (BASE) + "manifest-v1.json");
	if (body.isEmpty())
		return false;
	QJsonObject root = QJsonDocument::fromJson (body).object();
	if (root.value ("version").toInt() != 1)
		return false;               // опись новее, чем мы понимаем

	QList<Set> got;
	for (const QJsonValue &v : root.value ("sets").toArray()) {
		QJsonObject o = v.toObject();
		Set s;
		s.id       = o.value ("id").toString();
		s.title    = o.value ("title").toString();
		s.model    = o.value ("model").toString();
		s.kind     = o.value ("kind").toString();
		s.run      = o.value ("run").toString();
		s.west     = o.value ("west").toDouble();
		s.east     = o.value ("east").toDouble();
		s.south    = o.value ("south").toDouble();
		s.north    = o.value ("north").toDouble();
		s.tile     = o.value ("tile").toInt();
		s.days     = o.value ("days").toInt();
		s.hours    = o.value ("hours").toInt();
		s.interval = o.value ("interval").toInt();
		s.index    = o.value ("index").toString();
		for (const QJsonValue &f : o.value ("fields").toArray())
			s.fields << f.toString();
		if (s.id.isEmpty() || s.tile <= 0)
			continue;
		// Сохраняем уже прочитанные размеры, если выпуск тот же: опись
		// набора весит килобайты, но в роуминге и они лишние.
		for (const Set &old : list)
			if (old.id == s.id && old.run == s.run && old.detailed) {
				s.sizes = old.sizes;
				s.detailed = true;
			}
		got << s;
	}
	if (got.isEmpty())
		return false;
	list = got;
	fetched = QDateTime::currentDateTimeUtc ();
	return true;
}

//-------------------------------------------------------------------------
bool DwdTiles::loadIndex (Set &s)
{
	if (s.detailed)
		return true;
	QByteArray body = get (QString (BASE) + s.index);
	if (body.isEmpty())
		return false;
	QJsonObject o = QJsonDocument::fromJson (body).object()
	                    .value ("sizes").toObject();
	s.sizes.clear ();
	for (auto it = o.begin(); it != o.end(); ++it) {
		QList<qint64> row;
		for (const QJsonValue &v : it.value().toArray())
			row << (qint64) v.toDouble();
		s.sizes.insert (it.key(), row);
	}
	s.detailed = !s.sizes.isEmpty();
	return s.detailed;
}

//-------------------------------------------------------------------------
QString DwdTiles::key (double lon, double lat, int tile)
{
	// Имя квадрата — по его юго-западному углу, ровно как в нарезалке.
	int lo = (int) std::floor (lon / tile) * tile;
	int la = (int) std::floor (lat / tile) * tile;
	return QString ("%1%2%3%4")
	           .arg (la >= 0 ? "N" : "S")
	           .arg (std::abs (la), 2, 10, QChar ('0'))
	           .arg (lo >= 0 ? "E" : "W")
	           .arg (std::abs (lo), 3, 10, QChar ('0'));
}

//-------------------------------------------------------------------------
// Долгота, приведённая к обычному виду: [-180, 180).
//
// Карта долготу не нормирует — она копит её при прокрутке, и экран на
// 180-м меридиане приходит как 175..185, а после долгого хода на восток
// и вовсе как 350..370. Плитки же названы по долготе в обычном виде.
// Без приведения приложение спрашивало бы «E180» там, где лежит «W180»,
// и половина экрана оставалась бы пустой — как раз на Чукотке и в
// Беринговом проливе.
static double wrapLon (double lon)
{
	while (lon >= 180.0)  lon -= 360.0;
	while (lon < -180.0)  lon += 360.0;
	return lon;
}

//-------------------------------------------------------------------------
bool DwdTiles::covers (const Set &s, double x0, double y0,
                       double x1, double y1) const
{
	if (y1 < s.south || y0 > s.north)
		return false;
	// Участок мог достаться со сдвигом на целые обороты, а то и
	// перехлёстывать 180-й меридиан. Смотрим и сам прямоугольник, и его
	// двойник через оборот: хоть один да ляжет на область набора.
	double a = wrapLon (x0);
	double b = a + (x1 - x0);
	if (!(b < s.west || a > s.east))
		return true;
	return !(b - 360.0 < s.west || a - 360.0 > s.east);
}

//-------------------------------------------------------------------------
QList<QString> DwdTiles::tilesFor (const Set &s, double x0, double y0,
                                   double x1, double y1) const
{
	QList<QString> out;
	if (s.tile <= 0)
		return out;
	// Считаем от приведённой долготы, а дальше идём с шагом плитки:
	// если участок перехлёстывает 180-й меридиан, следующий квадрат за
	// 175° это -180°, и приведение на каждом шаге это учитывает.
	const double west = wrapLon (x0);
	const double span = std::min (360.0, x1 - x0);
	double lo0 = std::floor (west / s.tile) * s.tile;
	double la0 = std::floor (y0 / s.tile) * s.tile;
	for (double la = la0; la <= y1; la += s.tile)
		for (double lo = lo0; lo <= west + span; lo += s.tile) {
			QString k = key (wrapLon (lo), la, s.tile);
			// Квадратов без воды не нарезают вовсе: над Сахарой ветер
			// есть, а смысла в нём нет.
			if (s.sizes.contains (k) && !out.contains (k))
				out << k;
		}
	return out;
}

//-------------------------------------------------------------------------
qint64 DwdTiles::weigh (Set &s, double x0, double y0, double x1, double y1,
                        int days)
{
	if (!covers (s, x0, y0, x1, y1))
		return 0;
	if (!loadIndex (s))
		return -1;               // опись не далась — веса не знаем
	qint64 total = 0;
	for (const QString &k : tilesFor (s, x0, y0, x1, y1)) {
		const QList<qint64> &row = s.sizes[k];
		for (int d = 0; d < row.size() && d < days; ++d)
			total += row[d];
	}
	return total;
}

//-------------------------------------------------------------------------
int DwdTiles::fetch (Set &s, double x0, double y0, double x1, double y1,
                     int days, QByteArray *out,
                     const std::function<void(int,int,qint64)> &tell)
{
	if (!covers (s, x0, y0, x1, y1) || !loadIndex (s))
		return 0;
	QList<QString> want = tilesFor (s, x0, y0, x1, y1);
	// Сколько файлов всего: по ним и считается ход загрузки.
	int total = 0;
	for (const QString &k : want) {
		const QList<qint64> &row = s.sizes[k];
		for (int d = 0; d < row.size() && d < days; ++d)
			if (row[d] > 0)
				total++;
	}

	int done = 0, taken = 0;
	qint64 bytes = 0;
	for (const QString &k : want) {
		const QList<qint64> &row = s.sizes[k];
		for (int d = 0; d < row.size() && d < days; ++d) {
			if (row[d] <= 0)
				continue;
			QString name = QString ("%1_%2_d%3.grb2")
			                   .arg (s.id).arg (k).arg (d + 1);
			QByteArray part = get (QString (BASE) + name);
			done++;
			bytes += part.size();
			if (tell)
				tell (done, total, bytes);
			// Плитка могла не дойти — это не беда: на её месте останется
			// дырка, а соседи нарисуются. Прогноз лучше неполный, чем
			// никакой.
			if (part.startsWith ("GRIB")) {
				out->append (part);
				taken++;
			}
		}
	}
	return taken;
}
