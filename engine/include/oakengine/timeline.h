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

#ifndef OAKENGINE_TIMELINE_H
#define OAKENGINE_TIMELINE_H

#include <stdint.h>

#include "export.h"
#include "footage.h"
#include "init.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file timeline.h
 * @brief C ABI for sequences (Oak timelines)
 *
 * An OakEngineSequence wraps the engine's olive::Sequence node
 * (engine/node/project/sequence/sequence.h, a ViewerOutput). Most of the
 * family is inspection; playhead, workarea, the sequence parameters
 * (video/audio/preview/auto-cache, see below) and clip/track editing are
 * writes, undoable unless documented otherwise.
 *
 * Handles are borrowed from their owning OakEngineProject (Qt QObject parent
 * chain; sequences are added to the project graph which becomes their
 * parent). There is deliberately no oakengine_sequence_free(): a sequence
 * handle becomes invalid when its project is freed. Sequences created with
 * oakengine_sequence_new() are additionally undoable -- undoing the creation
 * removes the sequence from the project (but keeps it alive under the undo
 * command), redoing brings it back.
 *
 * Time is exposed in the two representations used across the engine
 * (olive::core::Timecode terminology, core/util/timecodefunctions.h):
 *
 *   - seconds: a plain double (`*_seconds` accessors), or a rational seconds
 *     value as a numerator/denominator int pair (`*_rational`);
 *
 *   - timestamp: an int64 count of timebase units, where the timebase is the
 *     sequence's frame duration (the frame rate flipped, e.g. 1001/30000 for
 *     a 30000/1001 sequence) -- i.e. a frame number. This matches how the
 *     engine converts between Rational times and frame timestamps
 *     (Timecode::time_to_timestamp / timestamp_to_time).
 *
 * Conventions match oakengine/project.h: booleans are int, 0
 * (OAKENGINE_OK)/negative OAKENGINE_E_* return codes, buf/size string output,
 * NULL handles yield no-ops or OAKENGINE_E_INVALID.
 */

/**
 * @brief Create a new sequence named `name` in `project` and return its
 * borrowed handle (NULL on failure; `project` NULL -> NULL).
 *
 * The sequence gets the application's default parameters
 * (ViewerOutput::set_default_parameters(): width/height/pixel aspect/
 * interlacing/audio layout from Config, frame rate from the
 * DefaultSequenceFrameRate config entry, 30000/1001 by default) and starts
 * with zero tracks. The creation is pushed onto the global undo stack like
 * the application's "Create New Sequence" action (minus opening a viewer).
 */
OAKENGINE_API OakEngineSequence *
oakengine_sequence_new(OakEngineProject *project, const char *name);

/**
 * @brief Sequence name (Node::get_label()). Uses the buf/size convention.
 */
OAKENGINE_API int oakengine_sequence_name(const OakEngineSequence *self,
										  char *buf, int buf_size);

/**
 * @brief Length of the sequence content in seconds
 * (ViewerOutput::get_length()). 0 for an empty sequence.
 */
OAKENGINE_API int oakengine_sequence_get_length(const OakEngineSequence *self,
												double *seconds);

/**
 * @brief Length of the sequence content as rational seconds
 * (ViewerOutput::get_length().numerator()/denominator()).
 */
OAKENGINE_API int
oakengine_sequence_get_length_rational(const OakEngineSequence *self, int *num,
									   int *den);

/**
 * @brief Sequence frame rate as a num/den pair, e.g. 30000/1001
 * (ViewerOutput::get_video_params().frame_rate()).
 */
OAKENGINE_API int
oakengine_sequence_get_frame_rate(const OakEngineSequence *self, int *num,
								  int *den);

/**
 * @brief Sequence video dimensions and pixel aspect ratio
 * (ViewerOutput::get_video_params()). Any output pointer may be NULL.
 */
OAKENGINE_API int
oakengine_sequence_get_video_params(const OakEngineSequence *self, int *width,
									int *height, int *par_num, int *par_den);

