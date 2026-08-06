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

#ifndef OAK_EDITOR_LOOPMODE_H
#define OAK_EDITOR_LOOPMODE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Playback loop mode, mirroring olive::LoopMode.
 *
 * The numeric values must stay in sync with src/common/src/loopmode.h.
 * Pure enum: no functions are needed.
 */
enum OakLoopMode {
	OAKCOMMON_LOOP_MODE_OFF = 0, /**< Looping disabled. */
	OAKCOMMON_LOOP_MODE_LOOP = 1, /**< Loop playback. */
	OAKCOMMON_LOOP_MODE_CLAMP = 2 /**< Clamp at the end. */
};

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_LOOPMODE_H
