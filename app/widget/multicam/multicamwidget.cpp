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

#include <QShortcut>

#include "node/nodeundo.h"
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
	connect(n, &ViewerOutput::size_changed, sizer_, &ViewerSizer::set_child_size);
	connect(n, &ViewerOutput::pixel_aspect_changed, sizer_,
			&ViewerSizer::set_pixel_aspect_ratio);

	VideoParams vp = n->get_video_params();
	sizer_->set_child_size(vp.width(), vp.height());
	sizer_->set_pixel_aspect_ratio(vp.pixel_aspect_ratio());
}

void MulticamWidget::DisconnectNodeEvent(ViewerOutput *n)
{
	disconnect(n, &ViewerOutput::size_changed, sizer_,
			   &ViewerSizer::set_child_size);
	disconnect(n, &ViewerOutput::pixel_aspect_changed, sizer_,
			   &ViewerSizer::set_pixel_aspect_ratio);
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

	MultiUndoCommand *command = new MultiUndoCommand();

	MultiCamNode *cam = node_;
	ClipBlock *clip = clip_;

	BlockSplitPreservingLinksCommand *split = nullptr;

	if (clip_ && split_clip &&
		clip_->in() < get_connected_node()->get_playhead() &&
		clip_->out() > get_connected_node()->get_playhead()) {
		QVector<Block *> blocks;

		blocks.append(clip_);
		blocks.append(clip_->block_links());

		split = new BlockSplitPreservingLinksCommand(
			blocks, { get_connected_node()->get_playhead() });
		split->redo_now();
		command->add_child(split);

		clip = static_cast<ClipBlock *>(split->get_split(clip_, 0));

		cam = clip->find_multicam();
	}

	command->add_child(new NodeParamSetStandardValueCommand(
		NodeKeyframeTrackReference(NodeInput(cam, cam->k_current_input)),
		source));

	for (Block *link : clip->block_links()) {
		if (ClipBlock *clink = dynamic_cast<ClipBlock *>(link)) {
			if (MultiCamNode *mlink = clink->find_multicam()) {
				command->add_child(new NodeParamSetStandardValueCommand(
					NodeKeyframeTrackReference(
						NodeInput(mlink, mlink->k_current_input)),
					source));
			}
		}
	}

	Core::instance()->undo_stack()->push(command,
										 tr("Switched Multi-Camera Source"));

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
	node_->get_rows_and_columns(&rows, &cols);

	int multi = std::max(cols, rows);

	int c = click.x() / (width / multi);
	int r = click.y() / (height / multi);

	int source = node_->rows_cols_to_index(r, c, rows, cols);

	Switch(source, true);
}

}
