#pragma once
#include "param.h"
#include "value.h"
namespace olive {
class AcceleratedJob {
public:
	virtual ~AcceleratedJob() = default;
	virtual void insert(const std::string &input, const NodeValueRow &row)
	{
		(void) input; (void) row;
	}
	virtual void insert(const std::string &input, const NodeValue &value)
	{
		(void) input; (void) value;
	}
	virtual void insert(const NodeValueRow &row)
	{
		(void) row;
	}
	virtual const NodeValueRow &get_values() const { static NodeValueRow r; return r; }
	virtual NodeValueRow &get_values() { static NodeValueRow r; return r; }
};
}
