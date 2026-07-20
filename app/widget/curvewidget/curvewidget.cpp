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

#include "curvewidget.h"

#include <QEvent>
#include <QLabel>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

#include "core.h"
#include "common/qtutils.h"
#include "node/node.h"
#include "oakengine/node.h"
#include "widget/timeruler/timeruler.h"

namespace olive
{

#define super TimeBasedWidget

CurveWidget::CurveWidget(QWidget *parent)
	: super(parent)
{
	QHBoxLayout *outer_layout = new QHBoxLayout(this);

	QSplitter *splitter = new QSplitter();
	outer_layout->addWidget(splitter);

	tree_view_ = new NodeTreeView();
	tree_view_->set_only_show_keyframable(true);
	tree_view_->set_show_keyframe_tracks_as_rows(true);
	connect(tree_view_, &NodeTreeView::input_selection_changed, this,
			&CurveWidget::input_selection_changed);
	splitter->addWidget(tree_view_);

	QWidget *workarea = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(workarea);
	layout->setContentsMargins(0, 0, 0, 0);
	splitter->addWidget(workarea);

	QHBoxLayout *top_controls = new QHBoxLayout();

	key_control_ = new NodeParamViewKeyframeControl(false);
	top_controls->addWidget(key_control_);

	top_controls->addStretch();

	linear_button_ = new QPushButton(tr("Linear"));
	linear_button_->setCheckable(true);
	linear_button_->setEnabled(false);
	top_controls->addWidget(linear_button_);
	connect(linear_button_, &QPushButton::clicked, this,
			&CurveWidget::keyframe_type_button_triggered);

	bezier_button_ = new QPushButton(tr("Bezier"));
	bezier_button_->setCheckable(true);
	bezier_button_->setEnabled(false);
	top_controls->addWidget(bezier_button_);
	connect(bezier_button_, &QPushButton::clicked, this,
			&CurveWidget::keyframe_type_button_triggered);

	hold_button_ = new QPushButton(tr("Hold"));
	hold_button_->setCheckable(true);
	hold_button_->setEnabled(false);
	top_controls->addWidget(hold_button_);
	connect(hold_button_, &QPushButton::clicked, this,
			&CurveWidget::keyframe_type_button_triggered);

	layout->addLayout(top_controls);

	// We use a separate layout for the ruler+view combination so that there's no spacing between them
	QVBoxLayout *ruler_view_layout = new QVBoxLayout();
	ruler_view_layout->setContentsMargins(0, 0, 0, 0);
	ruler_view_layout->setSpacing(0);

	ruler_view_layout->addWidget(ruler());

	view_ = new CurveView();
	connect_timeline_view(view_);
	view_->set_snap_service(this);
	ruler_view_layout->addWidget(view_);

	layout->addLayout(ruler_view_layout);

	// Connect ruler and view together
	connect(view_, &CurveView::selection_changed, this,
			&CurveWidget::selection_changed);
	connect(view_, &CurveView::dragged, this,
			&CurveWidget::keyframe_view_dragged);
	connect(view_, &CurveView::released, this,
			&CurveWidget::keyframe_view_released);

	// TimeBasedWidget's scrollbar has extra functionality that we can take advantage of
	view_->setHorizontalScrollBar(scrollbar());

	// Disable collapsing the main curve view (but allow collapsing the tree)
	splitter->setCollapsible(1, false);

	SetScale(120.0);
}

const double &CurveWidget::get_vertical_scale()
{
	return view_->get_y_scale();
}

void CurveWidget::set_vertical_scale(const double &vscale)
{
	view_->set_y_scale(vscale);
}

void CurveWidget::DeleteSelected()
{
	view_->delete_selected();
}

Node *CurveWidget::get_selected_node_with_id(const QString &id)
{
	for (auto it = view_->get_connections().cbegin();
		 it != view_->get_connections().cend(); it++) {
		Node *n = it.key().input().node();
		if (n->id() == id) {
			return n;
		}
	}

	return nullptr;
}

bool CurveWidget::copy_selected(bool cut)
{
	if (super::copy_selected(cut)) {
		return true;
	}

	return view_->copy_selected(cut);
}

bool CurveWidget::paste()
{
	if (super::paste()) {
		return true;
	}

	return view_->paste(std::bind(&CurveWidget::get_selected_node_with_id, this,
								  std::placeholders::_1));
}

void CurveWidget::set_nodes(const QVector<Node *> &nodes)
{
	tree_view_->set_nodes(nodes);

	// Save new node list
	nodes_ = nodes;

	// Generate colors
	foreach (Node *node, nodes_) {
		foreach (const QString &input, node->inputs()) {
			if (node->is_input_keyframable(input) &&
				!node->is_input_hidden(input)) {
				int arr_sz = node->input_array_size(input);
				for (int i = -1; i < arr_sz; i++) {
					// Generate a random color for this input
					const QVector<NodeKeyframeTrack> &tracks =
						node->get_keyframe_tracks(input, i);

					for (int j = 0; j < tracks.size(); j++) {
						NodeKeyframeTrackReference ref(
							NodeInput(node, input, i), j);

						if (!keyframe_colors_.contains(ref)) {
							QColor c =
								QColor::fromHsl(std::rand() % 360, 255, 160);

							keyframe_colors_.insert(ref, c);
							tree_view_->set_keyframe_track_color(ref, c);
							view_->set_keyframe_track_color(ref, c);
						}
					}
				}
			}
		}
	}
}

void CurveWidget::TimebaseChangedEvent(const Rational &timebase)
{
	super::TimebaseChangedEvent(timebase);

	view_->set_timebase(timebase);
}

void CurveWidget::ScaleChangedEvent(const double &scale)
{
	super::ScaleChangedEvent(scale);

	view_->set_scale(scale);
}

void CurveWidget::TimeTargetChangedEvent(ViewerOutput *target)
{
	TimeTargetObject::TimeTargetChangedEvent(target);

	key_control_->set_time_target(target);

	view_->set_time_target(target);
}

void CurveWidget::ConnectedNodeChangeEvent(ViewerOutput *n)
{
	super::ConnectedNodeChangeEvent(n);

	key_control_->set_time_target(n);

	set_time_target(n);
}

void CurveWidget::set_keyframe_button_enabled(bool enable)
{
	linear_button_->setEnabled(enable);
	bezier_button_->setEnabled(enable);
	hold_button_->setEnabled(enable);
}

void CurveWidget::set_keyframe_button_checked(bool checked)
{
	linear_button_->setChecked(checked);
	bezier_button_->setChecked(checked);
	hold_button_->setChecked(checked);
}

void CurveWidget::set_keyframe_button_checked_from_type(NodeKeyframe::Type type)
{
	linear_button_->setChecked(type == NodeKeyframe::k_linear);
	bezier_button_->setChecked(type == NodeKeyframe::k_bezier);
	hold_button_->setChecked(type == NodeKeyframe::k_hold);
}

void CurveWidget::connect_input(Node *node, const QString &input, int element)
{
	if (element == -1 && node->input_is_array(input)) {
		// This is the root element, connect all elements (if applicable)
		int arr_sz = node->input_array_size(input);
		for (int i = -1; i < arr_sz; i++) {
			connect_input_internal(node, input, i);
		}
	} else {
		// This is a single element, just connect it as-is
		connect_input_internal(node, input, element);
	}
}

void CurveWidget::connect_input_internal(Node *node, const QString &input,
									   int element)
{
	NodeInput input_ref(node, input, element);
	int track_count =
		NodeValue::get_number_of_keyframe_tracks(input_ref.get_data_type());
	for (int i = 0; i < track_count; i++) {
		NodeKeyframeTrackReference track_ref(input_ref, i);
		view_->connect_input(track_ref);
		selected_tracks_.append(track_ref);
	}
}

void CurveWidget::selection_changed()
{
	const std::vector<NodeKeyframe *> &selected = view_->get_selected_keyframes();

	set_keyframe_button_checked(false);
	set_keyframe_button_enabled(!selected.empty());

	if (!selected.empty()) {
		bool all_same_type = true;
		NodeKeyframe::Type type = selected.front()->type();

		for (size_t i = 1; i < selected.size(); i++) {
			NodeKeyframe *prev_item = selected.at(i - 1);
			NodeKeyframe *this_item = selected.at(i);

			if (prev_item->type() != this_item->type()) {
				all_same_type = false;
				break;
			}
		}

		if (all_same_type) {
			set_keyframe_button_checked_from_type(type);
		}
	}
}

void CurveWidget::keyframe_type_button_triggered(bool checked)
{
	QPushButton *key_btn = static_cast<QPushButton *>(sender());

	if (!checked) {
		// Keyframe buttons cannot be checked off, we undo this action here
		key_btn->setChecked(true);
		return;
	}

	// Get selected items and do nothing if there are none
	const std::vector<NodeKeyframe *> &selected = view_->get_selected_keyframes();
	if (selected.empty()) {
		return;
	}

	// Set all selected keyframes to this type
	NodeKeyframe::Type new_type;

	// Determine which type to set
	if (key_btn == bezier_button_) {
		new_type = NodeKeyframe::k_bezier;
	} else if (key_btn == hold_button_) {
		new_type = NodeKeyframe::k_hold;
	} else {
		new_type = NodeKeyframe::k_linear;
	}

	// Ensure only the appropriate button is checked
	set_keyframe_button_checked_from_type(new_type);

	// Through the liboakengine C ABI facade: one undoable command per
	// distinct input (usually just one), with the same batch semantics as
	// the old per-keyframe commands.
	struct TypeGroup {
		Node *node;
		QString input;
		int element;
		QVector<int64_t> times;
		QVector<int> tracks;
	};
	QVector<TypeGroup> groups;
	foreach (NodeKeyframe *item, selected) {
		int g = 0;
		for (; g < groups.size(); g++) {
			if (groups.at(g).node == item->parent() &&
				groups.at(g).input == item->input() &&
				groups.at(g).element == item->element()) {
				break;
			}
		}
		if (g == groups.size()) {
			groups.append({ item->parent(), item->input(), item->element(),
							{}, {} });
		}
		OakEngineNode *handle =
			reinterpret_cast<OakEngineNode *>(item->parent());
		int tbn = 0, tbd = 0;
		oakengine_node_frame_time_base(handle, &tbn, &tbd);
		groups[g].times.append(Timecode::time_to_timestamp(
			item->time(), Rational(tbn, tbd), Timecode::k_round));
		groups[g].tracks.append(item->track());
	}
	int facade_type = 0;
	if (new_type == NodeKeyframe::k_bezier) {
		facade_type = 1;
	} else if (new_type == NodeKeyframe::k_hold) {
		facade_type = 2;
	}
	foreach (const TypeGroup &g, groups) {
		oakengine_node_keyframes_set_type_many(
			reinterpret_cast<OakEngineNode *>(g.node),
			g.input.toUtf8().constData(), g.element, g.times.constData(),
			g.tracks.data(), g.times.size(), facade_type);
	}
}

void CurveWidget::input_selection_changed(const NodeKeyframeTrackReference &ref)
{
	key_control_->set_input(ref.input());

	foreach (const NodeKeyframeTrackReference &c, selected_tracks_) {
		view_->disconnect_input(c);
	}

	selected_tracks_.clear();

	if (ref.is_valid() && !ref.input().is_array()) {
		// This reference is a track, connect it only
		view_->connect_input(ref);
		selected_tracks_.append(ref);
	} else if (ref.input().is_valid()) {
		// This reference is a input, connect all tracks
		connect_input(ref.input().node(), ref.input().input(),
					 ref.input().element());
	} else if (Node *node = ref.input().node()) {
		// This is a node, add all inputs
		foreach (const QString &input, node->inputs()) {
			if (node->is_input_keyframable(input) &&
				!node->is_input_hidden(input)) {
				connect_input(node, input, -1);
			}
		}
	}

	view_->zoom_to_fit();
}

void CurveWidget::keyframe_view_dragged(int x, int y)
{
	set_catch_up_scroll_value(x);
	set_catch_up_scroll_value(view_->verticalScrollBar(), y, view_->height());
}

void CurveWidget::keyframe_view_released()
{
	stop_catch_up_scroll_timer();
	stop_catch_up_scroll_timer(view_->verticalScrollBar());
}

}