/* ---- Sequence parameters (sequence dialog) ------------------------------------
 *
 * The parameter set shown by the application's sequence dialog
 * (app/dialog/sequence): video size/frame rate/pixel aspect/interlacing,
 * preview pixel format and resolution divider, audio sample rate/channel
 * layout, and the video auto-cache flag. Setters take an `undoable` flag:
 * 1 pushes one undoable command onto the global undo stack (direct
 * application when the engine is not initialized), 0 applies directly with
 * no undo entry -- mirroring the dialog's two modes (SetUndoable()).
 * Fields pass -1 (ints/rationals) or <= 0 / 0 (audio) to leave them
 * unchanged; a call that changes nothing pushes no command and returns
 * OAKENGINE_OK. The video channel count and the audio sample format are
 * internal constants in the engine (VideoParams::k_internal_channel_count,
 * ViewerOutput::k_default_sample_format) and are kept as-is.
 */

/**
 * @brief Full read of the sequence's video parameters
 * (ViewerOutput::get_video_params()). Any output pointer may be NULL.
 * `fps_*` is the frame rate (the time base flipped), `format` a
 * PixelFormat::Format value, `divider` the preview resolution divider.
 */
OAKENGINE_API int oakengine_sequence_get_video_params_ex(
	const OakEngineSequence *self, int *width, int *height, int *fps_num,
	int *fps_den, int *par_num, int *par_den, int *interlacing, int *format,
	int *divider);

/**
 * @brief Write the sequence's video parameters (see the section comment
 * for the undoable/unchanged conventions).
 *
 * -1 leaves a field unchanged; both fps and pixel-aspect must be given as
 * num/den pairs (both -1 or both > 0). `interlacing` is a
 * VideoParams::Interlacing value (0..2), `format` a PixelFormat::Format
 * value (0..PixelFormat::count-1). Invalid values yield
 * OAKENGINE_E_INVALID. The frame rate is stored as its flipped time base
 * exactly like the application's dialog (VideoParams constructor), so the
 * derived effective size and frame_rate stay in sync.
 */
OAKENGINE_API int oakengine_sequence_set_video_params(
	OakEngineSequence *self, int width, int height, int fps_num, int fps_den,
	int par_num, int par_den, int interlacing, int format, int undoable);

/**
 * @brief Sequence audio sample rate and channel layout
 * (ViewerOutput::get_audio_params()). Any output pointer may be NULL.
 */
OAKENGINE_API int oakengine_sequence_get_audio_params(
	const OakEngineSequence *self, int *sample_rate,
	uint64_t *channel_layout);

/**
 * @brief Write the sequence's audio parameters (undoable flag as above).
 * `sample_rate` <= 0 or `channel_layout` == 0 leaves the field unchanged.
 * The sample format is kept unchanged (the dialog always uses
 * ViewerOutput::k_default_sample_format).
 */
OAKENGINE_API int oakengine_sequence_set_audio_params(
	OakEngineSequence *self, int sample_rate, uint64_t channel_layout,
	int undoable);

/**
 * @brief Preview resolution divider (VideoParams::divider()). 0 on a NULL
 * handle.
 */
OAKENGINE_API int
oakengine_sequence_get_preview_divider(const OakEngineSequence *self);

/**
 * @brief Set the preview resolution divider (undoable flag as above).
 * `divider` < 1 yields OAKENGINE_E_INVALID.
 */
OAKENGINE_API int oakengine_sequence_set_preview_divider(
	OakEngineSequence *self, int divider, int undoable);

/**
 * @brief 1 when video auto-cache is enabled
 * (ViewerOutput::is_video_auto_cache_enabled()). 0 on a NULL handle.
 */
OAKENGINE_API int
oakengine_sequence_get_video_auto_cache(const OakEngineSequence *self);

