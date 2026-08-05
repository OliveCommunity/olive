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

#ifndef OAK_CACHEJOB_H
#define OAK_CACHEJOB_H

#include <string>

#include "value.h"
#include "acceleratedjob.h"

namespace olive
{

class CacheJob : public AcceleratedJob {
public:
	CacheJob() = default;
	CacheJob(const std::string &filename, const NodeValue &fallback = NodeValue())
	{
		filename_ = filename;
	}

	const std::string &get_filename() const
	{
		return filename_;
	}
	void set_filename(const std::string &s)
	{
		filename_ = s;
	}

	const NodeValue &get_fallback() const
	{
		return fallback_;
	}
	void set_fallback(const NodeValue &val)
	{
		fallback_ = val;
	}

private:
	std::string filename_;

	NodeValue fallback_;
};

}

#endif // OAK_CACHEJOB_H
