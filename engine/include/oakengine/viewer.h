/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKENGINE_VIEWER_H
#define OAKENGINE_VIEWER_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "node.h"
#include "timeline.h"
#include "videoparams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file viewer.h
 * @brief C ABI for viewer nodes (olive::ViewerOutput and subclasses:
 * Sequence, Footage)
 *
 * A viewer node is the bridge between a node graph and a monitor: it owns
 * a playhead, a length, per-stream video/audio/subtitle parameters, a
 * workarea and a marker list. This family covers the application-side
 * uses of olive::ViewerOutput that are not already exposed through the
 * sequence (timeline.h) or node (node.h) families.
 *
 * Handles: a viewer handle is simply an OakEngineNode* whose engine object
 * is a ViewerOutput (validate with oakengine_viewer_from_node()). Borrowed,
 * same lifetime rules as node.h. Change notifications (length/playhead/
 * params/workarea-adjacent) are delivered through the event mechanism --
 * subscribe with the OAKENGINE_EVENT_VIEWER_* ids from oakengine/events.h
 * on the node handle.
 *
 * Conventions match the rest of the facade: rationals are int64
 * numerator/denominator pairs (seconds), booleans are int, 0
 * (OAKENGINE_OK)/negative OAKENGINE_E_* return codes, NULL handles are
 * no-ops returning OAKENGINE_E_INVALID.
 */

/**
 * @brief POD snapshot of a viewer's workarea (olive::TimelineWorkArea:
 * range in/out + enabled flag). Rationals in seconds.
 */
typedef struct oakengine_viewer_workarea {
	int64_t in_num;
	int64_t in_den;
	int64_t out_num;
	int64_t out_den;
	int enabled;
} oakengine_viewer_workarea;

/**
 * @brief Return `node` if its engine object is a viewer (olive::ViewerOutput
 * or subclass, e.g. Sequence/Footage), NULL otherwise. Replaces
 * dynamic_cast<ViewerOutput*> at the app boundary; also the canonical way
 * to validate a handle for this family.
 */
OAKENGINE_API OakEngineNode *oakengine_viewer_from_node(OakEngineNode *node);

/** @brief const overload of oakengine_viewer_from_node(). */
OAKENGINE_API const OakEngineNode *
oakengine_viewer_from_const_node(const OakEngineNode *node);

/* ---- Input ids / constants (ViewerOutput::k_* statics) ------------------ */

/** @brief ViewerOutput::k_video_params_input. Static string, never freed. */
OAKENGINE_API const char *oakengine_viewer_video_params_input_id(void);
/** @brief ViewerOutput::k_audio_params_input. */
OAKENGINE_API const char *oakengine_viewer_audio_params_input_id(void);
/** @brief ViewerOutput::k_subtitle_params_input. */
OAKENGINE_API const char *oakengine_viewer_subtitle_params_input_id(void);
/** @brief ViewerOutput::k_texture_input. */
OAKENGINE_API const char *oakengine_viewer_texture_input_id(void);
/** @brief ViewerOutput::k_samples_input. */
OAKENGINE_API const char *oakengine_viewer_samples_input_id(void);
/** @brief ViewerOutput::k_default_sample_format (olive::core::SampleFormat). */
OAKENGINE_API int oakengine_viewer_default_sample_format(void);

/* ---- Playhead / length --------------------------------------------------- */

/** @brief Current playhead in seconds (ViewerOutput::get_playhead()). */
OAKENGINE_API int oakengine_viewer_get_playhead(const OakEngineNode *self,
												int64_t *num, int64_t *den);

/** @brief Move the playhead (ViewerOutput::set_playhead()). Emits
 * OAKENGINE_EVENT_VIEWER_PLAYHEAD_CHANGED. */
OAKENGINE_API int oakengine_viewer_set_playhead(OakEngineNode *self,
												int64_t num, int64_t den);

/**
 * @brief Set the video parameters of stream `index` on `self`
 * (ViewerOutput::set_video_params()). `self` must be a viewer node.
 */
OAKENGINE_API int oakengine_viewer_set_video_params(OakEngineNode *self,
												const oak_video_params *params,
												int index);

/**
 * @brief Set the audio parameters of stream `index` on `self`
 * (ViewerOutput::set_audio_params()). `self` must be a viewer node.
 */
OAKENGINE_API int oakengine_viewer_set_audio_params(OakEngineNode *self,
												int sample_rate,
												uint64_t channel_layout,
												int format, int index);

/** @brief Content length in seconds (ViewerOutput::get_length()). */
OAKENGINE_API int oakengine_viewer_get_length(const OakEngineNode *self,
											  int64_t *num, int64_t *den);