/**
 * @brief Enable or disable video auto-cache.
 *
 * The engine's auto-cache accessors are currently stubs (the read always
 * reports 0, the write is ignored; the application's dialog has the
 * checkbox TEMP-disabled for the same reason). This forwards to the stub
 * without an undo command so the ABI is ready when the engine
 * implementation lands. `undoable` is accepted for symmetry and ignored.
 */
OAKENGINE_API int oakengine_sequence_set_video_auto_cache(
	OakEngineSequence *self, int enabled, int undoable);

/**
 * @brief Number of tracks per track type (Sequence::track_list(type)->
 * get_track_count()). Any of `video`/`audio`/`subtitle` may be NULL.
 */
OAKENGINE_API int oakengine_sequence_track_count(const OakEngineSequence *self,
												 int *video, int *audio,
												 int *subtitle);

/**
 * @brief Playhead position as a timestamp in timebase units (frame number;
 * ViewerOutput::get_playhead() rescaled to the frame-rate timebase).
 */
OAKENGINE_API int
oakengine_sequence_get_playhead(const OakEngineSequence *self,
								int64_t *timestamp);

/**
 * @brief Move the playhead to `timestamp` (frame number;
 * ViewerOutput::set_playhead()).
 */
OAKENGINE_API int oakengine_sequence_set_playhead(OakEngineSequence *self,
												  int64_t timestamp);

/**
 * @brief Playhead position in seconds.
 */
OAKENGINE_API int
oakengine_sequence_get_playhead_seconds(const OakEngineSequence *self,
										double *seconds);

/**
 * @brief 1 if the workarea (in/out range) is enabled
 * (TimelineWorkArea::enabled()).
 */
OAKENGINE_API int
oakengine_sequence_workarea_is_enabled(const OakEngineSequence *self);

/**
 * @brief Workarea in/out points as timestamps in timebase units
 * (TimelineWorkArea::in()/out()). Either pointer may be NULL.
 */
OAKENGINE_API int
oakengine_sequence_get_workarea(const OakEngineSequence *self, int64_t *in,
								int64_t *out);

/**
 * @brief Set the workarea: enable flag plus in/out timestamps in timebase
 * units (TimelineWorkArea::set_enabled()/set_range()).
 */
OAKENGINE_API int oakengine_sequence_set_workarea(OakEngineSequence *self,
												  int enabled, int64_t in,
												  int64_t out);

/**
 * @brief Number of timeline markers (TimelineMarkerList::size()).
 */
OAKENGINE_API int
oakengine_sequence_marker_count(const OakEngineSequence *self);

/**
 * @brief Marker at `index`: `time` receives its in-point as a timestamp in
 * timebase units (may be NULL), `name` its label using the buf/size
 * truncation convention (may be NULL to only fetch the time), `color` its
 * color index (may be NULL). Returns
 * OAKENGINE_OK on success, OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_sequence_marker_at(const OakEngineSequence *self,
											   int index, int64_t *time,
											   char *name, int name_size,
											   int *color);

/* ---- Timeline editing primitives ---------------------------------------- */

/**
 * @brief Track types, matching olive::Track::Type.
 */
#define OAKENGINE_TRACK_TYPE_VIDEO 0
#define OAKENGINE_TRACK_TYPE_AUDIO 1
#define OAKENGINE_TRACK_TYPE_SUBTITLE 2

/**
 * @brief Opaque clip handle (a ClipBlock on a track).
 *
 * Handles are borrowed from their owning project (QObject parent chain) and
 * become invalid when the project is freed or the clip is removed (e.g. by
 * undoing the add). There is no oakengine_clip_free().
 */
typedef struct OakEngineClip OakEngineClip;

/**
 * @brief Human-readable reason for the last failed editing call on this
 * thread (buf/size convention). Editing calls return NULL or a negative
 * OAKENGINE_E_* code; the text explains why.
 */
OAKENGINE_API int oakengine_sequence_last_error(char *buf, int buf_size);

