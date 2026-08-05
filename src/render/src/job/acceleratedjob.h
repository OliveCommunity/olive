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

#ifndef OAK_ACCELERATEDJOB_H
#define OAK_ACCELERATEDJOB_H

#include <string>

#include "param.h"
#include "valuedatabase.h"

namespace olive
{

class AcceleratedJob {
public:
	AcceleratedJob() = default;

	virtual ~AcceleratedJob()
	{
	}

	virtual NodeValue get(const std::string &input) const
	{
		auto it = value_map_.find(input);
		return it == value_map_.end() ? NodeValue() : it->second;
	}

	virtual void insert(const std::string &input, const NodeValueRow &row)
	{
		// QHash::value() semantics: a missing key yields a default NodeValue
		auto it = row.find(input);
		value_map_[input] = it == row.end() ? NodeValue() : it->second;
	}

	virtual void insert(const std::string &input, const NodeValue &value)
	{
		value_map_[input] = value;
	}

	virtual void insert(const NodeValueRow &row)
	{
		value_map_.insert(row.begin(), row.end());
	}

	virtual const NodeValueRow &get_values() const
	{
		return value_map_;
	}
	virtual NodeValueRow &get_values()
	{
		return value_map_;
	}

protected:
	NodeValueRow value_map_;
};

}

#endif // OAK_ACCELERATEDJOB_H
