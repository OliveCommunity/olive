/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2020 Olive Team
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

#include "footageproperties.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QTreeWidgetItem>
#include <QGroupBox>
#include <QListWidget>
#include <QCheckBox>
#include <QSpinBox>

#include "core.h"
#include "node/nodeundo.h"
#include "streamproperties/audiostreamproperties.h"
#include "streamproperties/videostreamproperties.h"

namespace olive
{

FootagePropertiesDialog::FootagePropertiesDialog(QWidget *parent,
												 Footage *footage)
	: QDialog(parent)
	, footage_(footage)
{
	QGridLayout *layout = new QGridLayout(this);

	setWindowTitle(tr("\"%1\" Properties").arg(footage_->get_label_or_name()));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	int row = 0;

	layout->addWidget(new QLabel(tr("Name:")), row, 0);

	footage_name_field_ = new QLineEdit(footage_->get_label());
	layout->addWidget(footage_name_field_, row, 1);
	row++;

	// Manual source start time: audio/timecode sync relies on this value,
	// which is otherwise only auto-detected from file metadata
	layout->addWidget(new QLabel(tr("Source Start Time:")), row, 0);

	{
		QHBoxLayout *start_time_layout = new QHBoxLayout();

		source_start_time_enable_ = new QCheckBox(tr("Set"));
		source_start_time_enable_->setChecked(footage_->has_source_start_time());
		start_time_layout->addWidget(source_start_time_enable_);

		source_start_time_spin_ = new QDoubleSpinBox();
		source_start_time_spin_->setRange(-86400.0, 86400.0);
		source_start_time_spin_->setDecimals(3);
		source_start_time_spin_->setSuffix(QStringLiteral(" s"));
		source_start_time_spin_->setValue(
			footage_->has_source_start_time() ?
				footage_->source_start_time().to_double() :
				0.0);
		source_start_time_spin_->setEnabled(
			source_start_time_enable_->isChecked());
		start_time_layout->addWidget(source_start_time_spin_, 1);

		QString detection_note;
		if (footage_->has_source_start_time()) {
			const QString &source = footage_->source_start_time_source();
			detection_note =
				(source == QStringLiteral("manual")) ?
					tr("(set manually)") :
					tr("(auto-detected: %1)").arg(source);
		} else {
			detection_note = tr("(not detected)");
		}
		start_time_layout->addWidget(new QLabel(detection_note));

		connect(source_start_time_enable_, &QCheckBox::toggled,
				source_start_time_spin_, &QDoubleSpinBox::setEnabled);

		layout->addLayout(start_time_layout, row, 1);
	}
	row++;

	layout->addWidget(new QLabel(tr("Tracks:")), row, 0, 1, 2);
	row++;

	track_list_ = new QListWidget();
	layout->addWidget(track_list_, row, 0, 1, 2);

	row++;

	stacked_widget_ = new QStackedWidget();
	layout->addWidget(stacked_widget_, row, 0, 1, 2);

	int first_usable_stream = -1;

	for (int i = 0; i < footage_->get_total_stream_count(); i++) {
		Track::Reference reference = footage_->get_reference_from_real_index(i);

		QString description;
		bool is_enabled = false;

		switch (reference.type()) {
		case Track::k_video: {
			stacked_widget_->addWidget(
				new VideoStreamProperties(footage_, reference.index()));

			VideoParams vp = footage_->get_video_params(reference.index());
			is_enabled = vp.enabled();
			description = Footage::describe_video_stream(vp);
			break;
		}
		case Track::k_audio: {
			stacked_widget_->addWidget(
				new AudioStreamProperties(footage_, reference.index()));

			AudioParams ap = footage_->get_audio_params(reference.index());
			is_enabled = ap.enabled();
			description = Footage::describe_audio_stream(ap);
			break;
		}
		case Track::k_subtitle: {
			SubtitleParams sp = footage_->get_subtitle_params(reference.index());
			is_enabled = sp.enabled();

			// FIXME: Language?
			description = tr("Subtitles");
			break;
		}
		default:
			stacked_widget_->addWidget(new StreamProperties());
			description = tr("Unknown");
			break;
		}

		QListWidgetItem *item = new QListWidgetItem(description, track_list_);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(is_enabled ? Qt::Checked : Qt::Unchecked);
		track_list_->addItem(item);

		if (first_usable_stream == -1 &&
			(reference.type() == Track::k_video ||
			 reference.type() == Track::k_audio ||
			 reference.type() == Track::k_subtitle)) {
			first_usable_stream = i;
		}
	}

	row++;

	QDialogButtonBox *buttons =
		new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->setCenterButtons(true);
	layout->addWidget(buttons, row, 0, 1, 2);

	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(track_list_, &QListWidget::currentRowChanged, stacked_widget_,
			&QStackedWidget::setCurrentIndex);

	// Auto-select first item that actually has properties
	if (first_usable_stream >= 0) {
		track_list_->setCurrentRow(first_usable_stream);
	}
	track_list_->setFocus();
}

void FootagePropertiesDialog::accept()
{
	// Perform sanity check on all pages
	for (int i = 0; i < stacked_widget_->count(); i++) {
		if (!static_cast<StreamProperties *>(stacked_widget_->widget(i))
				 ->sanity_check()) {
			// Switch to the failed panel in question
			stacked_widget_->setCurrentIndex(i);

			// Do nothing (it's up to the property panel itself to throw the error message)
			return;
		}
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	if (footage_->get_label() != footage_name_field_->text()) {
		NodeRenameCommand *nrc = new NodeRenameCommand();
		nrc->add_node(footage_, footage_name_field_->text());
		command->add_child(nrc);
	}

	// Apply source start time changes
	{
		const bool new_enabled = source_start_time_enable_->isChecked();
		const Rational new_time =
			Rational::from_double(source_start_time_spin_->value());
		if (new_enabled != footage_->has_source_start_time() ||
			(new_enabled && new_time != footage_->source_start_time())) {
			command->add_child(new FootageSetSourceStartTimeCommand(
				footage_, new_enabled, new_time, QStringLiteral("manual")));
		}
	}

	for (int i = 0; i < footage_->get_total_stream_count(); i++) {
		Track::Reference reference = footage_->get_reference_from_real_index(i);
		bool new_stream_enabled =
			(track_list_->item(i)->checkState() == Qt::Checked);
		bool old_stream_enabled = new_stream_enabled;

		switch (reference.type()) {
		case Track::k_video:
			old_stream_enabled =
				footage_->get_video_params(reference.index()).enabled();
			break;
		case Track::k_audio:
			old_stream_enabled =
				footage_->get_audio_params(reference.index()).enabled();
			break;
		case Track::k_subtitle:
			old_stream_enabled =
				footage_->get_subtitle_params(reference.index()).enabled();
			break;
		case Track::k_none:
		case Track::k_count:
			break;
		}

		if (old_stream_enabled != new_stream_enabled) {
			command->add_child(new StreamEnableChangeCommand(
				footage_, reference.type(), reference.index(),
				new_stream_enabled));
		}
	}

	for (int i = 0; i < stacked_widget_->count(); i++) {
		static_cast<StreamProperties *>(stacked_widget_->widget(i))
			->accept(command);
	}

	Core::instance()->undo_stack()->push(
		command, tr("Set Footage \"%1\" Properties").arg(footage_->get_label()));

	QDialog::accept();
}

FootagePropertiesDialog::StreamEnableChangeCommand::StreamEnableChangeCommand(
	Footage *footage, Track::Type type, int index_in_type, bool enabled)
	: footage_(footage)
	, type_(type)
	, index_(index_in_type)
	, new_enabled_(enabled)
{
}

Project *
FootagePropertiesDialog::StreamEnableChangeCommand::get_relevant_project() const
{
	return footage_->project();
}

void FootagePropertiesDialog::StreamEnableChangeCommand::redo()
{
	switch (type_) {
	case Track::k_video: {
		VideoParams vp = footage_->get_video_params(index_);
		old_enabled_ = vp.enabled();
		vp.set_enabled(new_enabled_);
		footage_->set_video_params(vp, index_);
		break;
	}
	case Track::k_audio: {
		AudioParams ap = footage_->get_audio_params(index_);
		old_enabled_ = ap.enabled();
		ap.set_enabled(new_enabled_);
		footage_->set_audio_params(ap, index_);
		break;
	}
	case Track::k_subtitle: {
		SubtitleParams sp = footage_->get_subtitle_params(index_);
		old_enabled_ = sp.enabled();
		sp.set_enabled(new_enabled_);
		footage_->set_subtitle_params(sp, index_);
		break;
	}
	case Track::k_none:
	case Track::k_count:
		break;
	}
}

void FootagePropertiesDialog::StreamEnableChangeCommand::undo()
{
	switch (type_) {
	case Track::k_video: {
		VideoParams vp = footage_->get_video_params(index_);
		vp.set_enabled(old_enabled_);
		footage_->set_video_params(vp, index_);
		break;
	}
	case Track::k_audio: {
		AudioParams ap = footage_->get_audio_params(index_);
		ap.set_enabled(old_enabled_);
		footage_->set_audio_params(ap, index_);
		break;
	}
	case Track::k_subtitle: {
		SubtitleParams sp = footage_->get_subtitle_params(index_);
		sp.set_enabled(old_enabled_);
		footage_->set_subtitle_params(sp, index_);
		break;
	}
	case Track::k_none:
	case Track::k_count:
		break;
	}
}

FootagePropertiesDialog::FootageSetSourceStartTimeCommand::
	FootageSetSourceStartTimeCommand(Footage *footage, bool enabled,
									 const Rational &time,
									 const QString &source)
	: footage_(footage)
	, new_enabled_(enabled)
	, new_time_(time)
	, new_source_(source)
{
}

Project *
FootagePropertiesDialog::FootageSetSourceStartTimeCommand::get_relevant_project()
	const
{
	return footage_->project();
}

void FootagePropertiesDialog::FootageSetSourceStartTimeCommand::redo()
{
	old_enabled_ = footage_->has_source_start_time();
	old_time_ = footage_->source_start_time();
	old_source_ = footage_->source_start_time_source();

	if (new_enabled_) {
		footage_->set_source_start_time(new_time_, new_source_);
	} else {
		footage_->clear_source_start_time();
	}
}

void FootagePropertiesDialog::FootageSetSourceStartTimeCommand::undo()
{
	if (old_enabled_) {
		footage_->set_source_start_time(old_time_, old_source_);
	} else {
		footage_->clear_source_start_time();
	}
}

}