/**
 * @brief Append a track of `track_type` (OAKENGINE_TRACK_TYPE_*) to the
 * sequence and return its index in that type's track list.
 *
 * Uses the engine's TimelineAddTrackCommand without auto-merge: the first
 * video/audio track is connected straight to the sequence's texture/samples
 * input (tracks beyond the first stay unconnected until a merge node is
 * added -- multi-track compositing is a later milestone). The add is
 * undoable like the other editing primitives. Returns the new track index
 * (>= 0) or a negative OAKENGINE_E_* code.
 */
OAKENGINE_API int oakengine_sequence_add_track(OakEngineSequence *self,
											   int track_type);

/**
 * @brief Place a clip of `footage` on a track (undoable).
 *
 * Creates an olive::ClipBlock whose buffer input is fed by the footage node
 * and places it on the track at `track_index` (within the track list of
 * `track_type`, OAKENGINE_TRACK_TYPE_VIDEO or _AUDIO; subtitle clips are
 * rejected with OAKENGINE_E_INVALID). The footage handle must be a borrowed
 * import handle belonging to the same project as the sequence (probed
 * handles carry no node and are rejected).
 *
 * `in`/`out` are the clip's timeline range and `media_in` the source in-
 * point, all as frame timestamps in the sequence's frame-rate timebase
 * (same convention as the rest of this family); `out` must be greater than
 * `in` and `media_in` must be >= 0. No track is created implicitly: an
 * out-of-range `track_index` fails with OAKENGINE_E_NOT_FOUND.
 *
 * The add mirrors the application's drop-import chain reduced to its
 * editing core (NodeAddCommand + NodeEdgeAddCommand onto
 * ClipBlock::k_buffer_in + TrackPlaceBlockCommand, pushed as one undoable
 * MultiUndoCommand). Returns a borrowed clip handle, or NULL on failure
 * (see oakengine_sequence_last_error()).
 */
OAKENGINE_API OakEngineClip *oakengine_sequence_add_footage_clip(
	OakEngineSequence *seq, OakEngineFootage *footage, int track_type,
	int track_index, int64_t in, int64_t out, int64_t media_in);

/**
 * @brief Number of clips on the track at `track_index` (within the
 * `track_type` list). Gap blocks are not clips and are not counted.
 * Returns the count (>= 0) or a negative OAKENGINE_E_* code
 * (OAKENGINE_E_NOT_FOUND when the track does not exist).
 */
OAKENGINE_API int oakengine_sequence_clip_count(OakEngineSequence *self,
												int track_type,
												int track_index);

/**
 * @brief Borrowed handle of the clip at `clip_index` on the track (gap
 * blocks are skipped), or NULL when out of range.
 */
OAKENGINE_API OakEngineClip *oakengine_sequence_clip_at(
	OakEngineSequence *self, int track_type, int track_index, int clip_index);

/**
 * @brief The clip's timeline range (`in`/`out`) and source in-point
 * (`media_in`) as frame timestamps in the sequence's frame-rate timebase.
 * Any pointer may be NULL.
 */
OAKENGINE_API int oakengine_clip_get_range(const OakEngineClip *self,
										   int64_t *in, int64_t *out,
										   int64_t *media_in);

/* ---- Editing primitives, round 2: split / ripple delete / trim / move ----
 *
 * All four are undoable like the other editing primitives and report
 * failures through oakengine_sequence_last_error(). Clips are addressed by
 * (track_type, track_index, clip_index) exactly like
 * oakengine_sequence_clip_at() (gap blocks are skipped). All times are
 * frame timestamps in the sequence's frame-rate timebase.
 */

/**
 * @brief Split the addressed clip in two at timeline `time` (undoable;
 * olive::BlockSplitCommand).
 *
 * `time` must lie strictly inside the clip's range. The left part keeps the
 * clip's in-point, the right part starts at `time` with its media in-point
 * advanced accordingly (the engine's split semantics). Returns OAKENGINE_OK
 * or a negative code (OAKENGINE_E_NOT_FOUND for a missing clip,
 * OAKENGINE_E_INVALID for a time outside the clip).
 */
