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

#include "scope.h"

#include <QVBoxLayout>

#include "panel/viewer/viewer.h"

namespace olive
{

ScopePanel::ScopePanel()
	: PanelWidget(QStringLiteral("ScopePanel"))
	, viewer_(nullptr)
{
	QWidget *central = new QWidget(this);
	setWidget(central);

	QVBoxLayout *layout = new QVBoxLayout(central);

	QHBoxLayout *toolbar_layout = new QHBoxLayout();
	toolbar_layout->setContentsMargins(0, 0, 0, 0);

	scope_type_combobox_ = new QComboBox();

	for (int i = 0; i < ScopePanel::k_type_count; i++) {
		// These strings get filled in later in Retranslate()
		scope_type_combobox_->addItem(QString());
	}

	toolbar_layout->addWidget(scope_type_combobox_);
	toolbar_layout->addStretch();

	layout->addLayout(toolbar_layout);

	stack_ = new QStackedWidget();
	layout->addWidget(stack_);

	// Create waveform view
	waveform_view_ = new WaveformScope();
	stack_->addWidget(waveform_view_);

	// Create vectorscope
	vectorscope_ = new VectorscopeScope();
	stack_->addWidget(vectorscope_);

	// Create histogram
	histogram_ = new HistogramScope();
	stack_->addWidget(histogram_);

	connect(
		scope_type_combobox_,
		static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
		stack_, &QStackedWidget::setCurrentIndex);

	retranslate();
}

void ScopePanel::set_type(ScopePanel::Type t)
{
	scope_type_combobox_->setCurrentIndex(t);
}

QString ScopePanel::type_to_name(ScopePanel::Type t)
{
	switch (t) {
	case k_type_waveform:
		return tr("Waveform");
	case k_type_vectorscope:
		return tr("Vectorscope");
	case k_type_histogram:
		return tr("Histogram");
	case k_type_count:
		break;
	}

	return QString();
}

void ScopePanel::set_viewer_panel(ViewerPanelBase *vp)
{
	if (viewer_ == vp) {
		return;
	}

	if (viewer_) {
		disconnect(viewer_, &ViewerPanelBase::texture_changed, this,
				   &ScopePanel::set_reference_buffer);
		disconnect(viewer_, &ViewerPanelBase::color_manager_changed, this,
				   &ScopePanel::set_color_manager);
	}

	viewer_ = vp;

	if (viewer_) {
		// Connect viewer widget texture drawing to scope panel
		connect(viewer_, &ViewerPanelBase::texture_changed, this,
				&ScopePanel::set_reference_buffer);
		connect(viewer_, &ViewerPanelBase::color_manager_changed, this,
				&ScopePanel::set_color_manager);

		set_color_manager(viewer_->get_color_manager());

		viewer_->update_texture_from_node();
	} else {
		set_reference_buffer(nullptr);
		set_color_manager(nullptr);
	}
}

void ScopePanel::set_reference_buffer(TexturePtr frame)
{
	histogram_->set_buffer(frame);
	vectorscope_->set_buffer(frame);
	waveform_view_->set_buffer(frame);
}

void ScopePanel::set_color_manager(OakEngineColorManager *manager)
{
	histogram_->connect_color_manager(manager);
	vectorscope_->connect_color_manager(manager);
	waveform_view_->connect_color_manager(manager);
}

void ScopePanel::retranslate()
{
	set_title(tr("Scopes"));

	for (int i = 0; i < ScopePanel::k_type_count; i++) {
		scope_type_combobox_->setItemText(i, type_to_name(static_cast<Type>(i)));
	}
}

}
