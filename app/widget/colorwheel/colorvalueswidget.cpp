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

#include "colorvalueswidget.h"

#include <QGridLayout>
#include <QMouseEvent>
#include <QTabWidget>

#include "common/configwrapper.h"
#include "core.h"
#include "ui/icons/icons.h"

namespace olive
{

ColorValuesWidget::ColorValuesWidget(OakEngineColorManager *manager, QWidget *parent)
	: QWidget(parent)
	, manager_(manager)
	, input_to_ref_(nullptr)
	, ref_to_display_(nullptr)
	, display_to_ref_(nullptr)
	, ref_to_input_(nullptr)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	// Create preview box
	{
		QHBoxLayout *preview_layout = new QHBoxLayout();

		preview_layout->setContentsMargins(0, 0, 0, 0);

		preview_layout->addWidget(new QLabel(tr("Preview")));

		preview_ = new ColorPreviewBox();
		preview_->setFixedHeight(fontMetrics().height() * 3 / 2);
		preview_layout->addWidget(preview_);

		color_picker_btn_ = new QPushButton();
		color_picker_btn_->setIcon(icon::color_picker);
		color_picker_btn_->setFixedWidth(
			color_picker_btn_->sizeHint().height());
		color_picker_btn_->setCheckable(true);
		connect(color_picker_btn_, &QPushButton::toggled, this,
				&ColorValuesWidget::color_picked_btn_toggled);
		connect(Core::instance(), &Core::color_picker_color_emitted, this,
				&ColorValuesWidget::set_reference_color);
		preview_layout->addWidget(color_picker_btn_);

		layout->addLayout(preview_layout);
	}

	// Create value tabs
	{
		QTabWidget *tabs = new QTabWidget();

		input_tab_ = new ColorValuesTab(true);
		tabs->addTab(input_tab_, tr("Input"));
		connect(input_tab_, &ColorValuesTab::color_changed, this,
				&ColorValuesWidget::update_values_from_input);
		connect(input_tab_, &ColorValuesTab::color_changed, this,
				&ColorValuesWidget::color_changed);
		connect(input_tab_, &ColorValuesTab::color_changed, preview_,
				&ColorPreviewBox::set_color);

		reference_tab_ = new ColorValuesTab();
		tabs->addTab(reference_tab_, tr("Reference"));
		connect(reference_tab_, &ColorValuesTab::color_changed, this,
				&ColorValuesWidget::update_values_from_ref);

		display_tab_ = new ColorValuesTab();
		tabs->addTab(display_tab_, tr("Display"));
		connect(display_tab_, &ColorValuesTab::color_changed, this,
				&ColorValuesWidget::update_values_from_display);

		layout->addWidget(tabs);
	}
}

Color ColorValuesWidget::get_color() const
{
	return reference_tab_->get_color();
}

void ColorValuesWidget::set_color_processor(ColorProcessorHandlePtr input_to_ref,
										  ColorProcessorHandlePtr ref_to_display,
										  ColorProcessorHandlePtr display_to_ref,
										  ColorProcessorHandlePtr ref_to_input)
{
	input_to_ref_ = input_to_ref;
	ref_to_display_ = ref_to_display;
	display_to_ref_ = display_to_ref;
	ref_to_input_ = ref_to_input;

	update_values_from_input();

	preview_->set_color_processor(input_to_ref_, ref_to_display_);
}

bool ColorValuesWidget::eventFilter(QObject *watcher, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonPress) {
		// Should signal to Core to stop pixel sampling and to us to remove our event filter
		bool use_this_color = true;

		foreach (QWidget *w, ignore_pick_from_) {
			if (w->underMouse()) {
				use_this_color = false;
				break;
			}
		}

		if (use_this_color) {
			picker_end_color_ = get_color();
		}
		color_picker_btn_->setChecked(false);
		return true;
	} else if (event->type() == QEvent::KeyPress) {
		QKeyEvent *key_ev = static_cast<QKeyEvent *>(event);

		if (key_ev->key() == Qt::Key_Escape) {
			color_picker_btn_->setChecked(false);
			return true;
		}
	}

	return QWidget::eventFilter(watcher, event);
}

void ColorValuesWidget::set_color(const Color &c)
{
	input_tab_->set_color(c);
	preview_->set_color(c);

	update_values_from_input();
}

void ColorValuesWidget::set_reference_color(const Color &c)
{
	reference_tab_->set_color(c);

	update_values_from_ref();
}

void ColorValuesWidget::update_values_from_input()
{
	update_ref_from_input();
	update_display_from_ref();
}

void ColorValuesWidget::update_values_from_ref()
{
	update_input_from_ref();
	update_display_from_ref();
}

