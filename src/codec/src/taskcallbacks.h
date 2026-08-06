/*

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

*/

#ifndef OAK_CODEC_TASKCALLBACKS_H
#define OAK_CODEC_TASKCALLBACKS_H

#include "codec/task.h"

namespace olive
{

/**
 * @brief C++-side convenience wrapper around the registered submit callback
 *
 * Returns the callback's return value, or OAKCODEC_E_STATE when no
 * callback is registered (interim pre-M8 state).
 */
int SubmitTask(const OakCodecTaskRequest &req);

} // namespace olive

#endif // OAK_CODEC_TASKCALLBACKS_H
