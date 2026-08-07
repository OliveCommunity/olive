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

#ifndef OAK_CODECBRIDGE_H
#define OAK_CODECBRIDGE_H

namespace olive
{

/**
 * @brief Register oaktask as oakcodec's background task submitter
 *
 * Wires oakcodec_set_task_submit_cb() to execute OAKCODEC_TASK_CONFORM /
 * OAKCODEC_TASK_PROXY requests as ConformTask/ProxyTask. Submission is
 * synchronous (the callback runs the task to completion), matching
 * oakcodec's interim contract.
 */
void register_codec_task_submitter();

/**
 * @brief Unregister the submitter (oaktask_shutdown).
 */
void unregister_codec_task_submitter();

}

#endif // OAK_CODECBRIDGE_H
