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

#include "multicamwidget.h"

#include "oakengine/node.h"

#include <QShortcut>

#include "oakengine/events.h"
#include "oakengine/viewer.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "timeline/timelineundosplit.h"
#include "widget/timeruler/timeruler.h"

namespace olive
{

#define super TimeBasedWidget

MulticamWidget::MulticamWidget(QWidget *parent)
	: super{ false, false, parent }
	, node_(nullptr)
	, clip_(nullptr)
{
	auto layout = new QVBoxLayout(this);

	sizer_ = new ViewerSizer(this);
	layout->addWidget(sizer_);

	display_ = new MulticamDisplay(this);
	display_->set_show_widget_background(true);
	connect(display_, &ViewerDisplayWidget::drag_started, this,
			&MulticamWidget::display_clicked);

	connect(sizer_, &ViewerSizer::request_scale, display_,
			&ViewerDisplayWidget::set_matrix_zoom);
	connect(sizer_, &ViewerSizer::request_translate, display_,
			&ViewerDisplayWidget::set_matrix_translate);
	connect(display_, &ViewerDisplayWidget::hand_drag_moved, sizer_,
			&ViewerSizer::hand_drag_move);
	sizer_->set_widget(display_);

	layout->addWidget(this->ruler());
	layout->addWidget(this->scrollbar());

	for (int i = 0; i < 9; i++) {
		new QShortcut(QStringLiteral("Ctrl+%1").arg(QString::number(i + 1)),
					  this, this, [this, i] { Switch(i, false); });
		new QShortcut(QString::number(i + 1), this, this,
					  [this, i] { Switch(i, true); });
	}
}

void MulticamWidget::set_multicam_node_internal(ViewerOutput *viewer,
											 MultiCamNode *n, ClipBlock *clip)
{
	if (get_connected_node() != viewer) {
		connect_viewer_node(viewer);
	}

	if (node_ != n) {
		node_ = n;
		display_->set_multicam_node(n);
	}

	if (clip_ != clip) {
		clip_ = clip;
	}
}

void MulticamWidget::set_multicam_node(ViewerOutput *viewer, MultiCamNode *n,
									 ClipBlock *clip, const Rational &time)
{
	if (time.isNaN() || !get_connected_node() ||
		time == get_connected_node()->get_playhead()) {
		set_multicam_node_internal(viewer, n, clip);
		play_queue_.clear();
	} else {
		MulticamNodeQueue m = { time, viewer, n, clip };
		play_queue_.push_back(m);
	}
}

void MulticamWidget::ConnectNodeEvent(ViewerOutput *n)
{
	OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(n);

	viewer_sub_ = oakengine_event_subscribe(
		handle, OAKENGINE_EVENT_VIEWER_SIZE_CHANGED,
		[](const oakengine_event *event, void *userdata) {
			auto *w = static_cast<MulticamWidget *>(userdata);
			w->sizer_->set_child_size(int(event->a), int(event->b));
		},
		this);
	viewer_sub2_ = oakengine_event_subscribe(
		handle, OAKENGINE_EVENT_VIEWER_PIXEL_ASPECT_CHANGED,
		[](const oakengine_event *event, void *userdata) {
			auto *w = static_cast<MulticamWidget *>(userdata);
			w->sizer_->set_pixel_aspect_ratio(
				Rational(event->a, event->b));
		},
		this);

	oak_video_params vp;
	oakengine_viewer_get_video_params(handle, 0, &vp);
	sizer_->set_child_size(vp.width, vp.height);
	sizer_->set_pixel_aspect_ratio(
		Rational(vp.pixel_aspect_num, vp.pixel_aspect_den));
}

void MulticamWidget::DisconnectNodeEvent(ViewerOutput *n)
{
	if (viewer_sub_ > 0) {
		oakengine_event_unsubscribe(viewer_sub_);
		viewer_sub_ = 0;
	}
	if (viewer_sub2_ > 0) {
		oakengine_event_unsubscribe(viewer_sub2_);
		viewer_sub2_ = 0;
	}
}

void MulticamWidget::TimeChangedEvent(const Rational &t)
{
	super::TimeChangedEvent(t);

	if (!play_queue_.empty()) {
		const MulticamNodeQueue &m = play_queue_.front();
		if (m.time >= t) {
			set_multicam_node_internal(m.viewer, m.node, m.clip);
			play_queue_.pop_front();
		}
	}
}

void MulticamWidget::Switch(int source, bool split_clip)
{
	if (!node_) {
		return;
	}

	MultiCamNode *cam = node_;
	ClipBlock *clip = clip_;

	const QByteArray undo_name = tr("Switched Multi-Camera Source").toUtf8();
	oakengine_undo_group_begin(undo_name.constData());

	if (clip_ && split_clip &&
		clip_->in() < get_connected_node()->get_playhead() &&
		clip_->out() > get_connected_node()->get_playhead()) {
		QVector<Block *> blocks;

		blocks.append(clip_);
		blocks.append(clip_->block_links());

		int split_tbn = 0, split_tbd = 0;
		oakengine_node_frame_time_base(
			reinterpret_cast<OakEngineNode *>(get_connected_node()),
			&split_tbn, &split_tbd);
		void *split = oakengine_block_split_preserving_links_command(
			reinterpret_cast<void *const *>(blocks.data()), blocks.size(),
			olive::core::Timecode::time_to_timestamp(
				get_connected_node()->get_playhead(),
				olive::Rational(split_tbn, split_tbd),
				olive::core::Timecode::k_round));
		oakengine_undo_push(split, undo_name.constData());
		clip = reinterpret_cast<ClipBlock *>(
			oakengine_block_split_get_split(
				split, reinterpret_cast<void *>(clip_), 0));

		cam = reinterpret_cast<MultiCamNode *>(
			oakengine_clip_find_multicam(reinterpret_cast<OakEngineNode *>(clip)));
	}

	oak_node_value val;
	memset(&val, 0, sizeof(val));
	val.type = OAK_NODE_VALUE_INT;
	val.num = source;

	if (cam) {
		oakengine_node_set_input(
			reinterpret_cast<OakEngineNode *>(cam),
			oakengine_multicam_input_current(), &val);
	}

	if (clip) {
		for (Block *link : clip->block_links()) {
			if (ClipBlock *clink = dynamic_cast<ClipBlock *>(link)) {
				if (MultiCamNode *mlink = reinterpret_cast<MultiCamNode *>(
						oakengine_clip_find_multicam(reinterpret_cast<OakEngineNode *>(clink)))) {
					oakengine_node_set_input(
						reinterpret_cast<OakEngineNode *>(mlink),
						oakengine_multicam_input_current(), &val);
				}
			}
		}
	}

	oakengine_undo_group_end();

	display_->update();

	emit switched();
}

void MulticamWidget::display_clicked(const QPoint &p)
{
	if (!node_) {
		return;
	}

	QPointF click = display_->screen_to_scene_point(p);
	int width = display_->get_video_params().width();
	int height = display_->get_video_params().height();

	if (click.x() < 0 || click.y() < 0 || click.x() >= width ||
		click.y() >= height) {
		return;
	}

	int rows, cols;
	oakengine_multicam_get_rows_and_columns(
		oakengine_multicam_get_source_count(
			reinterpret_cast<OakEngineNode *>(node_)),
		&rows, &cols);

	int multi = std::max(cols, rows);

	int c = click.x() / (width / multi);
	int r = click.y() / (height / multi);

	int source = oakengine_multicam_rows_cols_to_index(r, c, rows, cols);

	Switch(source, true);
}

}
