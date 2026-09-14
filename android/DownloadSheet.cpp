#include "DownloadSheet.h"
#include "Wheel.h"

#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

//=========================================================================
// Строка источника. Готовый флажок Qt на телефоне выходит в четверть
// пальца шириной — попасть по нему в качку нельзя. Поэтому вся строка
// и есть переключатель, а квадратик рисуется руками.
//=========================================================================
class SourceRow : public QWidget
{ Q_OBJECT
	public:
		SourceRow (const DownloadSheet::Source &s, QWidget *parent)
			: QWidget (parent), src (s)
		{
			setMinimumHeight (74);
			setSizePolicy (QSizePolicy::Preferred, QSizePolicy::Fixed);
			if (!src.here)
				src.on = false;
		}

		bool taken () const     { return src.here && src.on; }
		bool here () const      { return src.here; }
		const QString &id () const  { return src.id; }

		void setWeight (qint64 b, bool known)
		{
			bytes = b; weighed = known;
			update ();
		}

	signals:
		void toggled ();

	protected:
		void mousePressEvent (QMouseEvent *) override
		{
			if (!src.here)
				return;
			src.on = !src.on;
			update ();
			emit toggled ();
		}

		void paintEvent (QPaintEvent *) override
		{
			QPainter p (this);
			p.setRenderHint (QPainter::Antialiasing);
			const int h = height(), w = width();
			const bool live = src.here;

			QColor ink   = live ? QColor (0x18, 0x24, 0x2c)
			                    : QColor (0x9a, 0xa5, 0xad);
			QColor faint = live ? QColor (0x5d, 0x6d, 0x79)
			                    : QColor (0xb2, 0xbb, 0xc2);
			QColor mark  (0x2d, 0x6e, 0xa8);

			if (taken()) {
				p.setPen (Qt::NoPen);
				p.setBrush (QColor (0x2d, 0x6e, 0xa8, 20));
				p.drawRoundedRect (rect().adjusted (0, 2, 0, -2), 6, 6);
			}

			// Квадратик слева: пустой, с галкой или перечёркнутый.
			const int box = 26, bx = 14, by = (h - box)/2;
			QRectF b (bx, by, box, box);
			p.setBrush (Qt::NoBrush);
			p.setPen (QPen (taken() ? mark : faint, 2));
			p.drawRoundedRect (b, 4, 4);
			if (taken()) {
				p.setBrush (mark);
				p.setPen (Qt::NoPen);
				p.drawRoundedRect (b, 4, 4);
				QPainterPath tick;
				tick.moveTo (b.left() + 6.5,  b.center().y());
				tick.lineTo (b.left() + 11,   b.bottom() - 7.5);
				tick.lineTo (b.right() - 6,   b.top() + 8);
				p.setBrush (Qt::NoBrush);
				p.setPen (QPen (Qt::white, 2.6, Qt::SolidLine,
				                Qt::RoundCap, Qt::RoundJoin));
				p.drawPath (tick);
			}

			// Вес справа — по нему и выбирают.
			QFont fw = font(); fw.setPointSizeF (font().pointSizeF() + 1);
			fw.setBold (true);
			QString right = live ? weightText () : tr("нет здесь");
			p.setFont (fw);
			int rw = QFontMetrics (fw).horizontalAdvance (right) + 16;
			p.setPen (live ? (taken() ? mark : faint) : faint);
			p.drawText (QRect (w - rw - 12, 0, rw, h),
			            Qt::AlignRight | Qt::AlignVCenter, right);

			// Название и что внутри.
			const int x = bx + box + 14;
			const int tw = w - x - rw - 18;
			QFont ft = font(); ft.setPointSizeF (font().pointSizeF() + 1.5);
			p.setFont (ft);
			p.setPen (ink);
			QFontMetrics fm (ft);
			p.drawText (QRect (x, 10, tw, fm.height()),
			            Qt::AlignLeft | Qt::AlignVCenter,
			            fm.elidedText (src.title, Qt::ElideRight, tw));

			QFont fs = font(); fs.setPointSizeF (font().pointSizeF() - 0.5);
			p.setFont (fs);
			p.setPen (faint);
			QFontMetrics fms (fs);
			QString under = src.about;
			if (!src.run.isEmpty())
				under += "  ·  " + src.run;
			p.drawText (QRect (x, 10 + fm.height() + 2, tw, fms.height() + 4),
			            Qt::AlignLeft | Qt::AlignVCenter,
			            fms.elidedText (under, Qt::ElideRight, tw));

			p.setPen (QColor (0xdd, 0xe3, 0xe8));
			p.drawLine (bx, h-1, w - 12, h-1);
		}

	private:
		QString weightText () const
		{
			if (!weighed)
				return QStringLiteral("…");
			if (bytes <= 0)
				return tr("пусто");
			double m = bytes / 1048576.0;
			QString s = m < 10 ? QString::number (m, 'f', 1)
			                   : QString::number (qRound (m));
			// У NOAA вес — оценка: они режут у себя и длину заранее не
			// сообщают. Врать про точность нельзя, человек по ней решает.
			return (src.exact ? QString() : QStringLiteral("≈ ")) + s
			       + " " + tr("МБ");
		}

