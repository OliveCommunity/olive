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

#ifndef OAK_SWIRLDISTORTNODE_H
#define OAK_SWIRLDISTORTNODE_H

#include "node/gizmo/point.h"
#include "node/node.h"

namespace olive
{

class SwirlDistortNode : public Node {
	Q_OBJECT
public:
	SwirlDistortNode();

	NODE_DEFAULT_FUNCTIONS(SwirlDistortNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	static const QString k_texture_input;
	static const QString k_radius_input;
	static const QString k_angle_input;
	static const QString k_position_input;

protected slots:
	virtual void gizmo_drag_move(double x, double y,
							   const Qt::KeyboardModifiers &modifiers) override;

private:
	PointGizmo *gizmo_;
};

}

#endif // OAK_SWIRLDISTORTNODE_H
