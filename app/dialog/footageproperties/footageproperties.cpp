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
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "oakutil/oaknode.h"
#include "streamproperties/audiostreamproperties.h"
#include "streamproperties/videostreamproperties.h"
#include "widget/viewer/vieweroutpututils.h"

namespace olive
{

FootagePropertiesDialog::FootagePropertiesDialog(QWidget *parent,
												 OakEngineNode *footage)
	: QDialog(parent)
	, footage_(footage)
{
	QGridLayout *layout = new QGridLayout(this);

	// WRAPPER-GAP: oakengine_node_get_label_or_name -- emulate inline
	// (Node::get_label_or_name(): the label, falling back to the name).
	const QString footage_label = oak::Node(footage_).get_label();
	setWindowTitle(
		tr("\"%1\" Properties")
			.arg(footage_label.isEmpty() ? oak::Node(footage_).name() :
										   footage_label));
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

	int row = 0;

	layout->addWidget(new QLabel(tr("Name:")), row, 0);

	footage_name_field_ = new QLineEdit(footage_label);
	layout->addWidget(footage_name_field_, row, 1);
	row++;

	// Manual source start time: audio/timecode sync relies on this value,
	// which is otherwise only auto-detected from file metadata
	layout->addWidget(new QLabel(tr("Source Start Time:")), row, 0);

	{
		QHBoxLayout *start_time_layout = new QHBoxLayout();

		OakEngineFootage *start_time_handle = oakengine_footage_borrow(footage_);
		int sst_num = 0, sst_den = 1;
		const bool has_sst =
			oakengine_footage_get_source_start_time(start_time_handle,
													&sst_num,
													&sst_den) == 1;

		source_start_time_enable_ = new QCheckBox(tr("Set"));
		source_start_time_enable_->setChecked(has_sst);
		start_time_layout->addWidget(source_start_time_enable_);

		source_start_time_spin_ = new QDoubleSpinBox();
		source_start_time_spin_->setRange(-86400.0, 86400.0);
		source_start_time_spin_->setDecimals(3);
		source_start_time_spin_->setSuffix(QStringLiteral(" s"));
		source_start_time_spin_->setValue(
			has_sst ? double(sst_num) / double(sst_den) : 0.0);
		source_start_time_spin_->setEnabled(
			source_start_time_enable_->isChecked());
		start_time_layout->addWidget(source_start_time_spin_, 1);

		QString detection_note;
		// Detection source comes through the facade (auto-detected field or
		// "manual"), matching the engine's stored value.
		if (has_sst) {
			char source_buf[64];
			source_buf[0] = '\0';
			oakengine_footage_get_source_start_time_source(
				start_time_handle, source_buf, sizeof(source_buf));
			const QString source = QString::fromUtf8(source_buf);
			detection_note =
				(source == QStringLiteral("manual")) ?
					tr("(set manually)") :
					tr("(auto-detected: %1)").arg(source);
		} else {
			detection_note = tr("(not detected)");
		}
		oakengine_footage_free(start_time_handle);
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

	int total_stream_count = 0;
	{
		OakEngineFootage *count_handle = oakengine_footage_borrow(footage_);
		total_stream_count =
			oakengine_footage_get_video_stream_count(count_handle) +
			oakengine_footage_get_audio_stream_count(count_handle) +
			oakengine_footage_get_subtitle_stream_count(count_handle);
		oakengine_footage_free(count_handle);
	}

	for (int i = 0; i < total_stream_count; i++) {
		QString description;
		bool is_enabled = false;

		OakEngineFootage *facade_handle = oakengine_footage_borrow(footage_);

		// (track_type, stream_index) pair for this real stream index;
		// track types are OAKENGINE_TRACK_TYPE_* ordinals (identical to
		// engine Track::Type).
		int reference_type = -1;
		int reference_index = -1;
		oakengine_footage_get_stream_reference(facade_handle, i,
											   &reference_type,
											   &reference_index);

		switch (reference_type) {
		case OAKENGINE_TRACK_TYPE_VIDEO: {
			stacked_widget_->addWidget(
				new VideoStreamProperties(footage_, reference_index));

			is_enabled = oakengine_viewer_get_stream_enabled(
							reinterpret_cast<const OakEngineNode *>(footage_),
							OAKENGINE_TRACK_TYPE_VIDEO, reference_index) == 1;
			{
				char desc_buf[256];
				oakengine_footage_describe_video_stream(
					facade_handle, reference_index, desc_buf,
					sizeof(desc_buf));
				description = QString::fromUtf8(desc_buf);
			}
			break;
		}
		case OAKENGINE_TRACK_TYPE_AUDIO: {
			stacked_widget_->addWidget(
				new AudioStreamProperties(footage_, reference_index));

			AudioParams ap = viewer_output_audio_params(footage_, reference_index);
			is_enabled = ap.enabled();
			{
				char desc_buf[256];
				oakengine_footage_describe_audio_stream(
					facade_handle, reference_index, desc_buf,
					sizeof(desc_buf));
				description = QString::fromUtf8(desc_buf);
			}
			break;
		}
		case OAKENGINE_TRACK_TYPE_SUBTITLE: {
			is_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_SUBTITLE, reference_index);

			// FIXME: Language?
			description = tr("Subtitles");
			break;
		}
		default:
			stacked_widget_->addWidget(new StreamProperties());
			description = tr("Unknown");
			break;
		}

		oakengine_footage_free(facade_handle);

		QListWidgetItem *item = new QListWidgetItem(description, track_list_);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(is_enabled ? Qt::Checked : Qt::Unchecked);
		track_list_->addItem(item);

		if (first_usable_stream == -1 &&
			(reference_type == OAKENGINE_TRACK_TYPE_VIDEO ||
			 reference_type == OAKENGINE_TRACK_TYPE_AUDIO ||
			 reference_type == OAKENGINE_TRACK_TYPE_SUBTITLE)) {
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

	OakEngineFootage *facade_handle = oakengine_footage_borrow(footage_);

	// All writes go through the liboakengine C ABI facade; each call lands
	// on the shared undo stack as an undoable command (replacing this
	// dialog's own undo command classes with identical semantics).
	if (oak::Node(footage_).get_label() != footage_name_field_->text()) {
		oakengine_node_set_label(
			footage_, footage_name_field_->text().toUtf8().constData());
	}

	// Apply source start time changes
	{
		const bool new_enabled = source_start_time_enable_->isChecked();
		const Rational new_time =
			Rational::from_double(source_start_time_spin_->value());
		int cur_sst_num = 0, cur_sst_den = 1;
		const bool cur_has_sst =
			oakengine_footage_get_source_start_time(facade_handle,
													&cur_sst_num,
													&cur_sst_den) == 1;
		if (new_enabled != cur_has_sst ||
			(new_enabled &&
			 new_time != Rational(cur_sst_num, cur_sst_den))) {
			oakengine_footage_set_source_start_time(
				facade_handle, new_enabled ? 1 : 0, new_time.numerator(),
				new_time.denominator());
		}
	}

	int total_stream_count =
		oakengine_footage_get_video_stream_count(facade_handle) +
		oakengine_footage_get_audio_stream_count(facade_handle) +
		oakengine_footage_get_subtitle_stream_count(facade_handle);

	for (int i = 0; i < total_stream_count; i++) {
		int reference_type = -1;
		int reference_index = -1;
		oakengine_footage_get_stream_reference(facade_handle, i,
											   &reference_type,
											   &reference_index);
		bool new_stream_enabled =
			(track_list_->item(i)->checkState() == Qt::Checked);
		bool old_stream_enabled = new_stream_enabled;

		switch (reference_type) {
		case OAKENGINE_TRACK_TYPE_VIDEO:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_VIDEO, reference_index);
			break;
		case OAKENGINE_TRACK_TYPE_AUDIO:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_AUDIO, reference_index);
			break;
		case OAKENGINE_TRACK_TYPE_SUBTITLE:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_SUBTITLE, reference_index);
			break;
		default:
			break;
		}

		if (old_stream_enabled != new_stream_enabled) {
			oakengine_footage_set_stream_enabled(
				facade_handle, reference_type, reference_index,
				new_stream_enabled ? 1 : 0);
		}
	}

	oakengine_footage_free(facade_handle);

	void *command = oakengine_undo_command_create_multi();
	for (int i = 0; i < stacked_widget_->count(); i++) {
		static_cast<StreamProperties *>(stacked_widget_->widget(i))
			->accept(command);
	}
	oakengine_undo_command_free(command); // stream pages write through the facade directly

	QDialog::accept();
}

}
