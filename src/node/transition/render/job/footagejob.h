#pragma once
#include <string>
#include "render/job/acceleratedjob.h"
#include "videoparams.h"
#include "olive/core/render/audioparams.h"
namespace olive { using core::AudioParams; }
#include "loopmode.h"
#include "olive/core/util/timerange.h"
#include "output/track/track.h"
namespace olive {
class FootageJob : public AcceleratedJob {
public:
	FootageJob() {}
	FootageJob(const core::TimeRange &, const std::string &,
			   const std::string &, Track::Type, const core::Rational &,
			   LoopMode) {}
	void set_proxy(const std::string &, const std::string &, int) {}
	void set_video_params(const VideoParams &) {}
	void set_audio_params(const AudioParams &) {}
	void set_cache_path(const std::string &) {}
	core::TimeRange time() const { return core::TimeRange(); }
	int loop_mode_as_int() const { return 0; }
	LoopMode loop_mode() const { return LoopMode::k_loop_mode_off; }
	core::Rational length() const { return core::Rational(); }
	const VideoParams &video_params() const { static VideoParams p; return p; }
};
}
