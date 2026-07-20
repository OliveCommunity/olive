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

#include "sequence.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "config/config.h"
#include "common/qtutils.h"
#include "dialog/msgbox.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"

namespace olive
{

SequenceDialog::SequenceDialog(Sequence *s, Type t, QWidget *parent)
	: QDialog(parent)
	, sequence_(s)
	, make_undoable_(true)
{
	QVBoxLayout *layout = new QVBoxLayout(this);

	QSplitter *splitter = new QSplitter();
	layout->addWidget(splitter);

	preset_tab_ = new SequenceDialogPresetTab();
	splitter->addWidget(preset_tab_);

	parameter_tab_ = new SequenceDialogParameterTab(sequence_);
	splitter->addWidget(parameter_tab_);

	connect(preset_tab_, &SequenceDialogPresetTab::preset_changed,
			parameter_tab_, &SequenceDialogParameterTab::preset_changed);
	connect(preset_tab_, &SequenceDialogPresetTab::preset_accepted, this,
			&SequenceDialog::accept);
	connect(parameter_tab_, &SequenceDialogParameterTab::save_parameters_as_preset,
			preset_tab_, &SequenceDialogPresetTab::save_parameters_as_preset);

	// Set up name section
	QHBoxLayout *name_layout = new QHBoxLayout();
	name_layout->addWidget(new QLabel(tr("Name:")));
	name_field_ = new QLineEdit();
	name_layout->addWidget(name_field_);
	layout->addLayout(name_layout);

	// Set up dialog buttons
	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QPushButton *default_btn =
		buttons->addButton(tr("Set As Default"), QDialogButtonBox::ActionRole);
	connect(buttons, &QDialogButtonBox::accepted, this,
			&SequenceDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this,
			&SequenceDialog::reject);
	connect(default_btn, &QPushButton::clicked, this,
			&SequenceDialog::set_as_default_clicked);
	layout->addWidget(buttons);

	// Set window title based on type
	switch (t) {
	case k_new:
		setWindowTitle(tr("New Sequence"));
		break;
	case k_existing:
		setWindowTitle(tr("Editing \"%1\"").arg(sequence_->get_label()));
		break;
	}

	name_field_->setText(sequence_->get_label());
}

void SequenceDialog::set_undoable(bool u)
{
	make_undoable_ = u;
}

void SequenceDialog::set_name_is_editable(bool e)
{
	name_field_->setEnabled(e);
}

void SequenceDialog::accept()
{
	if (name_field_->isEnabled() && name_field_->text().isEmpty()) {
		msg_box(this, QMessageBox::Critical,
						tr("Error editing Sequence"),
						tr("Please enter a name for this Sequence."));
		return;
	}

	if (!VideoParams::format_is_float(
			parameter_tab_->get_selected_preview_format()) &&
		!OAK_CONFIG("PreviewNonFloatDontAskAgain").toBool()) {
		QMessageBox b(this);
		QCheckBox *dont_show_again = new QCheckBox(tr("Don't ask me again"));

		b.setIcon(QMessageBox::Warning);
		b.setWindowTitle(tr("Low Quality Preview"));
		b.setText(tr(
			"The preview resolution has been set to a non-float format. This may cause banding and clipping artifacts in the preview.\n\n"
			"Do you wish to continue?"));
		b.setCheckBox(dont_show_again);

		b.addButton(QMessageBox::Yes);
		b.addButton(QMessageBox::No);

		if (b.exec() == QMessageBox::No) {
			return;
		}

		if (dont_show_again->isChecked()) {
			OAK_CONFIG("PreviewNonFloatDontAskAgain") = true;
		}
	}

	// All parameter writes go through the liboakengine C ABI facade; in
	// undoable mode each call lands on the shared undo stack as one command,
	// otherwise it applies directly (the flag mirrors the old
	// SequenceParamCommand / direct-set split).
	const int undoable = make_undoable_ ? 1 : 0;
	OakEngineSequence *facade_handle =
		reinterpret_cast<OakEngineSequence *>(sequence_);
	const Rational frame_rate = parameter_tab_->get_selected_video_frame_rate();
	const Rational pixel_aspect =
		parameter_tab_->get_selected_video_pixel_aspect();
	oakengine_sequence_set_video_params(
		facade_handle, parameter_tab_->get_selected_video_width(),
		parameter_tab_->get_selected_video_height(), frame_rate.numerator(),
		frame_rate.denominator(), pixel_aspect.numerator(),
		pixel_aspect.denominator(),
		int(parameter_tab_->get_selected_video_interlacing_mode()),
		int(parameter_tab_->get_selected_preview_format()), undoable);
	oakengine_sequence_set_preview_divider(
		facade_handle, parameter_tab_->get_selected_preview_resolution(),
		undoable);
	oakengine_sequence_set_audio_params(
		facade_handle, parameter_tab_->get_selected_audio_sample_rate(),
		parameter_tab_->get_selected_audio_channel_layout(), undoable);
	oakengine_node_set_label_ex(
		reinterpret_cast<OakEngineNode *>(sequence_),
		name_field_->text().toUtf8().constData(), undoable);
	oakengine_sequence_set_video_auto_cache(
		facade_handle, parameter_tab_->get_selected_preview_auto_cache() ? 1 :
																		 0,
		undoable);

	QDialog::accept();
}

void SequenceDialog::set_as_default_clicked()
{
	if (msg_box(
			this, QMessageBox::Question, tr("Confirm Set As Default"),
			tr("Are you sure you want to set the current parameters as defaults?"),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		// Maybe replace with Preset system
		OAK_CONFIG("DefaultSequenceWidth") =
			parameter_tab_->get_selected_video_width();
		OAK_CONFIG("DefaultSequenceHeight") =
			parameter_tab_->get_selected_video_height();
		OAK_CONFIG("DefaultSequencePixelAspect") =
			QVariant::fromValue(parameter_tab_->get_selected_video_pixel_aspect());
		OAK_CONFIG("DefaultSequenceFrameRate") = QVariant::fromValue(
			parameter_tab_->get_selected_video_frame_rate().flipped());
		OAK_CONFIG("DefaultSequenceInterlacing") =
			parameter_tab_->get_selected_video_interlacing_mode();
		OAK_CONFIG("DefaultSequenceAudioFrequency") =
			parameter_tab_->get_selected_audio_sample_rate();
		OAK_CONFIG("DefaultSequenceAudioLayout") = QVariant::fromValue(
			parameter_tab_->get_selected_audio_channel_layout());
	}
}

}