		DownloadSheet::Source src;
		qint64 bytes   = 0;
		bool   weighed = false;
};

//=========================================================================
DownloadSheet::DownloadSheet (QWidget *parent)
	: QWidget (parent)
{
	setAutoFillBackground (true);
	setStyleSheet ("background: #f7f9fb;");

	QLabel *head = new QLabel (tr("Что скачать"));
	QFont fh = head->font();
	fh.setPointSizeF (fh.pointSizeF() + 5);
	fh.setBold (true);
	head->setFont (fh);

	hint = new QLabel (tr("Область — то, что сейчас на экране"));
	hint->setStyleSheet ("color: #5d6d79;");
	hint->setWordWrap (true);

	rows = new QVBoxLayout;
	rows->setSpacing (0);
	rows->setContentsMargins (0, 6, 0, 6);

	deep  = new Wheel (tr("Глубина"), 1, 10, 3, tr("сут"));
	every = new Wheel (tr("Шаг"),     1, 12, 3, tr("ч"));
	connect (deep,  &Wheel::valueChanged, this, &DownloadSheet::recount);
	connect (every, &Wheel::valueChanged, this, &DownloadSheet::recount);

	QHBoxLayout *wheels = new QHBoxLayout;
	wheels->setSpacing (10);
	wheels->addWidget (deep,  1);
	wheels->addWidget (every, 1);

	start = new QPushButton (tr("Скачать"));
	start->setMinimumHeight (62);
	QFont fb = start->font();
	fb.setPointSizeF (fb.pointSizeF() + 3);
	fb.setBold (true);
	start->setFont (fb);
	start->setStyleSheet (
	    "QPushButton { background: #2d6ea8; color: white; border: none;"
	    "              border-radius: 8px; }"
	    "QPushButton:disabled { background: #b8c4cd; }");
	connect (start, &QPushButton::clicked, this, &DownloadSheet::go);

	cancel = new QPushButton (tr("Отмена"));
	cancel->setMinimumHeight (48);
	cancel->setStyleSheet (
	    "QPushButton { background: transparent; color: #5d6d79;"
	    "              border: none; }");
	connect (cancel, &QPushButton::clicked, this, &DownloadSheet::dropped);

	QVBoxLayout *v = new QVBoxLayout (this);
	v->setContentsMargins (14, 18, 14, 10);
	v->setSpacing (6);
	v->addWidget (head);
	v->addWidget (hint);
	v->addSpacing (6);
	v->addLayout (rows);
	v->addStretch (1);
	v->addLayout (wheels);
	v->addSpacing (8);
	v->addWidget (start);
	v->addWidget (cancel);
}

//-------------------------------------------------------------------------
void DownloadSheet::setSources (const QList<Source> &src)
{
	for (SourceRow *r : items) {
		rows->removeWidget (r);
		r->deleteLater ();
	}
	items.clear ();
	for (const Source &s : src) {
		SourceRow *r = new SourceRow (s, this);
		connect (r, &SourceRow::toggled, this, &DownloadSheet::recount);
		rows->addWidget (r);
		items << r;
	}
	recount ();
}

//-------------------------------------------------------------------------
void DownloadSheet::setWeigher (std::function<qint64(const QString &, int)> f)
{
	weigher = f;
	recount ();
}

//-------------------------------------------------------------------------
QStringList DownloadSheet::chosen () const
{
	QStringList out;
	for (SourceRow *r : items)
		if (r->taken())
			out << r->id();
	return out;
}

int DownloadSheet::days () const      { return deep->value(); }
int DownloadSheet::hourStep () const  { return every->value(); }

void DownloadSheet::setDepth (int d, int h)
{
	deep->setValue (d);
	every->setValue (h);
	recount ();
}

//-------------------------------------------------------------------------
QString DownloadSheet::mb (qint64 bytes)
{
	double m = bytes / 1048576.0;
	if (m < 10)
		return QString::number (m, 'f', 1);
	return QString::number (qRound (m));
}

//-------------------------------------------------------------------------
void DownloadSheet::recount ()
{
	if (!weigher)
		return;
	qint64 total = 0;
	bool   any = false, exact = true;
	for (SourceRow *r : items) {
		qint64 b = r->here() ? weigher (r->id(), deep->value()) : 0;
		r->setWeight (b, b >= 0);
		if (r->taken() && b > 0) {
			total += b;
			any = true;
			if (r->id() == QLatin1String("noaa"))
				exact = false;
		}
	}
	start->setEnabled (any);
	start->setText (any
	    ? tr("Скачать %1 МБ").arg ((exact ? QString() : QStringLiteral("≈ "))
	                               + mb (total))
	    : tr("Ничего не выбрано"));
}

#include "DownloadSheet.moc"
