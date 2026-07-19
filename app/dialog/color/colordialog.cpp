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

#include "colordialog.h"

#include <QDialogButtonBox>
#include <QSplitter>
#include <QVBoxLayout>

#include "common/qtutils.h"

namespace olive
{

ColorDialog::ColorDialog(ColorManager *color_manager, const ManagedColor &start,
						 QWidget *parent)
	: QDialog(parent)
	, color_manager_(color_manager)
{
	setWindowTitle(tr("Select Color"));

	QVBoxLayout *layout = new QVBoxLayout(this);

	QSplitter *splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	layout->addWidget(splitter);

	QWidget *graphics_area = new QWidget();
	splitter->addWidget(graphics_area);

	QVBoxLayout *graphics_layout = new QVBoxLayout(graphics_area);

	QHBoxLayout *wheel_layout = new QHBoxLayout();
	graphics_layout->addLayout(wheel_layout);

	color_wheel_ = new ColorWheelWidget();
	wheel_layout->addWidget(color_wheel_);

	hsv_value_gradient_ = new ColorGradientWidget(Qt::Vertical);
	hsv_value_gradient_->setFixedWidth(
		QtUtils::q_font_metrics_width(fontMetrics(), QStringLiteral("HHH")));
	wheel_layout->addWidget(hsv_value_gradient_);

	QHBoxLayout *swatch_layout = new QHBoxLayout();
	graphics_layout->addLayout(swatch_layout);

	swatch_layout->addStretch();

	swatch_ = new ColorSwatchChooser(color_manager_);
	swatch_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	swatch_layout->addWidget(swatch_);

	swatch_layout->addStretch();

	QWidget *value_area = new QWidget();
	QVBoxLayout *value_layout = new QVBoxLayout(value_area);
	value_layout->setSpacing(0);
	splitter->addWidget(value_area);

	color_values_widget_ = new ColorValuesWidget(color_manager_);
	color_values_widget_->ignore_pick_from(this);
	value_layout->addWidget(color_values_widget_);

	chooser_ = new ColorSpaceChooser(color_manager_);

	value_layout->addWidget(chooser_);

	// Split window 50/50
	splitter->setSizes({ INT_MAX, INT_MAX });

	connect(color_wheel_, &ColorWheelWidget::selected_color_changed,
			color_values_widget_, &ColorValuesWidget::set_color);
	connect(color_wheel_, &ColorWheelWidget::selected_color_changed,
			hsv_value_gradient_, &ColorGradientWidget::set_selected_color);
	connect(color_wheel_, &ColorWheelWidget::selected_color_changed, swatch_,
			&ColorSwatchChooser::set_current_color);
	connect(hsv_value_gradient_, &ColorGradientWidget::selected_color_changed,
			color_values_widget_, &ColorValuesWidget::set_color);
	connect(hsv_value_gradient_, &ColorGradientWidget::selected_color_changed,
			color_wheel_, &ColorWheelWidget::set_selected_color);
	connect(hsv_value_gradient_, &ColorGradientWidget::selected_color_changed,
			swatch_, &ColorSwatchChooser::set_current_color);
	connect(color_values_widget_, &ColorValuesWidget::color_changed,
			hsv_value_gradient_, &ColorGradientWidget::set_selected_color);
	connect(color_values_widget_, &ColorValuesWidget::color_changed,
			color_wheel_, &ColorWheelWidget::set_selected_color);
	connect(color_values_widget_, &ColorValuesWidget::color_changed, swatch_,
			&ColorSwatchChooser::set_current_color);
	connect(swatch_, &ColorSwatchChooser::color_clicked, hsv_value_gradient_,
			&ColorGradientWidget::set_selected_color);
	connect(swatch_, &ColorSwatchChooser::color_clicked, color_wheel_,
			&ColorWheelWidget::set_selected_color);
	connect(swatch_, &ColorSwatchChooser::color_clicked, color_values_widget_,
			&ColorValuesWidget::set_color);

	connect(color_wheel_, &ColorWheelWidget::diameter_changed,
			hsv_value_gradient_, &ColorGradientWidget::setFixedHeight);

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	set_color(start);

	connect(chooser_, &ColorSpaceChooser::color_space_changed, this,
			&ColorDialog::color_space_changed);
	color_space_changed(chooser_->input(), chooser_->output());

	// Set default size ratio to 2:1
	resize(sizeHint().height() * 2, sizeHint().height());
}

void ColorDialog::set_color(const ManagedColor &start)
{
	chooser_->set_input(start.color_input());
	chooser_->set_output(start.color_output());

	Color managed_start;

	if (start.color_input().isEmpty()) {
		managed_start = start;

	} else {
		// Convert reference color to the input space
		ColorProcessorPtr linear_to_input = ColorProcessor::create(
			color_manager_, color_manager_->get_reference_color_space(),
			start.color_input());

		managed_start = linear_to_input->convert_color(start);
	}

	color_wheel_->set_selected_color(managed_start);
	hsv_value_gradient_->set_selected_color(managed_start);
	color_values_widget_->set_color(managed_start);
	swatch_->set_current_color(managed_start);
}

ManagedColor ColorDialog::get_selected_color() const
{
	ManagedColor selected = color_wheel_->get_selected_color();

	// Convert to linear and return a linear color
	if (input_to_ref_processor_) {
		selected = input_to_ref_processor_->convert_color(selected);
	}

	selected.set_color_input(get_color_space_input());
	selected.set_color_output(get_color_space_output());

	return selected;
}

QString ColorDialog::get_color_space_input() const
{
	return chooser_->input();
}

ColorTransform ColorDialog::get_color_space_output() const
{
	return chooser_->output();
}

void ColorDialog::color_space_changed(const QString &input,
									const ColorTransform &output)
{
	input_to_ref_processor_ = ColorProcessor::create(
		color_manager_, input, color_manager_->get_reference_color_space());

	ColorProcessorPtr ref_to_display = ColorProcessor::create(
		color_manager_, color_manager_->get_reference_color_space(), output);

	ColorProcessorPtr ref_to_input = ColorProcessor::create(
		color_manager_, color_manager_->get_reference_color_space(), input);

	// Display -> reference is the inverse of the display transform. Older OCIO
	// versions crashed on TRANSFORM_DIR_INVERSE; guard by requiring a valid
	// processor and fall back to disabling the display tab if creation fails.
	ColorProcessorPtr display_to_ref = ColorProcessor::create(
		color_manager_, color_manager_->get_reference_color_space(), output,
		ColorProcessor::k_inverse);
	if (display_to_ref && !display_to_ref->get_processor()) {
		display_to_ref = nullptr;
	}

	color_wheel_->set_color_processor(input_to_ref_processor_, ref_to_display);
	hsv_value_gradient_->set_color_processor(input_to_ref_processor_,
										   ref_to_display);
	color_values_widget_->set_color_processor(
		input_to_ref_processor_, ref_to_display, display_to_ref, ref_to_input);
}

}