OAKENGINE_API int oakengine_sequence_split_clip(OakEngineSequence *seq,
												int track_type,
												int track_index,
												int clip_index, int64_t time);

/**
 * @brief Delete the addressed clip and shift all following clips on the
 * track left by its length (undoable;
 * olive::TrackRippleRemoveAreaCommand).
 */
OAKENGINE_API int oakengine_sequence_ripple_delete_clip(OakEngineSequence *seq,
														int track_type,
														int track_index,
														int clip_index);

/**
 * @brief Change the clip's timeline range (undoable; olive::BlockTrimCommand,
 * the application's trim command).
 *
 * Pass the current value for the end that should stay unchanged; changing
 * both ends is applied as an in-trim followed by an out-trim in one
 * undoable command. Requires new_out > new_in and new_in >= 0. When the
 * in-point moves, the clip's media in-point moves with it (the engine's
 * set_length_and_media_in() alignment); adjacent gaps absorb the difference
 * (the engine's trim semantics, adjacent clips are not rolled). The clip
 * handle must still be on a track.
 */
OAKENGINE_API int oakengine_clip_trim(OakEngineClip *clip, int64_t new_in,
									  int64_t new_out);

/**
 * @brief Move the addressed clip to start at `new_in` on the same track
 * (undoable).
 *
 * Length and media in-point are preserved; the old spot is filled with a
 * gap (olive::TrackReplaceBlockWithGapCommand) and the clip is placed at
 * the destination (olive::TrackPlaceBlockCommand, which ripples whatever
 * was there). Moving across tracks is a later milestone. `new_in` must be
 * >= 0.
 */
OAKENGINE_API int oakengine_sequence_move_clip(OakEngineSequence *seq,
											   int track_type,
											   int track_index,
											   int clip_index,
											   int64_t new_in);

/* ---- Batch editing (timeline panel) ------------------------------------------
 *
 * Higher-level operations mirroring the application's timeline panel
 * (app/widget/timelinewidget), each undoable as ONE command like the
 * panel's own undo entries. Clip arrays hold borrowed handles
 * (oakengine_sequence_clip_at(); the handle is the engine ClipBlock
 * pointer in this family, so the application can pass its own clips
 * directly). All times are frame timestamps in the sequence's frame-rate
 * timebase.
 */

/**
 * @brief Split every given clip at timeline `time_ts`, preserving links
 * (undoable; olive::BlockSplitPreservingLinksCommand -- the application's
 * razor tool / split-at-playhead command).
 *
 * Clips not spanning `time_ts` are skipped (same as the engine command);
 * when none of the clips spans it, the call fails with
 * OAKENGINE_E_NOT_FOUND and nothing is pushed. The halves of linked clips
 * come out linked, like the application's split.
 */
OAKENGINE_API int oakengine_sequence_split_clips(
	OakEngineSequence *seq, OakEngineClip **clips, int clip_count,
	int64_t time_ts);

/**
 * @brief Delete clips leaving gaps, optionally rippling regions closed
 * (undoable; the clip-deletion core of the application's
 * TimelineWidget::DeleteSelected).
 *
 * Each clip is replaced with a gap (olive::TrackReplaceBlockWithGapCommand,
 * transitions left to the caller like the application) and removed from
 * the graph with its exclusive dependencies
 * (olive::NodeRemoveWithExclusiveDependenciesAndDisconnect).
 *
 * When `ripple` != 0, a olive::TimelineRippleDeleteGapsAtRegionsCommand
 * follows over `ripple_ranges_ts` -- 4 int64 per range: track_type,
 * track_index, in_ts, out_ts (NULL with `ripple_range_count` 0 ripples the
 * deleted clips' own ranges instead). `rippled` (may be NULL) receives 1
 * when the ripple actually produced commands. Everything lands as one
 * undoable command.
 */
