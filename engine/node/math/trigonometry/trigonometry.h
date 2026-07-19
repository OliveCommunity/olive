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

#ifndef OAK_TRIGNODE_H
#define OAK_TRIGNODE_H

#include "node/node.h"

namespace olive
{

class TrigonometryNode : public Node {
	Q_OBJECT
public:
	TrigonometryNode();

	NODE_DEFAULT_FUNCTIONS(TrigonometryNode)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_method_in;
	static const QString k_x_in;

private:
	enum Operation {
		k_op_sine,
		k_op_cosine,
		k_op_tangent,
		k_op_arc_sine,
		k_op_arc_cosine,
		k_op_arc_tangent,
		k_op_hyp_sine,
		k_op_hyp_cosine,
		k_op_hyp_tangent
	};
};

}

#endif // OAK_TRIGNODE_H
