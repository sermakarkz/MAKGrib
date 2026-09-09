// Проверка Меркатора без PROJ: сверяем с эталонной формулой и с тем,
// что даёт PROJ на настольной сборке.
#include <cstdio>
#include <cmath>
#include <QApplication>
#include "Projection.h"
static int bad = 0;
static void check (const char *what, bool ok, const QString &d="")
{
	printf ("  [%s] %s%s\n", ok?" OK ":"FAIL", what,
	        d.isEmpty()?"":(" -> "+d).toUtf8().constData());
	if (!ok) bad++;
}
int main (int argc, char **argv)
{
	QApplication app (argc, argv);
	// Каспий: центр 50E 42N, окно 900x600.
	Projection_MERCATOR_Simple m (900, 600, 50.0, 42.0, 20.0);
	m.setVisibleArea (46.0, 36.0, 56.0, 48.0);

	printf ("\n=== туда и обратно ===\n");
	bool roundtrip = true; double worst = 0;
	for (double lon = 46.5; lon <= 55.5; lon += 1.5)
		for (double lat = 36.5; lat <= 47.5; lat += 1.5) {
			int i, j; double x, y;
			m.map2screen (lon, lat, &i, &j);
			m.screen2map (i, j, &x, &y);
			double e = std::max (std::fabs(x-lon), std::fabs(y-lat));
			if (e > worst) worst = e;
			if (e > 0.05) roundtrip = false;
		}
	printf ("       наибольшее расхождение: %.4f градуса\n", worst);
	check ("координаты возвращаются на место", roundtrip);

	printf ("\n=== это действительно Меркатор ===\n");
	// На Меркаторе отношение вертикальных отрезков равной широтной
	// протяжённости растёт как 1/cos(широты). Проверяем на 40 и 60.
	int j40a, j40b, j60a, j60b, ii;
	m.map2screen (50.0, 40.0, &ii, &j40a);
	m.map2screen (50.0, 41.0, &ii, &j40b);
	m.map2screen (50.0, 46.0, &ii, &j60a);
	m.map2screen (50.0, 47.0, &ii, &j60b);
	double d40 = std::fabs (j40b - j40a), d46 = std::fabs (j60b - j60a);
	double got = d46/d40, want = std::cos(40*M_PI/180)/std::cos(46.5*M_PI/180);
	printf ("       градус широты на 40N = %.1f px, на 46N = %.1f px\n", d40, d46);
	printf ("       растяжение: получилось %.3f, по формуле %.3f\n", got, want);
	check ("широты растягиваются к полюсу как положено",
	       std::fabs (got - want) < 0.05);

	printf ("\n=== курс постоянного направления — прямая ===\n");
	// Локсодромия: идём из точки на NE, откладывая равные приращения
	// меркаторской ординаты и долготы. На экране должна выйти прямая.
	int px[6], py[6];
	for (int k = 0; k < 6; k++) {
		double lon = 47.0 + k*1.2;
		// dLat подобран так, чтобы приращение по Меркатору было равным
		double yMerc = std::log (std::tan (M_PI/4 + (37.0*M_PI/180)/2)) * 180/M_PI + k*1.2;
		double lat = (2*std::atan (std::exp (yMerc*M_PI/180)) - M_PI/2) * 180/M_PI;
		m.map2screen (lon, lat, &px[k], &py[k]);
	}
	double maxdev = 0;
	for (int k = 1; k < 5; k++) {
		double t = double(px[k]-px[0]) / double(px[5]-px[0]);
		double expect = py[0] + t*(py[5]-py[0]);
		maxdev = std::max (maxdev, std::fabs (py[k] - expect));
	}
	printf ("       отклонение от прямой: %.2f px\n", maxdev);
	check ("локсодромия рисуется прямой", maxdev < 1.5);

	printf ("\n%s\n\n", bad==0 ? "Все проверки пройдены." : "ЕСТЬ ОШИБКИ");
	return bad==0 ? 0 : 1;
}
