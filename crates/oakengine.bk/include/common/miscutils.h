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

#ifndef OAK_EDITOR_MISCUTILS_H
#define OAK_EDITOR_MISCUTILS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Minimum decibel value used by the editor (-200.0 dB).
 *
 * In basically all circumstances, this calculates to 0.0 linear.
 */
#define OAKCOMMON_DECIBEL_MINIMUM (-200.0)

/**
 * @brief Convert a linear amplitude to decibels.
 *
 * A linear value of 0.0 (or anything yielding an infinite result) returns
 * OAKCOMMON_DECIBEL_MINIMUM.
 *
 * @param linear Linear amplitude.
 * @param out_db Receives the decibel value. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_db is NULL.
 */
int oakcommon_decibel_from_linear(double linear, double *out_db);

/**
 * @brief Convert decibels to a linear amplitude.
 *
 * Results below 1e-6 are clamped to 0.0.
 *
 * @param db Decibel value.
 * @param out_linear Receives the linear amplitude. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_linear is NULL.
 */
int oakcommon_decibel_to_linear(double db, double *out_linear);

/**
 * @brief Convert a logarithmic slider position (0..1) to decibels.
 *
 * @param logarithmic Logarithmic position.
 * @param out_db Receives the decibel value. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_db is NULL.
 */
int oakcommon_decibel_from_logarithmic(double logarithmic, double *out_db);

/**
 * @brief Convert decibels to a logarithmic slider position (0..1).
 *
 * @param db Decibel value.
 * @param out_logarithmic Receives the logarithmic position. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_logarithmic
 * is NULL.
 */
int oakcommon_decibel_to_logarithmic(double db, double *out_logarithmic);

/**
 * @brief Convert a linear amplitude directly to a logarithmic position.
 *
 * @param linear Linear amplitude.
 * @param out_logarithmic Receives the logarithmic position. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_logarithmic
 * is NULL.
 */
int oakcommon_decibel_linear_to_logarithmic(double linear,
											double *out_logarithmic);

/**
 * @brief Convert a logarithmic position directly to a linear amplitude.
 *
 * @param logarithmic Logarithmic position.
 * @param out_linear Receives the linear amplitude. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_linear is NULL.
 */
int oakcommon_decibel_logarithmic_to_linear(double logarithmic,
											double *out_linear);

/**
 * @brief Linearly interpolate between a and b using t.
 *
 * t should be between 0.0 and 1.0: 0.0 returns a, 1.0 returns b.
 *
 * @param a Start value.
 * @param b End value.
 * @param t Interpolation factor.
 * @param out_value Receives the interpolated value. Must not be NULL.
 * @return OAKCOMMON_OK on success, OAKCOMMON_E_INVALID if out_value is NULL.
 */
int oakcommon_lerp(double a, double b, double t, double *out_value);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_MISCUTILS_H
