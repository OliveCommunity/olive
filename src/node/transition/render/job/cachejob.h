#pragma once
#include <string>
#include "render/job/acceleratedjob.h"
namespace olive {
class CacheJob : public AcceleratedJob {
public:
	CacheJob(const std::string &, const NodeValue &) {}
	TexturePtr get_fallback() const { return nullptr; }
};
}
