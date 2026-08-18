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
#include "common/handle.h"

#ifdef __cplusplus
namespace olive
{
class SubtitleParams;
}
extern "C" {
#endif

/**
 * @brief Neutral by-value handle to a subtitle parameter set
 *        (olive::SubtitleParams).
 *
 * Ownership/count semantics follow the convention in common/handle.h:
 * init functions return a handle whose object has reference count 1,
 * addref(ctx)/release(ctx) adjust it atomically, and release destroys
 * the object at zero. abi_version is always OAKCOMMON_ABI_VERSION.
 */
typedef struct OakSubtitleParams {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCOMMON_ABI_VERSION. */
} OakSubtitleParams;

/**
 * @brief Create an empty subtitle parameter set.
 *
 * @return Handle with reference count 1; ctx is NULL on allocation
 *         failure.
 */
OakSubtitleParams oakcommon_subtitleparams_init(void);

#ifdef __cplusplus
/**
 * @brief Copy a native olive::SubtitleParams into a new handle.
 *
 * The source object is deep-copied; the handle does not keep any
 * reference to @p src, which may be destroyed immediately afterwards.
 * Only visible to C++ consumers.
 *
 * @return Handle with reference count 1; ctx is NULL if src is NULL or
 *         on allocation failure.
 */
OakSubtitleParams oakcommon_subtitleparams_init_from_native(
	const olive::SubtitleParams *src);
#endif

/**
 * @brief Release one reference to a subtitle parameter set.
 *
 * Convenience wrapper around handle.release(handle.ctx): decrements the
 * atomic reference count and destroys the object when it reaches zero.
 * No-op when params is NULL or params->ctx is NULL.
 */
void oakcommon_subtitleparams_free(OakSubtitleParams *params);

int oakcommon_subtitleparams_get_stream_index(
	OakSubtitleParams params, int *index);
int oakcommon_subtitleparams_set_stream_index(
	OakSubtitleParams params, int index);
int oakcommon_subtitleparams_get_enabled(OakSubtitleParams params,
										 int *enabled);
int oakcommon_subtitleparams_set_enabled(OakSubtitleParams params,
										 int enabled);

/**
 * @brief Query whether the set contains at least one subtitle.
 */
int oakcommon_subtitleparams_is_valid(OakSubtitleParams params,
									  int *is_valid);

/**
 * @brief Number of subtitle entries.
 */
int oakcommon_subtitleparams_count(OakSubtitleParams params,
								   int *count);

/**
 * @brief Out time of the last subtitle (0/1 when empty).
 */
int oakcommon_subtitleparams_duration(OakSubtitleParams params,
									  int *numerator, int *denominator);

/**
 * @brief Append a subtitle entry.
 *
 * @param text Subtitle text. Must not be NULL.
 * @return OAKCOMMON_OK or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_add_subtitle(OakSubtitleParams params,
										  int in_num, int in_den, int out_num,
										  int out_den, const char *text);

/**
 * @brief Remove all subtitle entries.
 */
int oakcommon_subtitleparams_clear(OakSubtitleParams params);

/**
 * @brief Get the time range of the subtitle at @p index.
 *
 * @return OAKCOMMON_OK, OAKCOMMON_E_NOT_FOUND if @p index is out of range,
 *         or another negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_get_subtitle(OakSubtitleParams params,
										  int index, int *in_num, int *in_den,
										  int *out_num, int *out_den);

/**
 * @brief Get the text of the subtitle at @p index (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), OAKCOMMON_E_NOT_FOUND if @p index is out of
 *         range, or another negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_get_subtitle_text(OakSubtitleParams params,
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
int oakcommon_subtitleparams_load_xml(OakSubtitleParams params,
									  const char *xml);

/**
 * @brief Save subtitles to an XML fragment (two-stage string getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), or a negative OAKCOMMON_E_* error code.
 */
int oakcommon_subtitleparams_save_xml(OakSubtitleParams params,
									  char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_SUBTITLEPARAMS_H
