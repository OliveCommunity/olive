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

#ifndef OAK_EDITOR_NODE_FOOTAGE_H
#define OAK_EDITOR_NODE_FOOTAGE_H

#include <stdint.h>

#include "common/videoparams.h"
#include "node/error.h"
// NOTE: quoted-relative to bypass the "render/cancelatom.h" transition
// bridge (oakrender's C++ olive::CancelAtom) that shadows the C ABI
// header on oaknode's include path.
#include "../../include/render/cancelatom.h"
#include "node/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file footage.h
 * @brief C ABI for olive::Footage (oaknode)
 *
 * A footage node references an external media file and caches its stream
 * metadata. Footage handles are borrowed from the owning project; they
 * become invalid when the project is freed or cleared.
 *
 * NOTE: setting a filename whose file exists on disk triggers a probe,
 * which requires the codec/render modules (outside oaknode). Tests and
 * pure-graph consumers should use nonexistent paths; probing is the
 * facade layer's job.
 */

/**
 * @brief Opaque footage handle. Borrowed from the owning project.
 */
typedef struct OakNodeFootage OakNodeFootage;

/**
 * @brief Create a footage node owned by `project` (added to the project's
 * graph, not attached to any folder).
 *
 * @param filename Initial media path, may be NULL/empty.
 *
 * @return Footage handle, or NULL on failure.
 */
OakNodeFootage *oaknode_footage_create(OakNodeProject *project,
									   const char *filename);

/**
 * @brief Borrowed cast from a footage handle to its node handle.
 * NULL for NULL.
 */
OakNodeNode *oaknode_footage_as_node(OakNodeFootage *footage);

/**
 * @brief Current media path (Footage::filename()). Two-stage string getter.
 *
 * @return Required buffer size in bytes including the NUL, or a negative
 *         OAKNODE_E_* error code.
 */
int oaknode_footage_filename(const OakNodeFootage *footage, char *buf,
							 int buf_size);

/**
 * @brief Set the media path (Footage::set_filename()). Does not re-probe
 * unless the file exists (see the file comment above).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_set_filename(OakNodeFootage *footage, const char *filename);

/**
 * @brief 1 if the footage was successfully probed and is ready for use
 * (Footage::is_valid()), 0 otherwise. Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_is_valid(const OakNodeFootage *footage);

/**
 * @brief Last-modified timestamp of the media file in milliseconds since the
 * epoch (Footage::timestamp()).
 *
 * @param out_timestamp Receives the timestamp. Must not be NULL.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_timestamp(const OakNodeFootage *footage,
							  int64_t *out_timestamp);

/**
 * @brief Set the last-modified timestamp (Footage::set_timestamp()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_set_timestamp(OakNodeFootage *footage, int64_t timestamp);

/**
 * @brief Decoder ID recorded when the footage was probed
 * (Footage::decoder()). Two-stage string getter.
 */
int oaknode_footage_decoder(const OakNodeFootage *footage, char *buf,
							int buf_size);

/**
 * @brief Total number of streams (Footage::get_total_stream_count()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_total_stream_count(const OakNodeFootage *footage);

/**
 * @brief Number of video streams (ViewerOutput::get_video_stream_count()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_video_stream_count(const OakNodeFootage *footage);

/**
 * @brief Number of audio streams (ViewerOutput::get_audio_stream_count()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_audio_stream_count(const OakNodeFootage *footage);

/**
 * @brief Number of subtitle streams (ViewerOutput::get_subtitle_stream_count()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_subtitle_stream_count(const OakNodeFootage *footage);

/**
 * @brief Footage duration as a rational number of seconds
 * (ViewerOutput::get_length()).
 *
 * @param out_numerator Receives the numerator. Must not be NULL.
 * @param out_denominator Receives the denominator. Must not be NULL.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_duration(const OakNodeFootage *footage,
							 int *out_numerator, int *out_denominator);

/**
 * @brief 1 if proxy playback is enabled (Footage::proxy_enabled()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_proxy_enabled(const OakNodeFootage *footage);

/**
 * @brief Enable/disable proxy playback (Footage::set_proxy_enabled()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_set_proxy_enabled(OakNodeFootage *footage, int enabled);

/**
 * @brief Proxy file path, or "" when none (Footage::proxy_path()).
 * Two-stage string getter.
 */
int oaknode_footage_proxy_path(const OakNodeFootage *footage, char *buf,
							   int buf_size);

/**
 * @brief Proxy state enum value (Footage::proxy_state():
 * ProxyManager::ProxyState). Negative OAKNODE_E_* code on NULL.
 */
int oaknode_footage_proxy_state(const OakNodeFootage *footage);

/**
 * @brief Set all proxy fields at once (Footage::set_proxy()).
 *
 * @param path Proxy file path, may be NULL/empty.
 * @param state ProxyManager::ProxyState enum value.
 * @param video_stream_index Proxy's video stream index (-1 when none).
 * @param preset_version Proxy preset version.
 * @param enabled Non-zero to enable proxy playback.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_set_proxy(OakNodeFootage *footage, const char *path,
							  int state, int video_stream_index,
							  int preset_version, int enabled);

/**
 * @brief Clear all proxy fields (Footage::clear_proxy()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_footage_clear_proxy(OakNodeFootage *footage);

/**
 * @brief Video stream parameters as an oakcommon video-params handle
 * (ViewerOutput::get_video_params()). `out` receives a handle with
 * reference count 1 (release with oakcommon_videoparams_free()).
 * OAKNODE_E_NOT_FOUND for an out-of-range index.
 */
int oaknode_footage_get_video_params(OakNodeFootage *footage, int index,
									 OakVideoParams *out);

/**
 * @brief Set a video stream's parameters from an oakcommon handle
 * (ViewerOutput::set_video_params()).
 */
int oaknode_footage_set_video_params(OakNodeFootage *footage, int index,
									 const OakVideoParams *params);

/**
 * @brief Video length as a rational pair (ViewerOutput::get_video_length()).
 */
int oaknode_footage_get_video_length(OakNodeFootage *footage,
									 int64_t *out_num, int64_t *out_den);

/**
 * @brief Set the footage's cancellation atom used during probing
 * (Footage::set_cancel_pointer()). `atom` may be an empty OakCancelAtom
 * (ctx == NULL) to clear.
 */
int oaknode_footage_set_cancel_atom(OakNodeFootage *footage,
									OakCancelAtom atom);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_FOOTAGE_H
