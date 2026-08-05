#pragma once
#include <string>
#include "value.h"
#include "render/samplebuffer.h"
#include "olive/core/util/timerange.h"
namespace olive {
class SampleJob {
public:
	SampleJob() = default;
	SampleJob(const core::TimeRange &time, const NodeValue &value)
	{
		(void) time; (void) value;
	}
	SampleJob(const core::TimeRange &time, const std::string &from,
			  const NodeValueRow &values)
	{
		(void) time; (void) from; (void) values;
	}
	void insert(const std::string &input, const NodeValueRow &row)
	{
		(void) input; (void) row;
	}
	void insert(const std::string &input, const NodeValue &value)
	{
		(void) input; (void) value;
	}
	bool operator==(const SampleJob &) const { return true; }
	const SampleBuffer &samples() const { static SampleBuffer b; return b; }
	core::TimeRange time() const { return core::TimeRange(); }
};
}
