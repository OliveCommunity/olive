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
#include "core.h"
#include "common/qtutils.h"
#include "undo/undostack.h"

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
		QtUtils::msg_box(this, QMessageBox::Critical,
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

	// Generate video and audio parameter structs from data
	VideoParams video_params =
		VideoParams(parameter_tab_->get_selected_video_width(),
					parameter_tab_->get_selected_video_height(),
					parameter_tab_->get_selected_video_frame_rate().flipped(),
					parameter_tab_->get_selected_preview_format(),
					VideoParams::k_internal_channel_count,
					parameter_tab_->get_selected_video_pixel_aspect(),
					parameter_tab_->get_selected_video_interlacing_mode(),
					parameter_tab_->get_selected_preview_resolution());

	AudioParams audio_params =
		AudioParams(parameter_tab_->get_selected_audio_sample_rate(),
					parameter_tab_->get_selected_audio_channel_layout(),
					Sequence::k_default_sample_format);

	if (make_undoable_) {
		// Make undoable command to change the parameters
		SequenceParamCommand *param_command = new SequenceParamCommand(
			sequence_, video_params, audio_params, name_field_->text(),
			parameter_tab_->get_selected_preview_auto_cache());

		Core::instance()->undo_stack()->push(
			param_command,
			tr("Set Sequence Parameters For \"%1\"").arg(sequence_->get_label()));

	} else {
		// Set sequence values directly with no undo command
		sequence_->set_video_params(video_params);
		sequence_->set_audio_params(audio_params);
		sequence_->set_label(name_field_->text());
		sequence_->set_video_auto_cache_enabled(
			parameter_tab_->get_selected_preview_auto_cache());
	}

	QDialog::accept();
}

void SequenceDialog::set_as_default_clicked()
{
	if (QtUtils::msg_box(
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

SequenceDialog::SequenceParamCommand::SequenceParamCommand(
	Sequence *s, const VideoParams &video_params,
	const AudioParams &audio_params, const QString &name, bool autocache)
	: sequence_(s)
	, new_video_params_(video_params)
	, new_audio_params_(audio_params)
	, new_name_(name)
	, new_autocache_(autocache)
	, old_video_params_(s->get_video_params())
	, old_audio_params_(s->get_audio_params())
	, old_name_(s->get_label())
	, old_autocache_(s->is_video_auto_cache_enabled())
{
}

Project *SequenceDialog::SequenceParamCommand::get_relevant_project() const
{
	return sequence_->project();
}

void SequenceDialog::SequenceParamCommand::redo()
{
	if (sequence_->get_video_params() != new_video_params_) {
		sequence_->set_video_params(new_video_params_);
	}
	if (sequence_->get_audio_params() != new_audio_params_) {
		sequence_->set_audio_params(new_audio_params_);
	}
	sequence_->set_label(new_name_);
	sequence_->set_video_auto_cache_enabled(new_autocache_);
}

void SequenceDialog::SequenceParamCommand::undo()
{
	if (sequence_->get_video_params() != old_video_params_) {
		sequence_->set_video_params(old_video_params_);
	}
	if (sequence_->get_audio_params() != old_audio_params_) {
		sequence_->set_audio_params(old_audio_params_);
	}
	sequence_->set_label(old_name_);
	sequence_->set_video_auto_cache_enabled(old_autocache_);
}

}
