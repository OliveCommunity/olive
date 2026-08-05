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

#ifndef OAK_VALUENODE_H
#define OAK_VALUENODE_H

#include "node.h"

namespace olive
{

class ValueNode : public Node {
public:
	ValueNode();

	NODE_DEFAULT_FUNCTIONS(ValueNode)

	virtual std::string name() const override
	{
		return "Value";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.value";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_generator };
	}

	virtual std::string description() const override
	{
		return "Create a single value that can be connected to various other inputs.";
	}

	static const std::string k_type_input;
	static const std::string k_value_input;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

protected:
	virtual void InputValueChangedEvent(const std::string &input,
										int element) override;

private:
	static const std::vector<NodeValue::Type> k_supported_types;
};

}

#endif // OAK_VALUENODE_H
