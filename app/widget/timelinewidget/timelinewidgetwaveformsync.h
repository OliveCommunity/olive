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

#ifndef OAK_TIMELINEWIDGETWAVEFORMSYNC_H
#define OAK_TIMELINEWIDGETWAVEFORMSYNC_H

#include <QVector>

#include "node/block/block.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"

namespace olive
{

class AudioWaveformCache;
class ClipBlock;

/**
 * @brief Data required to synchronize a clip using its cached audio waveform.
 */
struct WaveformSyncClip {
	ClipBlock *clip = nullptr;
	const AudioWaveformCache *waveform = nullptr;
	TimeRange media_range;
	int sample_rate = 0;
};

/**
 * @brief Helpers used by TimelineWidget for waveform-based clip synchronization.
 *
 * Kept in a separate unit so they can be exercised directly by unit tests.
 */
namespace timeline_waveform_sync
{

/**
 * @brief Fill @p out with waveform-sync metadata for @p block if it is usable.
 *
 * A clip is considered usable as long as at least a portion of its media range
 * has been validated in the waveform cache. Previously the whole range had to
 * be validated, which made the context-menu action unavailable for long clips.
 */
bool get_waveform_sync_clip(Block *block, WaveformSyncClip *out);

/**
 * @brief Return all selected blocks that can be synchronized by waveform.
 */
QVector<WaveformSyncClip>
get_selected_waveform_sync_clips(const QVector<Block *> &blocks);

/**
 * @brief Extract a peak envelope from the validated regions of a waveform cache.
 *
 * Windows that have not been cached yet are filled with zero so that every
 * envelope stays aligned to the same absolute timeline; when @p valid_mask is
 * provided it receives one flag per window marking whether the window was
 * actually cached, allowing the correlation to skip uncached regions instead
 * of treating them as silence.
 */
QVector<double> extract_waveform_cache_envelope(const WaveformSyncClip &clip,
											 int sample_rate,
											 size_t window_samples,
											 QVector<bool> *valid_mask = nullptr);

} // namespace TimelineWaveformSync

} // namespace olive

#endif // OAK_TIMELINEWIDGETWAVEFORMSYNC_H
