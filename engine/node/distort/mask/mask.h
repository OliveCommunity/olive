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

#ifndef OAK_MASKDISTORTNODE_H
#define OAK_MASKDISTORTNODE_H

#include "node/generator/polygon/polygon.h"

namespace olive
{

class MaskDistortNode : public PolygonGenerator {
	Q_OBJECT
public:
	MaskDistortNode();

	NODE_DEFAULT_FUNCTIONS(MaskDistortNode)

	virtual QString name() const override
	{
		return tr("Mask");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.Olive.mask");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_distort };
	}

	virtual QString description() const override
	{
		return tr("Apply a polygonal mask.");
	}

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const QString k_invert_input;
	static const QString k_feather_input;
};

}

#endif // OAK_MASKDISTORTNODE_H
