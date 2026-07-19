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

#ifndef OAK_BLURFILTERNODE_H
#define OAK_BLURFILTERNODE_H

#include "node/gizmo/point.h"
#include "node/node.h"

namespace olive
{

class BlurFilterNode : public Node {
	Q_OBJECT
public:
	BlurFilterNode();

	enum Method { k_box, k_gaussian, k_directional, k_radial };

	NODE_DEFAULT_FUNCTIONS(BlurFilterNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	Method get_method() const
	{
		return static_cast<Method>(get_standard_value(k_method_input).toInt());
	}

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	static const QString k_texture_input;
	static const QString k_method_input;
	static const QString k_radius_input;
	static const QString k_horiz_input;
	static const QString k_vert_input;
	static const QString k_repeat_edge_pixels_input;

	static const QString k_directional_degrees_input;

	static const QString k_radial_center_input;

protected slots:
	virtual void gizmo_drag_move(double x, double y,
							   const Qt::KeyboardModifiers &modifiers) override;

protected:
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

private:
	void update_inputs(Method method);

	PointGizmo *radial_center_gizmo_;
};

}

#endif // OAK_BLURFILTERNODE_H
