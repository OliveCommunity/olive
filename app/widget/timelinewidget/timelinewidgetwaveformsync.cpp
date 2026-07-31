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

#include "timelinewidgetwaveformsync.h"

#include <cmath>

#include "olive/core/util/timecodefunctions.h"
#include "oakengine/viewer.h"
#include "widget/timelinewidget/cliphandle.h"

namespace olive
{

namespace timeline_waveform_sync
{

namespace
{

/**
 * @brief Validated ranges of a waveform cache as a TimeRangeList.
 *
 * WRAPPER-GAP: the C ABI has no waveform-specific validated-ranges accessor;
 * oakengine_playback_cache_valid_ranges() is reused instead. That function
 * reinterprets the handle as PlaybackCache, which is sound here because
 * AudioWaveformCache derives (single inheritance) from PlaybackCache.
 */
TimeRangeList waveform_validated_ranges(const void *waveform)
{
	TimeRangeList list;
	QVector<int64_t> quads(4 * 64);
	int count;
	while ((count = oakengine_playback_cache_valid_ranges(
				static_cast<OakEnginePlaybackCache *>(
					const_cast<void *>(waveform)),
				quads.data(), quads.size() / 4)) == quads.size() / 4) {
		quads.resize(quads.size() * 2);
	}
	for (int i = 0; i < count; i++) {
		list.insert(TimeRange(Rational(static_cast<int>(quads.at(i * 4 + 0)),
									   static_cast<int>(quads.at(i * 4 + 1))),
							  Rational(static_cast<int>(quads.at(i * 4 + 2)),
									   static_cast<int>(quads.at(i * 4 + 3)))));
	}
	return list;
}

/**
 * @brief Peak over [t, t + length) across all channels
 * (AudioWaveformCache::get_summary_from_time() equivalent). 0 when the
 * summary is unavailable.
 */
double waveform_window_peak(const void *waveform, const Rational &t,
							const Rational &length, int sample_rate)
{
	// The summary C ABI works in sample frames at the cache's sample rate
	const Rational sample_tb(1, sample_rate);
	const int64_t start_ts = core::Timecode::time_to_timestamp(
		t, sample_tb, core::Timecode::k_round);
	const int64_t end_ts = core::Timecode::time_to_timestamp(
		t + length, sample_tb, core::Timecode::k_round);

	double min_vals[64], max_vals[64];
	int channels = 0;
	if (oakengine_waveform_cache_get_summary(waveform, start_ts, end_ts,
											min_vals, max_vals, 64,
											&channels) != OAKENGINE_OK) {
		return 0.0;
	}

	double peak = 0.0;
	for (int i = 0; i < channels; i++) {
		const double channel_peak =
			std::max(std::abs(min_vals[i]), std::abs(max_vals[i]));
		peak = std::max(peak, channel_peak);
	}
	return peak;
}

} // namespace

bool get_waveform_sync_clip(OakEngineBlock *block, WaveformSyncClip *out)
{
	if (!block ||
		!oakengine_node_is_clip(reinterpret_cast<OakEngineNode *>(block))) {
		return false;
	}
	OakEngineBlock *clip = block;

	const void *waveform = clip_waveform(clip);
	if (!waveform) {
		return false;
	}

	const TimeRange media_range = clip_media_range(clip);
	if (media_range.length().isNull()) {
		return false;
	}

	const int sample_rate = oakengine_waveform_cache_sample_rate(waveform);
	if (sample_rate <= 0) {
		return false;
	}

	// Allow waveform sync as long as at least some portion of the clip's
	// media range has been validated. Requiring the entire range to be
	// validated makes the menu item stay disabled for long clips and gives
	// the appearance that "nothing happens" when the user tries to sync.
	const TimeRangeList validated_ranges =
		waveform_validated_ranges(waveform).intersects(media_range);
	if (validated_ranges.isEmpty()) {
		return false;
	}

	out->clip = clip;
	out->waveform = waveform;
	out->media_range = media_range;
	out->sample_rate = sample_rate;
	return true;
}

QVector<WaveformSyncClip>
get_selected_waveform_sync_clips(const QVector<OakEngineBlock *> &blocks)
{
	QVector<WaveformSyncClip> clips;
	for (OakEngineBlock *block : blocks) {
		WaveformSyncClip sync_clip;
		if (get_waveform_sync_clip(block, &sync_clip)) {
			clips.append(sync_clip);
		}
	}
	return clips;
}

QVector<double> extract_waveform_cache_envelope(const WaveformSyncClip &clip,
											 int sample_rate,
											 size_t window_samples,
											 QVector<bool> *valid_mask)
{
	QVector<double> envelope;
	if (sample_rate <= 0 || !window_samples) {
		return envelope;
	}

	if (valid_mask) {
		valid_mask->clear();
	}

	const Rational window_time(static_cast<int>(window_samples), sample_rate);

	// Only trust regions that have actually been validated. Unvalidated cache
	// returns zero samples, which both drags the correlation score down and
	// can produce false peaks if one clip happens to have more cached data
	// than another. Zero placeholders keep every envelope aligned to the same
	// absolute timeline, while the validity mask lets the correlation skip
	// those placeholders entirely.
	const TimeRangeList validated_ranges =
		waveform_validated_ranges(clip.waveform).intersects(clip.media_range);

	for (Rational t = clip.media_range.in(); t < clip.media_range.out();
		 t += window_time) {
		const Rational length = qMin(window_time, clip.media_range.out() - t);
		const TimeRange window(t, t + length);

		const bool window_valid = validated_ranges.contains(window);

		double peak = 0.0;
		if (window_valid) {
			peak = waveform_window_peak(clip.waveform, t, length, sample_rate);
		}

		envelope.append(peak);
		if (valid_mask) {
			valid_mask->append(window_valid);
		}
	}
	return envelope;
}

} // namespace TimelineWaveformSync

} // namespace olive