/** @brief Video content length in seconds (ViewerOutput::get_video_length()). */
OAKENGINE_API int oakengine_viewer_get_video_length(const OakEngineNode *self,
													int64_t *num,
													int64_t *den);

/** @brief Audio content length in seconds (ViewerOutput::get_audio_length()). */
OAKENGINE_API int oakengine_viewer_get_audio_length(const OakEngineNode *self,
													int64_t *num,
													int64_t *den);

/* ---- Stream parameters ---------------------------------------------------- */

/**
 * @brief Video params of stream `index` (ViewerOutput::get_video_params()).
 * `out` is always written; an out-of-range index yields a zeroed struct
 * (width/height 0 = invalid, matches an invalid olive::VideoParams).
 */
OAKENGINE_API int oakengine_viewer_get_video_params(
	const OakEngineNode *self, int index, oak_video_params *out);

/**
 * @brief Audio params of stream `index` (ViewerOutput::get_audio_params()).
 * Any of the out pointers may be NULL. `format` is an
 * olive::core::SampleFormat value; out-of-range index yields 0/0/0.
 */
OAKENGINE_API int oakengine_viewer_get_audio_params(
	const OakEngineNode *self, int index, int *sample_rate,
	uint64_t *channel_layout, int *format);

/** @brief Number of video streams (ViewerOutput::get_video_stream_count()). */
OAKENGINE_API int oakengine_viewer_get_video_stream_count(
	const OakEngineNode *self);
/** @brief Number of audio streams (ViewerOutput::get_audio_stream_count()). */
OAKENGINE_API int oakengine_viewer_get_audio_stream_count(
	const OakEngineNode *self);
/** @brief Number of subtitle streams (ViewerOutput::get_subtitle_stream_count()). */
OAKENGINE_API int oakengine_viewer_get_subtitle_stream_count(
	const OakEngineNode *self);

/**
 * @brief 1 if stream `index` of `track_type` (OAKENGINE_TRACK_TYPE_*) is
 * enabled (VideoParams/AudioParams/SubtitleParams::enabled()), else 0;
 * OAKENGINE_E_INVALID (< 0) on bad arguments.
 */
OAKENGINE_API int oakengine_viewer_get_stream_enabled(
	const OakEngineNode *self, int track_type, int index);

/**
 * @brief Number of subtitles in subtitle stream `index`
 * (SubtitleParams::size()); < 0 on bad arguments.
 */
OAKENGINE_API int oakengine_viewer_get_subtitle_count(
	const OakEngineNode *self, int index);

/**
 * @brief Borrowed pointer to subtitle `sub_index` of subtitle stream
 * `index` (a const olive::Subtitle*; the application copies the value out,
 * it must not free or store it beyond the footage's lifetime). NULL on
 * bad arguments.
 */
OAKENGINE_API const void *oakengine_viewer_get_subtitle_at(
	const OakEngineNode *self, int index, int sub_index);

/**
 * @brief 1 if the viewer has at least one enabled stream of `track_type`
 * (OAKENGINE_TRACK_TYPE_* from timeline.h), else 0
 * (ViewerOutput::has_enabled_video/audio/subtitle_streams()).
 */
OAKENGINE_API int oakengine_viewer_has_enabled_streams(
	const OakEngineNode *self, int track_type);

/**
 * @brief Params of the first enabled video stream
 * (ViewerOutput::get_first_enabled_video_stream()); zeroed struct when
 * none is enabled.
 */
OAKENGINE_API int oakengine_viewer_get_first_enabled_video_stream(
	const OakEngineNode *self, oak_video_params *out);

/**
 * @brief Number of enabled streams of all types
 * (ViewerOutput::get_enabled_streams_as_references().size()).
 */
OAKENGINE_API int oakengine_viewer_get_enabled_stream_count(
	const OakEngineNode *self);

/**
 * @brief Write the enabled stream references
 * (ViewerOutput::get_enabled_streams_as_references()) into caller arrays:
 * `types[k]` = OAKENGINE_TRACK_TYPE_*, `indices[k]` = stream index within
 * that type. At most `max` entries are written; returns the total count
 * (call with max=0/NULL arrays to query, or use
 * oakengine_viewer_get_enabled_stream_count()).
 */
OAKENGINE_API int oakengine_viewer_get_enabled_streams(
	const OakEngineNode *self, int *types, int *indices, int max);

/* ---- Workarea -------------------------------------------------------------- */

/** @brief Snapshot of the viewer's workarea (ViewerOutput::get_work_area()
 * range/enabled as POD). */
OAKENGINE_API int oakengine_viewer_get_workarea(
	const OakEngineNode *self, oakengine_viewer_workarea *out);

/** @brief Set the workarea range (TimelineWorkArea::set_range()). Emits the
 * workarea range notification on the underlying workarea object. */
