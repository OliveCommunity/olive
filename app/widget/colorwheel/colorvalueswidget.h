/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_COLORVALUESWIDGET_H
#define OAK_COLORVALUESWIDGET_H

#include <QCheckBox>
#include <QPushButton>
#include <QWidget>

#include "colorpreviewbox.h"
#include "node/color/colormanager/colormanager.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/stringslider.h"

namespace olive
{

class ColorValuesTab : public QWidget {
	Q_OBJECT
public:
	ColorValuesTab(bool with_legacy_option = false, QWidget *parent = nullptr);

	Color get_color() const;

	void set_color(const Color &c);

	double get_red() const;
	double get_green() const;
	double get_blue() const;
	void set_red(double r);
	void set_green(double g);
	void set_blue(double b);

signals:
	void color_changed(const Color &c);

private:
	static const double k_legacy_multiplier;

	double get_value_internal(FloatSlider *slider) const;
	void set_value_internal(FloatSlider *slider, double v);

	bool are_sliders_legacy_values() const;

	FloatSlider *create_color_slider();

	FloatSlider *red_slider_;
	FloatSlider *green_slider_;
	FloatSlider *blue_slider_;

	QLabel *hex_lbl_;
	StringSlider *hex_slider_;

	QVector<FloatSlider *> sliders_;

	QCheckBox *legacy_box_;

private slots:
	void slider_changed();

	void legacy_changed(bool e);

	void update_hex();

	void hex_changed(const QString &s);
};

class ColorValuesWidget : public QWidget {
	Q_OBJECT
public:
	ColorValuesWidget(ColorManager *manager, QWidget *parent = nullptr);

	Color get_color() const;

	void set_color_processor(ColorProcessorPtr input_to_ref,
						   ColorProcessorPtr ref_to_display,
						   ColorProcessorPtr display_to_ref,
						   ColorProcessorPtr ref_to_input);

	virtual bool eventFilter(QObject *watcher, QEvent *event) override;

	void ignore_pick_from(QWidget *w)
	{
		ignore_pick_from_.append(w);
	}

public slots:
	void set_color(const Color &c);

	void set_reference_color(const Color &c);

signals:
	void color_changed(const Color &c);

private:
	void update_input_from_ref();

	void update_display_from_ref();

	void update_ref_from_input();

	void update_ref_from_display();

	ColorManager *manager_;

	ColorPreviewBox *preview_;

	ColorValuesTab *input_tab_;

	ColorValuesTab *reference_tab_;

	ColorValuesTab *display_tab_;

	ColorProcessorPtr input_to_ref_;

	ColorProcessorPtr ref_to_display_;

	ColorProcessorPtr display_to_ref_;

	ColorProcessorPtr ref_to_input_;

	QPushButton *color_picker_btn_;

	Color picker_end_color_;

	QVector<QWidget *> ignore_pick_from_;

private slots:
	void update_values_from_input();

	void update_values_from_ref();

	void update_values_from_display();

	void color_picked_btn_toggled(bool e);
};

}

#endif // OAK_COLORVALUESWIDGET_H
