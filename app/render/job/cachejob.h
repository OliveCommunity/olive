/***
  This file is part of Oak Video Editor - A fork of original project Olive 

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team

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

#ifndef CACHEJOB_H
#define CACHEJOB_H

#include <cstdint>
#include <QVariant>
#include <QUuid>

#include "node/value.h"
#include "render/job/acceleratedjob.h"

namespace olive
{

class CacheJob : public AcceleratedJob {
public:
	CacheJob() = default;
	CacheJob(const QUuid &uuid, const int64_t &time,
			 const NodeValue &fallback = NodeValue())
	{
		uuid_ = uuid;
		time_ = time;
		fallback_ = fallback;
	}

	const QUuid &GetUuid() const
	{
		return uuid_;
	}
	void SetUuid(const QUuid &u)
	{
		uuid_ = u;
	}

	const int64_t &GetTime() const
	{
		return time_;
	}
	void SetTime(const int64_t &t)
	{
		time_ = t;
	}

	const NodeValue &GetFallback() const
	{
		return fallback_;
	}
	void SetFallback(const NodeValue &val)
	{
		fallback_ = val;
	}

private:
	QUuid uuid_;
	int64_t time_ = 0;

	NodeValue fallback_;
};

}

#endif // CACHEJOB_H
