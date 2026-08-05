#pragma once
#include <vector>
#include "olive/core/render/audioparams.h"
namespace olive { using core::AudioParams; }
namespace olive {
class AudioProcessor {
public:
	using Buffer = std::vector<std::vector<char>>;
	bool open(const AudioParams &, const AudioParams &, double = 1.0) { return true; }
	void close() {}
	bool is_open() const { return false; }
	int convert(float **, int, Buffer *) { return 0; }
	void flush() {}
};
}
