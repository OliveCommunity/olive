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

#include "trackviewitem.h"

#include <QDebug>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "ui/icons/icons.h"
#include "widget/menu/menu.h"
#include "widget/timelinewidget/trackhandle.h"

namespace olive
{

namespace
{

// The sequence that owns `track`, through the C ABI.
OakEngineSequence *track_owner_sequence(OakEngineTrack *track)
{
	return reinterpret_cast<OakEngineSequence *>(oakengine_track_get_sequence(
		reinterpret_cast<OakEngineNode *>(track)));
}

} // namespace

TrackViewItem::TrackViewItem(OakEngineTrack *track, QWidget *parent)
	: QWidget(parent)
	, track_(track)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	stack_ = new QStackedWidget();
	layout->addWidget(stack_);

	label_ = new ClickableLabel();
	connect(label_, &ClickableLabel::mouse_double_clicked, this,
			&TrackViewItem::label_clicked);
	bridge_.subscribe(track_, OAKENGINE_EVENT_TRACK_INDEX_CHANGED);
	connect(&bridge_, &EngineEventBridge::track_index_changed, this,
			&TrackViewItem::update_label);
	update_label();
	stack_->addWidget(label_);

	line_edit_ = new FocusableLineEdit();
	connect(line_edit_, &FocusableLineEdit::confirmed, this,
			&TrackViewItem::line_edit_confirmed);
	connect(line_edit_, &FocusableLineEdit::cancelled, this,
			&TrackViewItem::line_edit_cancelled);
	stack_->addWidget(line_edit_);

	mute_button_ = create_msl_button(Qt::red);
	mute_button_->setChecked(oakengine_track_is_muted(
		track_owner_sequence(track),
		track_type_of(track), track_index_of(track)));
	update_mute_button(oakengine_track_is_muted(
		track_owner_sequence(track),
		track_type_of(track), track_index_of(track)));
	connect(mute_button_, &QPushButton::toggled, this, [this](bool checked) {
		oakengine_track_set_muted(
			track_owner_sequence(track_),
			track_type_of(track_), track_index_of(track_), checked);
	});
	connect(mute_button_, &QPushButton::toggled, this,
			&TrackViewItem::update_mute_button);
	layout->addWidget(mute_button_);

	/*solo_button_ = CreateMSLButton(tr("S"), Qt::yellow);
  layout->addWidget(solo_button_);*/

	lock_button_ = create_msl_button(Qt::gray);
	lock_button_->setChecked(oakengine_track_is_locked(
		track_owner_sequence(track),
		track_type_of(track), track_index_of(track)));
	update_lock_button(oakengine_track_is_locked(
		track_owner_sequence(track),
		track_type_of(track), track_index_of(track)));
	connect(lock_button_, &QPushButton::toggled, this, [this](bool checked) {
		oakengine_track_set_locked(
			track_owner_sequence(track_),
			track_type_of(track_), track_index_of(track_), checked);
	});
	connect(lock_button_, &QPushButton::toggled, this,
			&TrackViewItem::update_lock_button);
	layout->addWidget(lock_button_);

	setMinimumHeight(mute_button_->height());
	setContextMenuPolicy(Qt::CustomContextMenu);

	bridge_.subscribe(track, OAKENGINE_EVENT_TRACK_MUTED_CHANGED);
	connect(&bridge_, &EngineEventBridge::track_muted_changed, mute_button_,
			[this](OakEngineTrack *, bool muted) {
				mute_button_->setChecked(muted);
			});
	connect(this, &QWidget::customContextMenuRequested, this,
			&TrackViewItem::show_context_menu);
}

QPushButton *TrackViewItem::create_msl_button(const QColor &checked_color) const
{
	QPushButton *button = new QPushButton();
	button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	button->setCheckable(true);
	button->setStyleSheet(
		QStringLiteral("QPushButton::checked { background: %1; }")
			.arg(checked_color.name()));

	int size = button->sizeHint().height();
	size = qRound(size * 0.75);
	button->setFixedSize(size, size);

	return button;
}

void TrackViewItem::label_clicked()
{
	stack_->setCurrentWidget(line_edit_);
	line_edit_->setFocus();
	line_edit_->selectAll();
}

void TrackViewItem::line_edit_confirmed()
{
	line_edit_->blockSignals(true);

	oakengine_node_set_label(
		reinterpret_cast<OakEngineNode *>(track_),
		line_edit_->text().toUtf8().constData());
	update_label();

	stack_->setCurrentWidget(label_);

	line_edit_->blockSignals(false);
}

void TrackViewItem::line_edit_cancelled()
{
	line_edit_->blockSignals(true);

	stack_->setCurrentWidget(label_);

	line_edit_->blockSignals(false);
}

