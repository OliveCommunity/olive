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

#ifndef OAK_EDITOR_SUBTITLEPARAMS_H
#define OAK_EDITOR_SUBTITLEPARAMS_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a subtitle parameter set (olive::SubtitleParams).
 */
typedef struct OakCommonSubtitleParams OakCommonSubtitleParams;

/**
 * @brief Create an empty subtitle parameter set.
 *
 * @return Params handle, or NULL on allocation failure.
 */
OakCommonSubtitleParams *oakcommon_subtitleparams_init(void);

/**
 * @brief Destroy a subtitle parameter set. No-op on NULL.
 */
void oakcommon_subtitleparams_free(OakCommonSubtitleParams *params);

int oakcommon_subtitleparams_get_stream_index(
	OakCommonSubtitleParams *params, int *index);
int oakcommon_subtitleparams_set_stream_index(
	OakCommonSubtitleParams *params, int index);
int oakcommon_subtitleparams_get_enabled(OakCommonSubtitleParams *params,
										 int *enabled);
int oakcommon_subtitleparams_set_enabled(OakCommonSubtitleParams *params,
										 int enabled);

/**
 * @brief Query whether the set contains at least one subtitle.
 */
int oakcommon_subtitleparams_is_valid(OakCommonSubtitleParams *params,
									  int *is_valid);

/**
 * @brief Number of subtitle entries.
 */
int oakcommon_subtitleparams_count(OakCommonSubtitleParams *params,
								   int *count);

/**
 * @brief Out time of the last subtitle (0/1 when empty).
 */
int oakcommon_subtitleparams_duration(OakCommonSubtitleParams *params,
									  int *numerator, int *denominator);

/**
 * @brief Append a subtitle entry.
 *
 * @param text Subtitle text. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_add_subtitle(OakCommonSubtitleParams *params,
										  int in_num, int in_den, int out_num,
										  int out_den, const char *text);

/**
 * @brief Remove all subtitle entries.
 */
int oakcommon_subtitleparams_clear(OakCommonSubtitleParams *params);

/**
 * @brief Get the time range of the subtitle at @p index.
 *
 * @return OAKCOMMON_OK, OAKCOMMON_E_NOT_FOUND if @p index is out of range,
 *         or another negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_get_subtitle(OakCommonSubtitleParams *params,
										  int index, int *in_num, int *in_den,
										  int *out_num, int *out_den);

/**
 * @brief Get the text of the subtitle at @p index (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), OAKCOMMON_E_NOT_FOUND if @p index is out of
 *         range, or another negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_get_subtitle_text(OakCommonSubtitleParams *params,
											   int index, char *buf,
											   int buf_size);

/**
 * @brief Generate a default ASS header (static, no handle required).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_generate_ass_header(char *buf, int buf_size);

/**
 * @brief Load subtitles from an XML fragment.
 *
 * @param xml NUL-terminated XML text. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_load_xml(OakCommonSubtitleParams *params,
									  const char *xml);

/**
 * @brief Save subtitles to an XML fragment (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_save_xml(OakCommonSubtitleParams *params,
									  char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_SUBTITLEPARAMS_H
