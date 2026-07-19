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

#ifndef OAK_SHAPENODE_H
#define OAK_SHAPENODE_H

#include "shapenodebase.h"

namespace olive
{

class ShapeNode : public ShapeNodeBase {
	Q_OBJECT
public:
	ShapeNode();

	enum Type { k_rectangle, k_ellipse, k_rounded_rectangle };

	NODE_DEFAULT_FUNCTIONS(ShapeNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static QString k_type_input;
	static QString k_radius_input;

protected:
	virtual void InputValueChangedEvent(const QString &input,
										int element) override;
};

}

#endif // OAK_SHAPENODE_H
