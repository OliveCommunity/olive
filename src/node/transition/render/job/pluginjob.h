#pragma once
#include "render/job/acceleratedjob.h"
#include "olive/core/util/rational.h"
namespace olive {
class Node;
namespace plugin {
class PluginJob : public AcceleratedJob {
public:
	template <typename InstanceT>
	PluginJob(InstanceT *, const Node *, const NodeValueRow &,
			  const core::Rational &)
	{
	}
};
}
}
