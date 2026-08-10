/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_EDITOR_TIMELINE_DISPLAYMODE_H
#define OAK_EDITOR_TIMELINE_DISPLAYMODE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared timeline display-mode constants.
 *
 * Neutral home for the enum values behind the TimelineThumbnailMode /
 * TimelineWaveformMode config keys; mirrors olive::Timeline::ThumbnailMode
 * / WaveformMode (src/timeline/src/timelinecommon.h) and must stay
 * value-compatible with them.
 */
enum OakTimelineThumbnailMode {
	OAK_TIMELINE_THUMBNAIL_OFF = 0,
	OAK_TIMELINE_THUMBNAIL_IN_OUT = 1,
	OAK_TIMELINE_THUMBNAIL_ON = 2
};

enum OakTimelineWaveformMode {
	OAK_TIMELINE_WAVEFORMS_DISABLED = 0,
	OAK_TIMELINE_WAVEFORMS_ENABLED = 1
};

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_TIMELINE_DISPLAYMODE_H