OAKENGINE_API int oakengine_sequence_delete_clips(
	OakEngineSequence *seq, OakEngineClip **clips, int clip_count, int ripple,
	const int64_t *ripple_ranges_ts, int ripple_range_count, int *rippled);

/**
 * @brief Remove the area [in_ts, out_ts) on every track and shift the
 * following content left (undoable;
 * olive::TimelineRippleRemoveAreaCommand -- the application's
 * "ripple to playhead"). `in_ts` must be >= 0 and `out_ts` > `in_ts`.
 */
OAKENGINE_API int oakengine_sequence_ripple_delete_range(
	OakEngineSequence *seq, int64_t in_ts, int64_t out_ts);

/**
 * @brief Delete the workarea range on every track (undoable, ONE command;
 * the application's TimelineWidget::delete_in_to_out).
 *
 * `ripple` != 0: the area is removed and following content shifts left
 * (olive::TimelineRippleRemoveAreaCommand). `ripple` == 0: a gap of the
 * range's length is placed at `in_ts` on every unlocked track (the
 * application's gap-insert path). Either way the workarea is disabled
 * afterwards (olive::WorkareaSetEnabledCommand), all as one undoable
 * command. Requires an enabled workarea on the sequence
 * (OAKENGINE_E_STATE otherwise) and 0 <= in_ts < out_ts. The application's
 * playhead move after a ripple stays with the caller (not undoable).
 */
OAKENGINE_API int oakengine_sequence_ripple_delete_in_to_out(
	OakEngineSequence *seq, int ripple, int64_t in_ts, int64_t out_ts);

/**
 * @brief Trim the nearest clip of every unlocked track to `point_ts`
 * (undoable, ONE command; the application's TimelineWidget::edit_to).
 *
 * `edge` 0 (in): per track the nearest block starting before or at the
 * point gets its in-point trimmed to the point; `edge` 1 (out): the
 * nearest block before the point gets its out-point trimmed to it
 * (olive::BlockTrimCommand per affected track). Gap blocks and blocks
 * already at the point are skipped, like the application. Returns the
 * number of trimmed clips (>= 0; 0 when nothing qualified and no command
 * is pushed) or a negative code.
 */
OAKENGINE_API int oakengine_sequence_trim_clips_to(OakEngineSequence *seq,
												   int edge, int64_t point_ts);

/**
 * @brief Remove every empty track (undoable, ONE command; the
 * application's "delete all empty tracks").
 *
 * `track_type` is an OAKENGINE_TRACK_TYPE_* value to only purge that
 * type, or -1 for all types (the application's behavior). Returns the
 * number of removed tracks (>= 0; 0 when no track was empty and no
 * command is pushed) or a negative code.
 */
OAKENGINE_API int oakengine_sequence_delete_empty_tracks(OakEngineSequence *seq,
														 int track_type);

/**
 * @brief Remove the markers at the given times (undoable, ONE command;
 * the marker-list equivalent of the application's marker
 * SeekableWidget::delete_selected).
 *
 * `times_ts` holds `count` marker in-point timestamps; each must name an
 * existing marker (markers are unique per time in the engine) or the
 * whole call fails with OAKENGINE_E_NOT_FOUND and nothing is pushed.
 * Returns the number of removed markers (>= 0) or a negative code.
 */
OAKENGINE_API int oakengine_sequence_marker_remove_many(
	OakEngineSequence *seq, const int64_t *times_ts, int count);

/* ---- Track structure and markers ------------------------------------------
 *
 * Track structure edits are undoable like the other editing primitives.
 * Track height, mute and lock are NOT undoable in the engine (the
 * application wires those buttons straight to the setters, see
 * app/widget/timelinewidget/trackview/trackviewitem.cpp), and this family
 * follows that behavior. Marker edits are undoable.
 * All marker times are frame timestamps in the sequence's frame-rate
 * timebase, like the rest of the family.
 */

