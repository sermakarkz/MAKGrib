/**********************************************************************
MAKGrib для Android — кнопка со значком.

Значки рисуются кодом. Готовые знаки на Android подменяются цветными
эмодзи — мы это уже проходили со стрелками, — а картинки пришлось бы
держать в нескольких разрешениях.
***********************************************************************/
#ifndef ICONBUTTON_H
#define ICONBUTTON_H

#include <QPushButton>

class IconButton : public QPushButton
{ Q_OBJECT
	public:
		enum Kind { Back, Forward, Gear, Route, Download, Info, Done, Locate };

		IconButton (Kind kind, bool accent, QWidget *parent = nullptr);

		void setKind (Kind k);
		void setAccent (bool on);
		// Круглая кнопка поверх карты, а не полоса в нижнем ряду.
		void setRound (bool on);

	protected:
		void paintEvent (QPaintEvent *) override;

	private:
		Kind kind;
		bool accent;
		bool round = false;
};

#endif
