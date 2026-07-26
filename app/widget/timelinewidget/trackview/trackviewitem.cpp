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

#include "node/project/sequence/sequence.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "ui/icons/icons.h"
#include "widget/menu/menu.h"

namespace olive
{

TrackViewItem::TrackViewItem(Track *track, QWidget *parent)
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
	bridge_.subscribe(reinterpret_cast<OakEngineTrack *>(track_),
						OAKENGINE_EVENT_TRACK_INDEX_CHANGED);
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
		reinterpret_cast<OakEngineSequence *>(track->sequence()),
		track->type(), track->index()));
	update_mute_button(oakengine_track_is_muted(
		reinterpret_cast<OakEngineSequence *>(track->sequence()),
		track->type(), track->index()));
	connect(mute_button_, &QPushButton::toggled, this, [this](bool checked) {
		oakengine_track_set_muted(
			reinterpret_cast<OakEngineSequence *>(track_->sequence()),
			track_->type(), track_->index(), checked);
	});
	connect(mute_button_, &QPushButton::toggled, this,
			&TrackViewItem::update_mute_button);
	layout->addWidget(mute_button_);

	/*solo_button_ = CreateMSLButton(tr("S"), Qt::yellow);
  layout->addWidget(solo_button_);*/

	lock_button_ = create_msl_button(Qt::gray);
	lock_button_->setChecked(oakengine_track_is_locked(
		reinterpret_cast<OakEngineSequence *>(track->sequence()),
		track->type(), track->index()));
	update_lock_button(oakengine_track_is_locked(
		reinterpret_cast<OakEngineSequence *>(track->sequence()),
		track->type(), track->index()));
	connect(lock_button_, &QPushButton::toggled, this, [this](bool checked) {
		oakengine_track_set_locked(
			reinterpret_cast<OakEngineSequence *>(track_->sequence()),
			track_->type(), track_->index(), checked);
	});
	connect(lock_button_, &QPushButton::toggled, this,
			&TrackViewItem::update_lock_button);
	layout->addWidget(lock_button_);

	setMinimumHeight(mute_button_->height());
	setContextMenuPolicy(Qt::CustomContextMenu);

	bridge_.subscribe(reinterpret_cast<OakEngineTrack *>(track),
						OAKENGINE_EVENT_TRACK_MUTED_CHANGED);
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
	switch (track_->type()) {
	case Track::k_video:
		prefix = QStringLiteral("V%1").arg(track_->index() + 1);
		break;
	case Track::k_audio:
		prefix = QStringLiteral("A%1").arg(track_->index() + 1);
		break;
	case Track::k_subtitle:
		prefix = QStringLiteral("S%1").arg(track_->index() + 1);
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
	emit about_to_delete_track(reinterpret_cast<OakEngineTrack *>(track_));
	// Through the liboakengine C ABI facade (one undoable command, same as
	// the old TimelineRemoveTrackCommand push).
	oakengine_sequence_remove_track(
		reinterpret_cast<OakEngineSequence *>(track_->sequence()),
		int(track_->type()), track_->index());
}

void TrackViewItem::delete_all_empty_tracks()
{
	Sequence *sequence = track_->sequence();
	QVector<Track *> tracks_to_remove;
	QStringList track_names_to_remove;

	foreach (Track *t, sequence->get_tracks()) {
		if (t->blocks().isEmpty()) {
			tracks_to_remove.append(t);
			track_names_to_remove.append(t->get_label_or_name());
		}
	}

	if (tracks_to_remove.isEmpty()) {
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
			oakengine_sequence_delete_empty_tracks(
				reinterpret_cast<OakEngineSequence *>(sequence), -1);
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
