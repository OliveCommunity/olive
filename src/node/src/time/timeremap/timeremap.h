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

#ifndef OAK_TIMEREMAPNODE_H
#define OAK_TIMEREMAPNODE_H

#include "node.h"

namespace olive
{

class TimeRemapNode : public Node {
public:
	TimeRemapNode();

	NODE_DEFAULT_FUNCTIONS(TimeRemapNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual TimeRange input_time_adjustment(const std::string &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const override;
	virtual TimeRange
	output_time_adjustment(const std::string &input, int element,
						 const TimeRange &input_time) const override;

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const std::string k_time_input;
	static const std::string k_input_input;

private:
	Rational get_remapped_time(const Rational &input) const;
};

}

#endif // OAK_TIMEREMAPNODE_H