void TrackViewItem::update_label()
{
	// Per the UI design reference, tracks are identified by an NLE-style
	// type+number prefix (V1/V2 for video, A1/A2 for audio, S1 for subtitle)
	// followed by the track's custom label or default name.
	QString prefix;
	switch (track_type_of(track_)) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		prefix = QStringLiteral("V%1").arg(track_index_of(track_) + 1);
		break;
	case OAKENGINE_TRACK_TYPE_AUDIO:
		prefix = QStringLiteral("A%1").arg(track_index_of(track_) + 1);
		break;
	case OAKENGINE_TRACK_TYPE_SUBTITLE:
		prefix = QStringLiteral("S%1").arg(track_index_of(track_) + 1);
		break;
	}

	char label_buf[256];
	oakengine_node_get_label(
		reinterpret_cast<OakEngineNode *>(track_),
		label_buf, sizeof(label_buf));
	QString display;
	if (label_buf[0]) {
		display = QString::fromUtf8(label_buf);
	} else {
		oakengine_node_get_name(
			reinterpret_cast<OakEngineNode *>(track_),
			label_buf, sizeof(label_buf));
		display = QString::fromUtf8(label_buf);
	}

	label_->setText(prefix.isEmpty() ? display
									 : QStringLiteral("%1  %2").arg(prefix, display));
}

void TrackViewItem::show_context_menu(const QPoint &p)
{
	Menu m(this);

	QAction *delete_action = m.addAction(tr("&Delete"));
	connect(delete_action, &QAction::triggered, this,
			&TrackViewItem::delete_track, Qt::QueuedConnection);

	m.addSeparator();

	QAction *delete_unused_action = m.addAction(tr("Delete All &Empty"));
	connect(delete_unused_action, &QAction::triggered, this,
			&TrackViewItem::delete_all_empty_tracks, Qt::QueuedConnection);

	m.exec(mapToGlobal(p));
}

void TrackViewItem::delete_track()
{
	emit about_to_delete_track(track_);
	// Through the liboakengine C ABI facade (one undoable command, same as
	// the old TimelineRemoveTrackCommand push).
	oakengine_sequence_remove_track(
		track_owner_sequence(track_),
		int(track_type_of(track_)), track_index_of(track_));
}

void TrackViewItem::delete_all_empty_tracks()
{
	OakEngineSequence *sequence = track_owner_sequence(track_);
	QStringList track_names_to_remove;

	// Iterate all track lists (video, audio, subtitle), mirroring
	// Sequence::get_tracks(); the per-type counts line up with the
	// OAKENGINE_TRACK_TYPE_* ordinals (0..2).
	int track_counts[3] = { 0, 0, 0 };
	oakengine_sequence_track_count(sequence, &track_counts[0],
								   &track_counts[1], &track_counts[2]);
	for (int type = 0; type < 3; type++) {
		for (int ti = 0; ti < track_counts[type]; ti++) {
			OakEngineTrack *t = oakengine_sequence_track_at(sequence, type, ti);
			if (t && oakengine_track_block_count(t) == 0) {
				// WRAPPER-GAP: oakengine_node_get_label_or_name -- emulate
				// inline (the label, falling back to the name).
				char buf[256];
				buf[0] = '\0';
				oakengine_node_get_label(
					reinterpret_cast<OakEngineNode *>(t), buf, sizeof(buf));
				QString name = QString::fromUtf8(buf);
				if (name.isEmpty()) {
					oakengine_node_get_name(
						reinterpret_cast<OakEngineNode *>(t), buf,
						sizeof(buf));
					name = QString::fromUtf8(buf);
				}
				track_names_to_remove.append(name);
			}
		}
	}

	if (track_names_to_remove.isEmpty()) {
		QMessageBox::information(this, tr("Delete All Empty"),
								 tr("No tracks are currently empty"));
	} else {
		if (QMessageBox::question(
				this, tr("Delete All Empty"),
				tr("This will delete the following tracks:\n\n%1\n\nDo you wish to continue?")
					.arg(track_names_to_remove.join('\n')),
				QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok) {
			// Batch removal through the liboakengine C ABI facade (one
			// undoable command, same as the old per-track children).
			oakengine_sequence_delete_empty_tracks(sequence, -1);
		}
	}
}

void TrackViewItem::update_mute_button(bool e)
{
	mute_button_->setIcon(e ? icon::eye_closed : icon::eye_opened);
}

void TrackViewItem::update_lock_button(bool e)
{
	lock_button_->setIcon(e ? icon::lock_closed : icon::lock_opened);
}

}
