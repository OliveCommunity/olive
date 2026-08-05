#pragma once
// Transitional: engine/audio/audiovisualwaveform.h de-Qt target form
// (oakaudio contract, M6). Data-only surface used by oakrender: the Sample
// reduction types plus the writers renderprocessor/audiowaveformcache call.
// QPainter-based draw_sample() stays in the app layer.
#include <map>
#include <vector>

#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/rational.h"

namespace olive {

using core::Rational;
using core::SampleBuffer;

class AudioVisualWaveform {
public:
	struct SamplePerChannel {
		float min;
		float max;
	};

	using Sample = std::vector<SamplePerChannel>;

	// Same value as the engine original (audiovisualwaveform.cpp)
	inline static const Rational k_minimum_sample_rate = Rational(1, 8);

	int channel_count() const { return int(channels_.size()); }

	void set_channel_count(int channels) { channels_.resize(size_t(channels)); }

	void overwrite_samples(const SampleBuffer &samples, int sample_rate,
						   bool /*from_maximum*/ = false)
	{
		(void) samples;
		(void) sample_rate;
	}

	Sample get_summary_from_time(const Rational &, const Rational &) const
	{
		return Sample();
	}

	static Sample sum_samples(const SampleBuffer &, size_t, size_t)
	{
		return Sample();
	}

	static Sample re_sum_samples(const SamplePerChannel *, int, int)
	{
		return Sample();
	}

	const Rational &length() const { return length_; }

	// Simplified stub-model version of the engine writer: the real
	// implementation walks mipmapped rates; here sums are merged 1:1 per
	// channel at sample granularity (dest/offset in the same time base).
	void overwrite_sums(const AudioVisualWaveform &sums, const Rational &dest,
						const Rational &offset = 0, const Rational &length = 0)
	{
		size_t our_start = size_t(dest.to_double());
		size_t their_start = size_t(offset.to_double());
		size_t count = length.to_double() > 0
						   ? size_t(length.to_double())
						   : SIZE_MAX;
		for (size_t ch = 0; ch < channels_.size() &&
							ch < sums.channels_.size();
			 ch++) {
			Sample &our_arr = channels_[ch];
			const Sample &their_arr = sums.channels_[ch];
			if (our_start + count > our_arr.size()) {
				our_arr.resize(our_start + (count == SIZE_MAX ? their_arr.size() - their_start : count));
			}
			for (size_t i = 0; i < count &&
							   their_start + i < their_arr.size() &&
							   our_start + i < our_arr.size();
				 i++) {
				our_arr[our_start + i] = their_arr[their_start + i];
			}
		}
		Rational end = dest + (length.to_double() > 0 ? length : Rational());
		if (end > length_) {
			length_ = end;
		}
	}

private:
	std::vector<Sample> channels_;
	Rational length_;
};

}
