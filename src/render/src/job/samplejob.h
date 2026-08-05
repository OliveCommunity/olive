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

#ifndef OAK_SAMPLEJOB_H
#define OAK_SAMPLEJOB_H

#include "acceleratedjob.h"
#include "olive/core/util/timerange.h"

namespace olive
{

using core::TimeRange;

class SampleJob : public AcceleratedJob {
public:
	SampleJob()
	{
	}

	SampleJob(const TimeRange &time, const NodeValue &value)
	{
		samples_ = value.to_samples();
		time_ = time;
	}

	SampleJob(const TimeRange &time, const std::string &from,
			  const NodeValueRow &row)
	{
		samples_ = row.at(from).to_samples();
		time_ = time;
	}

	const SampleBuffer &samples() const
	{
		return samples_;
	}

	bool has_samples() const
	{
		return samples_.is_allocated();
	}

	const TimeRange &time() const
	{
		return time_;
	}

private:
	SampleBuffer samples_;

	TimeRange time_;
};

}

#endif // OAK_SAMPLEJOB_H