void ColorValuesWidget::update_values_from_display()
{
	update_ref_from_display();
	update_input_from_ref();
}

void ColorValuesWidget::color_picked_btn_toggled(bool e)
{
	Core::instance()->request_pixel_sampling_in_viewers(e);

	if (e) {
		qApp->installEventFilter(this);

		// Store current color in case it needs to be restored
		picker_end_color_ = get_color();
	} else {
		qApp->removeEventFilter(this);

		// Restore original color (or use overridden color from eventFilter)
		set_reference_color(picker_end_color_);
		emit color_changed(input_tab_->get_color());
	}
}

void ColorValuesWidget::update_input_from_ref()
{
	if (ref_to_input_) {
		input_tab_->set_color(
			oak_convert_color(ref_to_input_, reference_tab_->get_color()));
	} else {
		input_tab_->set_color(reference_tab_->get_color());
	}

	preview_->set_color(input_tab_->get_color());
	emit color_changed(input_tab_->get_color());
}

void ColorValuesWidget::update_display_from_ref()
{
	if (ref_to_display_) {
		display_tab_->set_color(
			oak_convert_color(ref_to_display_, reference_tab_->get_color()));
	} else {
		display_tab_->set_color(reference_tab_->get_color());
	}
}

void ColorValuesWidget::update_ref_from_input()
{
	if (input_to_ref_) {
		reference_tab_->set_color(
			oak_convert_color(input_to_ref_, input_tab_->get_color()));
	} else {
		reference_tab_->set_color(input_tab_->get_color());
	}
}

void ColorValuesWidget::update_ref_from_display()
{
	if (display_to_ref_) {
		reference_tab_->set_color(
			oak_convert_color(display_to_ref_, display_tab_->get_color()));
	} else {
		reference_tab_->set_color(display_tab_->get_color());
	}
}

const double ColorValuesTab::k_legacy_multiplier = 255.0;