OAKENGINE_API int oakengine_viewer_set_workarea_range(OakEngineNode *self,
													  int64_t in_num,
													  int64_t in_den,
													  int64_t out_num,
													  int64_t out_den);

/** @brief Enable/disable the workarea (TimelineWorkArea::set_enabled()). */
OAKENGINE_API int oakengine_viewer_set_workarea_enabled(OakEngineNode *self,
														int enabled);

/* ---- Parameter setup / waveform --------------------------------------------- */

/** @brief Apply the application default parameters
 * (ViewerOutput::set_default_parameters(): width/height/pixel aspect/
 * interlacing/audio layout from Config, frame rate from
 * DefaultSequenceFrameRate). */
OAKENGINE_API int oakengine_viewer_set_default_parameters(OakEngineNode *self);

/**
 * @brief Create a command that sets the viewer's preview resolution divider
 * (changes the k_video_params_input standard value). Returns an opaque command
 * pointer, or NULL when `self` is not a viewer or `divider` is invalid.
 */
OAKENGINE_API void *oakengine_viewer_set_preview_divider_command(
	OakEngineNode *self, int divider);

/**
 * @brief Adopt the parameters of the given footage viewers
 * (ViewerOutput::set_parameters_from_footage()). Every element of
 * `footage` must itself be a viewer handle.
 */
OAKENGINE_API int oakengine_viewer_set_parameters_from_footage(
	OakEngineNode *self, OakEngineNode *const *footage, int count);

/** @brief Enable/disable waveform cache requests
 * (ViewerOutput::set_waveform_enabled()). */
OAKENGINE_API int oakengine_viewer_set_waveform_enabled(OakEngineNode *self,
													   int enabled);

/**
 * @brief The waveform cache of the connected sample output, or NULL
 * (ViewerOutput::get_connected_waveform()). Opaque borrowed pointer; the
 * application only passes it through to its own audio monitor, it must not
 * dereference it.
 */
OAKENGINE_API const void *
oakengine_viewer_get_connected_waveform(const OakEngineNode *self);

/**
 * @brief Borrowed handle of the viewer's timeline marker list
 * (ViewerOutput::get_markers()), for the oakengine_marker_list_* family
 * and the OAKENGINE_EVENT_MARKER_LIST_* events. NULL when `self` is not a
 * viewer.
 */
OAKENGINE_API OakEngineMarkerList *
oakengine_viewer_get_marker_list(OakEngineNode *self);

/**
 * @brief Borrowed handle of the viewer's workarea
 * (ViewerOutput::get_work_area()), for the oakengine_workarea_* family and
 * the OAKENGINE_EVENT_WORKAREA_* events. NULL when `self` is not a viewer.
 */
OAKENGINE_API OakEngineWorkarea *
oakengine_viewer_get_workarea_handle(OakEngineNode *self);

/* ---- Playback cache / frame cache ------------------------------------------ */

/**
 * @brief Opaque playback cache handle (olive::PlaybackCache).
 */
typedef struct OakEnginePlaybackCache OakEnginePlaybackCache;

/**
 * @brief Opaque frame cache handle (olive::FrameHashCache).
 */
typedef struct OakEngineFrameCache OakEngineFrameCache;

/**
 * @brief Borrowed playback cache of a viewer's connected output
 * (ViewerOutput::get_connected_video_cache() for video, or from the
 * ClipBlock::connected_video_cache()). Returns NULL when not available
 * or when `self` is not a viewer/clip node.
 */
OAKENGINE_API OakEnginePlaybackCache *
oakengine_viewer_get_playback_cache(OakEngineNode *self);

/**
 * @brief Static indicator height for playback cache rendering
 * (PlaybackCache::get_cache_indicator_height()). > 0.
 */
OAKENGINE_API int oakengine_playback_cache_indicator_height(void);

/**
 * @brief Fill `ranges` with the valid (cached) time ranges from the
 * playback cache. `ranges` is an array of (in_num,in_den,out_num,out_den)
 * int64_t quads; at most `max` ranges are written. Returns the number of
 * ranges written, or OAKENGINE_E_INVALID on NULL cache.
 */
OAKENGINE_API int oakengine_playback_cache_valid_ranges(
	OakEnginePlaybackCache *cache, int64_t *ranges, int max);

/**
 * @brief Borrowed frame hash cache (FrameHashCache) of a viewer node
 * (ViewerOutput has a get_video_cache(), etc.). Returns NULL when not
 * available or when `self` is not a viewer node.
 */
OAKENGINE_API OakEngineFrameCache *
oakengine_viewer_get_frame_cache(OakEngineNode *self);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_VIEWER_H */
