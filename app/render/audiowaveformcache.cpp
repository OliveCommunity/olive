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

AudioWaveformCache::AudioWaveformCache(QObject *parent)
	: super{ parent }
{
	waveforms_ = std::make_shared<AudioVisualWaveform>();
}

void AudioWaveformCache::write_waveform(const TimeRange &range,
									   const TimeRangeList &valid_ranges,
									   const AudioVisualWaveform *waveform)
{
	// Write each valid range to the segments
	foreach (const TimeRange &r, valid_ranges) {
		if (waveform) {
			waveforms_->overwrite_sums(*waveform, r.in(), r.in() - range.in(),
									  r.length());
		}

		validate(r);
	}
}

void draw_sub_rect(QPainter *painter, const QRect &rect, const double &scale,
				 const TimeRange &wave_range,
				 const AudioVisualWaveform &waveform, const TimeRange &subrange)
{
	// Find start time of passthrough
	TimeRange intersect = wave_range.intersected(subrange);

	// Create new rect that starts at the offset of pass_start from start_time
	// Set rect width to either length of passthrough or until the end
	QRect pass_rect(
		rect.x() + (intersect.in() - wave_range.in()).to_double() * scale,
		rect.y(), intersect.length().to_double() * scale, rect.height());

	// Draw waveform with this info
	AudioVisualWaveform::draw_waveform(painter, pass_rect, scale, waveform,
									  intersect.in());
}

void AudioWaveformCache::Draw(QPainter *painter, const QRect &rect,
							  const double &scale,
							  const Rational &start_time) const
{
	if (!passthroughs_.empty()) {
		TimeRange wave_range(start_time,
							 start_time +
								 Rational::from_double(rect.width() / scale));
		TimeRangeList draw_range = { wave_range };
		for (const WaveformPassthrough &p : passthroughs_) {
			if (draw_range.overlaps_with(p, true, false)) {
				draw_sub_rect(painter, rect, scale, wave_range, *p.waveform, p);

				// Remove this range
				draw_range.remove(p);
			}
		}

		for (const TimeRange &r : draw_range) {
			draw_sub_rect(painter, rect, scale, wave_range, *waveforms_, r);
		}
	} else {
		AudioVisualWaveform::draw_waveform(painter, rect, scale, *waveforms_,
										  start_time);
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
