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

#include "tiledistortnode.h"

#include "node/sliderdisplaytype.h"

namespace olive
{

const QString TileDistortNode::k_texture_input = QStringLiteral("tex_in");
const QString TileDistortNode::k_scale_input = QStringLiteral("scale_in");
const QString TileDistortNode::k_position_input = QStringLiteral("position_in");
const QString TileDistortNode::k_anchor_input = QStringLiteral("anchor_in");
const QString TileDistortNode::k_mirror_x_input = QStringLiteral("mirrorx_in");
const QString TileDistortNode::k_mirror_y_input = QStringLiteral("mirrory_in");

#define super Node

TileDistortNode::TileDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_scale_input, NodeValue::k_float, 0.5);
	set_input_property(k_scale_input, QStringLiteral("min"), 0);
	set_input_property(k_scale_input, QStringLiteral("view"),
					 slider::k_percentage);

	add_input(k_position_input, NodeValue::k_vec2, QVector2D(0, 0));

	add_input(k_anchor_input, NodeValue::k_combo, k_middle_center);

	add_input(k_mirror_x_input, NodeValue::k_boolean, false);
	add_input(k_mirror_y_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);

	gizmo_ = add_draggable_gizmo<PointGizmo>({
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 0),
		NodeKeyframeTrackReference(NodeInput(this, k_position_input), 1),
	});
	gizmo_->set_shape(PointGizmo::k_anchor_point);
}

QString TileDistortNode::name() const
{
	return tr("Tile");
}

QString TileDistortNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.tile");
}

QVector<Node::CategoryID> TileDistortNode::category() const
{
	return { k_category_distort };
}

QString TileDistortNode::description() const
{
	return tr("Infinitely tile an image horizontally and vertically.");
}

void TileDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_scale_input, tr("Scale"));
	set_input_name(k_position_input, tr("Position"));
	set_input_name(k_mirror_x_input, tr("Mirror Horizontally"));
	set_input_name(k_mirror_y_input, tr("Mirror Vertically"));

	set_input_name(k_anchor_input, tr("Anchor"));
	set_combo_box_strings(k_anchor_input, {
										 tr("Top-Left"),
										 tr("Top-Center"),
										 tr("Top-Right"),
										 tr("Middle-Left"),
										 tr("Middle-Center"),
										 tr("Middle-Right"),
										 tr("Bottom-Left"),
										 tr("Bottom-Center"),
										 tr("Bottom-Right"),
									 });
}

ShaderCode TileDistortNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/tile.frag"));
}

void TileDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		if (!qFuzzyCompare(value[k_scale_input].to_double(), 1.0)) {
			ShaderJob job(value);
			job.insert(QStringLiteral("resolution_in"),
					   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
								 this));
			table->push(NodeValue::k_texture, tex->to_job(job), this);
		} else {
			// If we're not flipping or flopping just push the texture
			table->push(value[k_texture_input]);
		}
	}
}

void TileDistortNode::update_gizmo_positions(const NodeValueRow &row,
										   const NodeGlobals &globals)
{
	if (TexturePtr tex = row[k_texture_input].to_texture()) {
		QPointF res = tex->virtual_resolution().toPointF();
		QPointF pos = row[k_position_input].to_vec2().toPointF();
		qreal x = pos.x();
		qreal y = pos.y();

		Anchor a = static_cast<Anchor>(row[k_anchor_input].to_int());
		if (a == k_top_left || a == k_top_center || a == k_top_right) {
			// Do nothing
		} else if (a == k_middle_left || a == k_middle_center ||
				   a == k_middle_right) {
			y += res.y() / 2;
		} else if (a == k_bottom_left || a == k_bottom_center ||
				   a == k_bottom_right) {
			y += res.y();
		}
		if (a == k_top_left || a == k_middle_left || a == k_bottom_left) {
			// Do nothing
		} else if (a == k_top_center || a == k_middle_center ||
				   a == k_bottom_center) {
			x += res.x() / 2;
		} else if (a == k_top_right || a == k_middle_right || a == k_bottom_right) {
			x += res.x();
		}

		gizmo_->set_point(QPointF(x, y));
	}
}

void TileDistortNode::gizmo_drag_move(double x, double y,
									const Qt::KeyboardModifiers &modifiers)
{
	NodeInputDragger &x_drag = gizmo_->get_draggers()[0];
	NodeInputDragger &y_drag = gizmo_->get_draggers()[1];

	x_drag.drag(x_drag.get_start_value().toDouble() + x);
	y_drag.drag(y_drag.get_start_value().toDouble() + y);
}

}
