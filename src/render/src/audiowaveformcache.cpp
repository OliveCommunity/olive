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

#include "audiowaveformcache.h"

namespace olive
{

#define super PlaybackCache

AudioWaveformCache::AudioWaveformCache(Node *parent)
	: super{ parent }
{
	waveforms_ = std::make_shared<AudioVisualWaveform>();
}

void AudioWaveformCache::write_waveform(const TimeRange &range,
									   const TimeRangeList &valid_ranges,
									   const AudioVisualWaveform *waveform)
{
	// Write each valid range to the segments
	for (const TimeRange &r : valid_ranges) {
		if (waveform) {
			waveforms_->overwrite_sums(*waveform, r.in(), r.in() - range.in(),
									  r.length());
		}

		validate(r);
	}
}

AudioVisualWaveform::Sample
AudioWaveformCache::get_summary_from_time(const Rational &start,
									   const Rational &length) const
{
	return waveforms_->get_summary_from_time(start, length);
}

Rational AudioWaveformCache::length() const
{
	return waveforms_->length();
}

void AudioWaveformCache::set_passthrough(PlaybackCache *cache)
{
	AudioWaveformCache *c = static_cast<AudioWaveformCache *>(cache);

	for (const TimeRange &r : c->get_validated_ranges()) {
		WaveformPassthrough t = r;
		t.waveform = c->waveforms_;
		passthroughs_.push_back(t);
	}
	passthroughs_.insert(passthroughs_.end(), c->passthroughs_.begin(),
						 c->passthroughs_.end());

	set_parameters(c->get_parameters());
	set_saving_enabled(c->is_saving_enabled());
}

void AudioWaveformCache::InvalidateEvent(const TimeRange &range)
{
	TimeRangeList::util_remove(&passthroughs_, range);

	super::InvalidateEvent(range);
}

}
