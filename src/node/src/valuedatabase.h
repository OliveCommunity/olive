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

#ifndef OAK_NODEVALUEDATABASE_H
#define OAK_NODEVALUEDATABASE_H

#include "param.h"
#include "value.h"

namespace olive
{

class NodeValueDatabase {
public:
	NodeValueDatabase() = default;

	NodeValueTable &operator[](const std::string &input_id)
	{
		return tables_[input_id];
	}

	void insert(const std::string &key, const NodeValueTable &value)
	{
		tables_[key] = value;
	}

	NodeValueTable take(const std::string &key)
	{
		auto it = tables_.find(key);
		if (it == tables_.end()) {
			return NodeValueTable();
		}
		NodeValueTable value = std::move(it->second);
		tables_.erase(it);
		return value;
	}

	NodeValueTable merge() const;

	using Tables = std::map<std::string, NodeValueTable>;
	using const_iterator = Tables::const_iterator;
	using iterator = Tables::iterator;

	inline const_iterator cbegin() const
	{
		return tables_.cbegin();
	}

	inline const_iterator cend() const
	{
		return tables_.cend();
	}

	inline iterator begin()
	{
		return tables_.begin();
	}

	inline iterator end()
	{
		return tables_.end();
	}

	inline bool contains(const std::string &s) const
	{
		return tables_.count(s);
	}

private:
	Tables tables_;
};

}

#endif // OAK_NODEVALUEDATABASE_H
