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
#include "streamproperties/audiostreamproperties.h"
#include "streamproperties/videostreamproperties.h"
#include "widget/viewer/vieweroutpututils.h"

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
		// Detection source comes through the facade (auto-detected field or
		// "manual"), matching the engine's stored value.
		if (footage_->has_source_start_time()) {
			OakEngineFootage *facade_handle = oakengine_footage_borrow(
				reinterpret_cast<OakEngineNode *>(footage_));
			char source_buf[64];
			source_buf[0] = '\0';
			oakengine_footage_get_source_start_time_source(
				facade_handle, source_buf, sizeof(source_buf));
			oakengine_footage_free(facade_handle);
			const QString source = QString::fromUtf8(source_buf);
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

		OakEngineFootage *facade_handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(footage_));

		switch (reference.type()) {
		case Track::k_video: {
			stacked_widget_->addWidget(
				new VideoStreamProperties(footage_, reference.index()));

			VideoParams vp = viewer_output_video_params(footage_, reference.index());
			is_enabled = vp.enabled();
			{
				char desc_buf[256];
				oakengine_footage_describe_video_stream(
					facade_handle, reference.index(), desc_buf,
					sizeof(desc_buf));
				description = QString::fromUtf8(desc_buf);
			}
			break;
		}
		case Track::k_audio: {
			stacked_widget_->addWidget(
				new AudioStreamProperties(footage_, reference.index()));

			AudioParams ap = viewer_output_audio_params(footage_, reference.index());
			is_enabled = ap.enabled();
			{
				char desc_buf[256];
				oakengine_footage_describe_audio_stream(
					facade_handle, reference.index(), desc_buf,
					sizeof(desc_buf));
				description = QString::fromUtf8(desc_buf);
			}
			break;
		}
		case Track::k_subtitle: {
			is_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_SUBTITLE, reference.index());

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

	OakEngineFootage *facade_handle = oakengine_footage_borrow(
		reinterpret_cast<OakEngineNode *>(footage_));

	// All writes go through the liboakengine C ABI facade; each call lands
	// on the shared undo stack as an undoable command (replacing this
	// dialog's own undo command classes with identical semantics).
	if (footage_->get_label() != footage_name_field_->text()) {
		oakengine_node_set_label(
			reinterpret_cast<OakEngineNode *>(footage_),
			footage_name_field_->text().toUtf8().constData());
	}

	// Apply source start time changes
	{
		const bool new_enabled = source_start_time_enable_->isChecked();
		const Rational new_time =
			Rational::from_double(source_start_time_spin_->value());
		if (new_enabled != footage_->has_source_start_time() ||
			(new_enabled && new_time != footage_->source_start_time())) {
			oakengine_footage_set_source_start_time(
				facade_handle, new_enabled ? 1 : 0, new_time.numerator(),
				new_time.denominator());
		}
	}

	for (int i = 0; i < footage_->get_total_stream_count(); i++) {
		Track::Reference reference = footage_->get_reference_from_real_index(i);
		bool new_stream_enabled =
			(track_list_->item(i)->checkState() == Qt::Checked);
		bool old_stream_enabled = new_stream_enabled;

		switch (reference.type()) {
		case Track::k_video:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_VIDEO, reference.index());
			break;
		case Track::k_audio:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_AUDIO, reference.index());
			break;
		case Track::k_subtitle:
			old_stream_enabled = oakengine_footage_get_stream_enabled(
				facade_handle, OAKENGINE_TRACK_TYPE_SUBTITLE, reference.index());
			break;
		case Track::k_none:
		case Track::k_count:
			break;
		}

		if (old_stream_enabled != new_stream_enabled) {
			oakengine_footage_set_stream_enabled(
				facade_handle, int(reference.type()), reference.index(),
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