ColorValuesTab::ColorValuesTab(bool with_legacy_option, QWidget *parent)
	: QWidget(parent)
{
	QGridLayout *layout = new QGridLayout(this);

	int row = 0;

	if (with_legacy_option) {
		legacy_box_ = new QCheckBox(tr("Use legacy (8-bit) values"));
		legacy_box_->setChecked(
			OAK_CONFIG("UseLegacyColorInInputTab").toBool());
		connect(legacy_box_, &QCheckBox::clicked, this,
				&ColorValuesTab::legacy_changed);
		layout->addWidget(legacy_box_, row, 0, 1, 2);
		row++;
	} else {
		legacy_box_ = nullptr;
	}

	sliders_.resize(3);

	layout->addWidget(new QLabel(tr("Red")), row, 0);

	red_slider_ = create_color_slider();
	sliders_[0] = red_slider_;
	layout->addWidget(red_slider_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Green")), row, 0);

	green_slider_ = create_color_slider();
	sliders_[1] = green_slider_;
	layout->addWidget(green_slider_, row, 1);

	row++;

	layout->addWidget(new QLabel(tr("Blue")), row, 0);

	blue_slider_ = create_color_slider();
	sliders_[2] = blue_slider_;
	layout->addWidget(blue_slider_, row, 1);

	row++;

	hex_lbl_ = new QLabel(tr("Web"));
	layout->addWidget(hex_lbl_, row, 0);

	hex_slider_ = new StringSlider();
	connect(hex_slider_, &StringSlider::value_changed, this,
			&ColorValuesTab::hex_changed);
	layout->addWidget(hex_slider_, row, 1);

	if (legacy_box_) {
		legacy_changed(are_sliders_legacy_values());
	}
}

Color ColorValuesTab::get_color() const
{
	return Color(get_red(), get_green(), get_blue());
}

void ColorValuesTab::set_color(const Color &c)
{
	set_red(c.red());
	set_green(c.green());
	set_blue(c.blue());
}

double ColorValuesTab::get_red() const
{
	return get_value_internal(red_slider_);
}

double ColorValuesTab::get_green() const
{
	return get_value_internal(green_slider_);
}

double ColorValuesTab::get_blue() const
{
	return get_value_internal(blue_slider_);
}

void ColorValuesTab::set_red(double r)
{
	set_value_internal(red_slider_, r);
}

void ColorValuesTab::set_green(double g)
{
	set_value_internal(green_slider_, g);
}

void ColorValuesTab::set_blue(double b)
{
	set_value_internal(blue_slider_, b);
}

double ColorValuesTab::get_value_internal(FloatSlider *slider) const
{
	double d = slider->get_value();

	if (are_sliders_legacy_values()) {
		d /= k_legacy_multiplier;
	}

	return d;
}

void ColorValuesTab::set_value_internal(FloatSlider *slider, double v)
{
	if (are_sliders_legacy_values()) {
		v *= k_legacy_multiplier;
	}

	slider->set_value(v);
	update_hex();
}

FloatSlider *ColorValuesTab::create_color_slider()
{
	FloatSlider *fs = new FloatSlider();
	fs->set_ladder_element_count(1);
	connect(fs, &FloatSlider::value_changed, this,
			&ColorValuesTab::slider_changed);
	return fs;
}

void ColorValuesTab::slider_changed()
{
	emit color_changed(get_color());
	update_hex();
}

void ColorValuesTab::legacy_changed(bool legacy)
{
	OAK_CONFIG("UseLegacyColorInInputTab") = legacy;

	double legacy_multiplier = legacy ? k_legacy_multiplier :
										1.0 / k_legacy_multiplier;
	int decimal_places = legacy ? 0 : 5;
	double drag_multiplier = legacy ? 1.0 : 0.01;

	foreach (FloatSlider *s, sliders_) {
		s->set_value(s->get_value() * legacy_multiplier);
		s->set_decimal_places(decimal_places);
		s->set_drag_multiplier(drag_multiplier);
	}

	update_hex();
}

QString rgb_val_to_string(double d)
{
	QString s = QString::number(d);

	if (!s.contains('.')) {
		s.append(QStringLiteral(".0"));
	}

	return s;
}

void ColorValuesTab::update_hex()
{
	if (are_sliders_legacy_values()) {
		double r = red_slider_->get_value();
		double g = green_slider_->get_value();
		double b = blue_slider_->get_value();

		if (r > k_legacy_multiplier || g > k_legacy_multiplier ||
			b > k_legacy_multiplier) {
			hex_slider_->set_value(tr("(Invalid)"));
		} else {
			uint32_t rgb = (uint8_t(r) << 16) | (uint8_t(g) << 8) | uint8_t(b);

			hex_slider_->set_value(QStringLiteral("%1")
									  .arg(rgb, 6, 16, QLatin1Char('0'))
									  .toUpper());
		}
	} else {
		hex_slider_->set_value(
			QStringLiteral("rgb(%1, %2, %3)")
				.arg(rgb_val_to_string(red_slider_->get_value()),
					 rgb_val_to_string(green_slider_->get_value()),
					 rgb_val_to_string(blue_slider_->get_value())));
	}
}

bool parse_rgb_string(QString s, double *r, double *g, double *b)
{
	// Trim whitespace
	s = s.trimmed();

	s.remove(QStringLiteral("rgba"), Qt::CaseInsensitive);
	s.remove(QStringLiteral("rgb"), Qt::CaseInsensitive);
	s.remove('(');
	s.remove(')');

	QStringList vals = s.split(',');
	if (vals.size() < 3) {
		return false;
	}

	bool ok;
	*r = vals.at(0).toDouble(&ok);
	if (!ok)
		return false;

	*g = vals.at(1).toDouble(&ok);
	if (!ok)
		return false;

	*b = vals.at(2).toDouble(&ok);
	if (!ok)
		return false;

	return true;
}

void ColorValuesTab::hex_changed(const QString &s)
{
	bool ok;
	uint32_t hex = s.toULong(&ok, 16);

	if (ok) {
		if (hex >= 0x1000000) {
			hex >>= 8;
		}

		uint32_t r = (hex & 0xFF0000) >> 16;
		uint32_t g = (hex & 0x00FF00) >> 8;
		uint32_t b = (hex & 0x0000FF);

		if (are_sliders_legacy_values()) {
			red_slider_->set_value(r);
			green_slider_->set_value(g);
			blue_slider_->set_value(b);
		} else {
			red_slider_->set_value(double(r) / k_legacy_multiplier);
			green_slider_->set_value(double(g) / k_legacy_multiplier);
			blue_slider_->set_value(double(b) / k_legacy_multiplier);
		}

		emit color_changed(get_color());
	} else {
		// Attempt to parse rgb/rgba
		double r, g, b;
		if (parse_rgb_string(s, &r, &g, &b)) {
			if (are_sliders_legacy_values()) {
				red_slider_->set_value(r * k_legacy_multiplier);
				green_slider_->set_value(g * k_legacy_multiplier);
				blue_slider_->set_value(b * k_legacy_multiplier);
			} else {
				red_slider_->set_value(r);
				green_slider_->set_value(g);
				blue_slider_->set_value(b);
			}

			emit color_changed(get_color());
		}
	}

	// Conform string to our formatting
	update_hex();
}

bool ColorValuesTab::are_sliders_legacy_values() const
{
	return legacy_box_ && legacy_box_->isChecked();
}

}
