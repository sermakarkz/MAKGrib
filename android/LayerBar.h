/**********************************************************************
MAKGrib для Android — выбор слоя карты.

Узкая шторка у левого края: значок на каждое поле, которое есть в
скачанном файле. Значки рисуются кодом, а не берутся картинками — на
Android готовые знаки вроде «☁» подменяются цветными эмодзи, да и
собственный рисунок читается лучше на мелком размере.
***********************************************************************/
#ifndef LAYERBAR_H
#define LAYERBAR_H

#include <QList>
#include <QWidget>

class LayerBar : public QWidget
{ Q_OBJECT
	public:
		struct Item {
			int     type;        // код поля, GRB_*
			int     levelType;   // -1 — любой уровень
			int     levelValue;
			QString name;        // короткая подпись под значком
		};

		explicit LayerBar (QWidget *parent = nullptr);

		void setItems (const QList<Item> &items);
		void setCurrent (int index);
		void refit ();           // пересчитать под текущий размер карты
		int  count () const   { return list.size(); }
		const Item &item (int i) const  { return list.at(i); }

		static const int WIDTH = 78;

	signals:
		void chosen (int index);

	protected:
		void paintEvent (QPaintEvent *) override;
		void mouseReleaseEvent (QMouseEvent *) override;

	private:
		void drawIcon (QPainter &p, int type, const QRectF &box,
		               const QColor &ink);

		QList<Item> list;
		int  cur;
		int  cell;               // высота ячейки, подгоняется под экран
};

#endif
