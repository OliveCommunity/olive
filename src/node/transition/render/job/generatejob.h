#pragma once
#include "render/job/acceleratedjob.h"
#include "value.h"
namespace olive {
class GenerateJob : public AcceleratedJob {
public:
	GenerateJob() {}
	explicit GenerateJob(const NodeValueRow &) {}
	NodeValue get(const std::string &) const { return NodeValue(); }
};
}
