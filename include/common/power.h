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

#ifndef OAK_EDITOR_POWER_H
#define OAK_EDITOR_POWER_H

#include <stdint.h>

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Round `value` up to the next power of two
 *
 * Stateless pure function, no handle required. Writes the result to `out`.
 *
 * @param value Input value.
 * @param out Receives the rounded value. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if `out` is NULL.
 */
int oakcommon_power_ceil_to_power_of_2(uint32_t value, uint32_t *out);

/**
 * @brief Round `value` down to the nearest power of two
 *
 * Stateless pure function, no handle required. Writes the result to `out`.
 *
 * @param value Input value.
 * @param out Receives the rounded value. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if `out` is NULL.
 */
int oakcommon_power_floor_to_power_of_2(uint32_t value, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_POWER_H
