/**********************************************************************
MAKGrib для Android — цветовая шкала у правого края.

Без неё цвет на карте не значит ничего: зелёное — это восемь метров в
секунду или восемнадцать? Полоска показывает соответствие цвета и
величины для того слоя, который сейчас выбран, в тех единицах, что
выставлены в настройках.
***********************************************************************/
#ifndef SCALEBAR_H
#define SCALEBAR_H

#include <functional>

#include <QColor>
#include <QWidget>

class ScaleBar : public QWidget
{ Q_OBJECT
	public:
		explicit ScaleBar (QWidget *parent = nullptr);

		// Тип поля (GRB_*), границы и способ узнать цвет значения.
		void setLayer (int dataType, double lo, double hi,
		               std::function<QColor(double)> color);

		static const int WIDTH = 48;

	protected:
		void paintEvent (QPaintEvent *) override;

	private:
		// Внутри всё в единицах GRIB — кельвины, метры в секунду. На
		// экране показываем в тех, что выставлены в настройках, и
		// засечки считаем тоже в них: иначе круглый шаг в кельвинах
		// даёт на шкале «−3,3» и «6,9».
		double  toShow (double raw) const;
		double  tickStep (double pixels, double span) const;

		int    type;
		double lo, hi;
		std::function<QColor(double)> color;
};

#endif
