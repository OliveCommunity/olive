#pragma once
#include <string>
namespace olive {
class DiskManager {
public:
	static DiskManager *instance() { static DiskManager d; return &d; }
	std::string get_default_cache_path() const { return std::string(); }
};
}
