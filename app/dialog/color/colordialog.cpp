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

#include "oakutil/qtutils.h"

namespace olive
{

ColorDialog::ColorDialog(OakEngineColorManager *color_manager, const ManagedColor &start,
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
		QByteArray ref_cs = oak_query_string([this](char *buf, int size) {
			return oakengine_color_manager_reference_color_space(
				color_manager_, buf, size);
		}).toUtf8();
		QByteArray in_cs = start.color_input().toUtf8();
		oak_color_transform in_pod;
		in_pod.is_display = 0;
		in_pod.output = in_cs.constData();
		in_pod.view = nullptr;
		in_pod.look = nullptr;
		ColorProcessorHandlePtr linear_to_input(
			oakengine_color_processor_create(color_manager_, ref_cs.constData(),
											 &in_pod,
											 OAKENGINE_COLOR_PROCESSOR_NORMAL),
			ColorProcessorHandleDeleter());

		managed_start = oak_convert_color(linear_to_input, start);
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
		selected = oak_convert_color(input_to_ref_processor_, selected);
	}

	selected.set_color_input(get_color_space_input());
	selected.set_color_output(get_color_space_output());

	return selected;
}

QString ColorDialog::get_color_space_input() const
{
	return chooser_->input();
}

oak::ColorTransform ColorDialog::get_color_space_output() const
{
	return chooser_->output();
}

void ColorDialog::color_space_changed(const QString &input,
									const oak::ColorTransform &output)
{
	QByteArray ref_cs = oak_query_string([this](char *buf, int size) {
		return oakengine_color_manager_reference_color_space(
			color_manager_, buf, size);
	}).toUtf8();
	QByteArray in = input.toUtf8();
	QByteArray o, v, l;
	oak_color_transform out_pod = oak_to_transform(output, &o, &v, &l);

	auto make_proc = [&](const char *input_cs, const oak_color_transform *dest,
						 int dir) -> ColorProcessorHandlePtr {
		return ColorProcessorHandlePtr(
			oakengine_color_processor_create(color_manager_, input_cs, dest,
											 dir),
			ColorProcessorHandleDeleter());
	};

	input_to_ref_processor_ = make_proc(in.constData(), &out_pod,
										OAKENGINE_COLOR_PROCESSOR_NORMAL);

	oak_color_transform ref_display_pod;
	ref_display_pod.is_display = out_pod.is_display;
	ref_display_pod.output = out_pod.output;
	ref_display_pod.view = out_pod.view;
	ref_display_pod.look = out_pod.look;
	ColorProcessorHandlePtr ref_to_display = make_proc(
		ref_cs.constData(), &ref_display_pod,
		OAKENGINE_COLOR_PROCESSOR_NORMAL);

	oak_color_transform ref_input_pod;
	ref_input_pod.is_display = 0;
	ref_input_pod.output = in.constData();
	ref_input_pod.view = nullptr;
	ref_input_pod.look = nullptr;
	ColorProcessorHandlePtr ref_to_input = make_proc(
		ref_cs.constData(), &ref_input_pod,
		OAKENGINE_COLOR_PROCESSOR_NORMAL);

	// Display -> reference is the inverse of the display transform. Older OCIO
	// versions crashed on TRANSFORM_DIR_INVERSE; guard by requiring a valid
	// processor and fall back to disabling the display tab if creation fails.
	ColorProcessorHandlePtr display_to_ref = make_proc(
		ref_cs.constData(), &ref_display_pod,
		OAKENGINE_COLOR_PROCESSOR_INVERSE);
	if (display_to_ref && !oakengine_color_processor_is_valid(display_to_ref.get())) {
		display_to_ref = nullptr;
	}

	color_wheel_->set_color_processor(input_to_ref_processor_, ref_to_display);
	hsv_value_gradient_->set_color_processor(input_to_ref_processor_,
										   ref_to_display);
	color_values_widget_->set_color_processor(
		input_to_ref_processor_, ref_to_display, display_to_ref, ref_to_input);
}

}