/**
 * @brief Remove a track and its content (undoable;
 * olive::TimelineRemoveTrackCommand). OAKENGINE_E_NOT_FOUND when the track
 * does not exist.
 */
OAKENGINE_API int oakengine_sequence_remove_track(OakEngineSequence *seq,
												  int track_type,
												  int track_index);

/**
 * @brief Move the track at `from_index` to `to_index` within the track
 * list (undoable).
 *
 * Implemented as a true move (take out and re-insert, the tracks in
 * between shift), assembled from the engine's edge commands. `from_index`
 * == `to_index` is a no-op success; an out-of-range index fails with
 * OAKENGINE_E_NOT_FOUND.
 */
OAKENGINE_API int oakengine_sequence_move_track(OakEngineSequence *seq,
												int track_type,
												int from_index, int to_index);

/**
 * @brief Track height in the engine's internal units
 * (Track::get_track_height(); NOT undoable, see the section comment).
 */
OAKENGINE_API int oakengine_track_get_height(const OakEngineSequence *seq,
											 int track_type, int track_index,
											 double *height);

/**
 * @brief Set the track height in internal units
 * (Track::set_track_height(); NOT undoable). `height` must be > 0.
 */
OAKENGINE_API int oakengine_track_set_height(OakEngineSequence *seq,
											 int track_type, int track_index,
											 double height);

/**
 * @brief 1 if the track is muted (Track::is_muted()).
 */
OAKENGINE_API int oakengine_track_is_muted(const OakEngineSequence *seq,
										   int track_type, int track_index);

/**
 * @brief Mute or unmute the track (Track::set_muted(); NOT undoable).
 */
OAKENGINE_API int oakengine_track_set_muted(OakEngineSequence *seq,
											int track_type, int track_index,
											int muted);

/**
 * @brief 1 if the track is locked (Track::is_locked()).
 */
OAKENGINE_API int oakengine_track_is_locked(const OakEngineSequence *seq,
											int track_type, int track_index);

/**
 * @brief Lock or unlock the track (Track::set_locked(); NOT undoable).
 */
OAKENGINE_API int oakengine_track_set_locked(OakEngineSequence *seq,
											 int track_type, int track_index,
											 int locked);

/**
 * @brief Add a timeline marker at `time_ts` named `name` (undoable;
 * olive::MarkerAddCommand).
 *
 * The engine does not allow two markers at the exact same time (its
 * insertion asserts on it), so adding one fails with OAKENGINE_E_STATE.
 * `name` may be NULL for an empty name.
 */
OAKENGINE_API int oakengine_sequence_marker_add(OakEngineSequence *seq,
												int64_t time_ts,
												const char *name);

/**
 * @brief Add a timeline marker with an explicit color index (undoable).
 *
 * Same as oakengine_sequence_marker_add() (which passes color 0) but the
 * caller picks the marker color, like the application's "set marker"
 * action (color of the closest marker, or the configured default).
 */
OAKENGINE_API int oakengine_sequence_marker_add_ex(OakEngineSequence *seq,
												   int64_t time_ts,
												   const char *name,
												   int color);

/**
 * @brief Remove the (first) marker at `time_ts` (undoable;
 * olive::MarkerRemoveCommand). OAKENGINE_E_NOT_FOUND when no marker exists
 * at that exact time.
 */
OAKENGINE_API int oakengine_sequence_marker_remove(OakEngineSequence *seq,
												   int64_t time_ts);

/**
 * @brief Rename the (first) marker at `time_ts` (undoable;
 * olive::MarkerChangeNameCommand). OAKENGINE_E_NOT_FOUND when no marker
 * exists at that exact time.
 */
OAKENGINE_API int oakengine_sequence_marker_rename(OakEngineSequence *seq,
												   int64_t time_ts,
												   const char *name);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_TIMELINE_H */
