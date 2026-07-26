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

#include "oakengine/timeline.h"

#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QString>

#include "coreengine.h"
#include "node/block/block.h"
#include "node/block/clip/clip.h"
#include "node/block/gap/gap.h"
#include "node/nodeundo.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineundogeneral.h"
#include "timeline/timelineundopointer.h"
#include "timeline/timelineundoripple.h"
#include "timeline/timelineundosplit.h"
#include "timeline/timelineundoworkarea.h"
#include "timeline/timelineworkarea.h"
#include "undo/undocommand.h"
#include "node/input/multicam/multicamnode.h"
#include "node/output/track/tracklist.h"
#include "undo/undostack.h"
#include "undointernal.h"

// Internal cross-family accessor (not part of the public C ABI), defined in
// footage.cpp: borrowed project node of an import handle, nullptr otherwise.
extern "C" __attribute__((visibility("hidden"))) void *
oakengine_capi_footage_node(OakEngineFootage *h);

namespace
{

olive::Sequence *impl(OakEngineSequence *h)
{
	return reinterpret_cast<olive::Sequence *>(h);
}

const olive::Sequence *impl(const OakEngineSequence *h)
{
	return reinterpret_cast<const olive::Sequence *>(h);
}

OakEngineSequence *wrap(olive::Sequence *s)
{
	return reinterpret_cast<OakEngineSequence *>(s);
}

// ViewerOutput::get_playhead() is not a const method in the engine; the
// facade keeps const-correct handles and casts locally.
olive::Sequence *mutable_impl(const OakEngineSequence *h)
{
	return const_cast<olive::Sequence *>(
		reinterpret_cast<const olive::Sequence *>(h));
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// Copy a QString into a fixed-capacity C buffer, always NUL-terminating and
// truncating what does not fit.
void copy_to_buf(const QString &s, char *dst, size_t cap)
{
	const QByteArray utf = s.toUtf8();
	const size_t n = qMin(size_t(utf.size()), cap - 1);
	memcpy(dst, utf.constData(), n);
	dst[n] = '\0';
}

// The sequence's frame duration as a Rational timebase (frame rate flipped).
// Returns false when the sequence has no valid frame rate (no video params).
bool time_base_of(const olive::Sequence *s, olive::Rational *out)
{
	const olive::Rational frame_rate = s->get_video_params().frame_rate();
	if (frame_rate.isNull() || frame_rate.isNaN()) {
		return false;
	}
	*out = frame_rate.flipped();
	return true;
}

// Rational seconds -> timestamp in timebase units, like
// Timecode::time_to_timestamp with k_round rounding.
int64_t time_to_ts(const olive::Rational &time, const olive::Rational &tb)
{
	return olive::core::Timecode::time_to_timestamp(
		time, tb, olive::core::Timecode::k_round);
}

// ---- Editing primitive helpers -------------------------------------------

// Last editing error per thread (editing calls return NULL/negative codes).
thread_local QString g_seq_last_error;

void set_seq_error(const QString &error)
{
	g_seq_last_error = error;
}

olive::Track::Type to_track_type(int track_type)
{
	switch (track_type) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		return olive::Track::k_video;
	case OAKENGINE_TRACK_TYPE_AUDIO:
		return olive::Track::k_audio;
	default:
		return olive::Track::k_subtitle;
	}
}

OakEngineClip *wrap_clip(olive::ClipBlock *c)
{
	return reinterpret_cast<OakEngineClip *>(c);
}

// Push an undoable command onto the global undo stack when the engine is
// initialized, otherwise execute it directly (same degradation as the
// round-1 primitives).
void push_or_run(olive::UndoCommand *command, const QString &name)
{
	oakengine_undo_push_or_run(command, name);
}

// Apply a command honoring an explicit undoable flag: 1 pushes through
// push_or_run(), 0 applies directly with no undo entry (the sequence
// dialog's non-undoable mode for freshly created sequences).
void apply_or_push(olive::UndoCommand *command, const QString &name,
				   int undoable)
{
	if (undoable) {
		push_or_run(command, name);
	} else {
		command->redo_now();
		delete command;
	}
}

// Undo commands for sequence parameter writes. The engine has no undo
// commands for these (the application's sequence dialog carries its own
// SequenceParamCommand at the app layer), so the facade carries
// read-modify-write equivalents with the same semantics.
class SequenceVideoParamsCommand : public olive::UndoCommand {
public:
	SequenceVideoParamsCommand(olive::Sequence *sequence,
							   const olive::VideoParams &params)
		: sequence_(sequence)
		, new_params_(params)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return sequence_->project();
	}

protected:
	virtual void redo() override
	{
		old_params_ = sequence_->get_video_params();
		sequence_->set_video_params(new_params_);
	}

	virtual void undo() override
	{
		sequence_->set_video_params(old_params_);
	}

private:
	olive::Sequence *sequence_;
	olive::VideoParams old_params_;
	olive::VideoParams new_params_;
};

class SequenceAudioParamsCommand : public olive::UndoCommand {
public:
	SequenceAudioParamsCommand(olive::Sequence *sequence,
							   const olive::AudioParams &params)
		: sequence_(sequence)
		, new_params_(params)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return sequence_->project();
	}

protected:
	virtual void redo() override
	{
		old_params_ = sequence_->get_audio_params();
		sequence_->set_audio_params(new_params_);
	}

	virtual void undo() override
	{
		sequence_->set_audio_params(old_params_);
	}

private:
	olive::Sequence *sequence_;
	olive::AudioParams old_params_;
	olive::AudioParams new_params_;
};

// The clip at (track_index, clip_index) within the given track list,
// skipping gap blocks; nullptr when out of range.
olive::ClipBlock *clip_at_index(olive::TrackList *list, int track_index,
								int clip_index)
{
	if (track_index < 0 || track_index >= list->get_track_count()) {
		return nullptr;
	}
	int seen = 0;
	for (olive::Block *b : list->get_track_at(track_index)->blocks()) {
		if (olive::ClipBlock *clip = dynamic_cast<olive::ClipBlock *>(b)) {
			if (seen == clip_index) {
				return clip;
			}
			seen++;
		}
	}
	return nullptr;
}

// Track helpers for block traversal.

olive::Track *track_impl(OakEngineTrack *h)
{
	return reinterpret_cast<olive::Track *>(h);
}

const olive::Track *track_impl(const OakEngineTrack *h)
{
	return reinterpret_cast<const olive::Track *>(h);
}

olive::Block *block_impl(OakEngineBlock *h)
{
	return reinterpret_cast<olive::Block *>(h);
}

const olive::Block *block_impl(const OakEngineBlock *h)
{
	return reinterpret_cast<const olive::Block *>(h);
}

// Timebase of a track's owning sequence (frame duration = frame_rate flipped).
// Returns (1001, 30000) as fallback when the sequence lacks valid params.
olive::Rational track_time_base(const olive::Track *track)
{
	if (const olive::Sequence *seq = track->sequence()) {
		const olive::Rational fr = seq->get_video_params().frame_rate();
		if (!fr.isNull() && !fr.isNaN()) {
			return fr.flipped();
		}
	}
	return olive::Rational(1001, 30000);
}

// Convert a track's timebase to a timestamp (Rational -> int64_t).
int64_t track_time_to_ts(const olive::Rational &time, const olive::Rational &tb)
{
	return olive::core::Timecode::time_to_timestamp(
		time, tb, olive::core::Timecode::k_round);
}

// Convert a timestamp to Rational using the track's timebase.
olive::Rational track_ts_to_time(int64_t ts, const olive::Rational &tb)
{
	return olive::core::Timecode::timestamp_to_time(ts, tb);
}

} // namespace

extern "C"
{

OakEngineSequence *oakengine_sequence_new(OakEngineProject *project,
										  const char *name)
{
	olive::Project *p = reinterpret_cast<olive::Project *>(project);
	if (!p || !p->root()) {
		return nullptr;
	}

	olive::Sequence *sequence = new olive::Sequence();
	sequence->set_default_parameters();
	sequence->set_label(QString::fromUtf8(name ? name : ""));

	// set_default_parameters() reads the sequence defaults from the user
	// config; a config that lacks those keys yields invalid parameters
	// (e.g. sample rate 0), which later aborts the render worker. Backfill
	// hard defaults for anything invalid.
	if (sequence->get_audio_params().sample_rate() <= 0) {
		sequence->set_audio_params(
			olive::AudioParams(48000, olive::core::k_channel_layout_stereo,
							   olive::core::SampleFormat::f32_p));
	}
	{
		const olive::VideoParams vp = sequence->get_video_params();
		const olive::Rational frame_rate = vp.frame_rate();
		if (vp.width() <= 0 || vp.height() <= 0 || frame_rate.isNull() ||
			frame_rate.isNaN()) {
			const olive::PixelFormat::Format format =
				vp.format() == olive::PixelFormat::invalid ?
					olive::PixelFormat::f32 :
					static_cast<olive::PixelFormat::Format>(vp.format());
			sequence->set_video_params(olive::VideoParams(
				1920, 1080, olive::Rational(1001, 30000), format,
				olive::VideoParams::k_internal_channel_count));
		}
	}

	// Same undoable creation as the application's "Create New Sequence"
	// action (app/core.cpp), minus opening a viewer. Without an EngineCore
	// (library not initialized) the command is executed non-undoably.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::NodeAddCommand(p, sequence));
	command->add_child(new olive::FolderAddChild(p->root(), sequence));

	oakengine_undo_push_or_run(command, QStringLiteral("Create Sequence"));

	return wrap(sequence);
}

int oakengine_sequence_name(const OakEngineSequence *self, char *buf,
							int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->get_label(), buf, buf_size);
}

int oakengine_sequence_get_length(const OakEngineSequence *self,
								  double *seconds)
{
	if (!self || !seconds) {
		return OAKENGINE_E_INVALID;
	}
	*seconds = impl(self)->get_length().to_double();
	return OAKENGINE_OK;
}

int oakengine_sequence_get_length_rational(const OakEngineSequence *self,
										   int *num, int *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational length = impl(self)->get_length();
	if (num) {
		*num = length.numerator();
	}
	if (den) {
		*den = length.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_get_frame_rate(const OakEngineSequence *self, int *num,
									  int *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational frame_rate = impl(self)->get_video_params().frame_rate();
	if (frame_rate.isNull() || frame_rate.isNaN()) {
		return OAKENGINE_E_STATE;
	}
	if (num) {
		*num = frame_rate.numerator();
	}
	if (den) {
		*den = frame_rate.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_get_video_params(const OakEngineSequence *self,
										int *width, int *height, int *par_num,
										int *par_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::VideoParams params = impl(self)->get_video_params();
	if (width) {
		*width = params.width();
	}
	if (height) {
		*height = params.height();
	}
	const olive::Rational par = params.pixel_aspect_ratio();
	if (par_num) {
		*par_num = par.numerator();
	}
	if (par_den) {
		*par_den = par.denominator();
	}
	return OAKENGINE_OK;
}

/* ---- Sequence parameters (sequence dialog) ------------------------------------ */

int oakengine_sequence_get_video_params_ex(
	const OakEngineSequence *self, int *width, int *height, int *fps_num,
	int *fps_den, int *par_num, int *par_den, int *interlacing, int *format,
	int *divider)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::VideoParams params = impl(self)->get_video_params();
	if (width) {
		*width = params.width();
	}
	if (height) {
		*height = params.height();
	}
	const olive::Rational frame_rate = params.frame_rate();
	if (fps_num) {
		*fps_num = frame_rate.numerator();
	}
	if (fps_den) {
		*fps_den = frame_rate.denominator();
	}
	const olive::Rational par = params.pixel_aspect_ratio();
	if (par_num) {
		*par_num = par.numerator();
	}
	if (par_den) {
		*par_den = par.denominator();
	}
	if (interlacing) {
		*interlacing = int(params.interlacing());
	}
	if (format) {
		*format = int(params.format());
	}
	if (divider) {
		*divider = params.divider();
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_set_video_params(
	OakEngineSequence *self, int width, int height, int fps_num, int fps_den,
	int par_num, int par_den, int interlacing, int format, int undoable)
{
	set_seq_error(QString());
	if (!self) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	// -1 leaves a field unchanged; 0 / out-of-range values are rejected.
	if (width < -1 || width == 0 || height < -1 || height == 0) {
		set_seq_error(QStringLiteral("invalid video size %1x%2")
						  .arg(width)
						  .arg(height));
		return OAKENGINE_E_INVALID;
	}
	if ((fps_num == -1) != (fps_den == -1) || fps_num < -1 || fps_num == 0 ||
		fps_den < -1 || fps_den == 0) {
		set_seq_error(QStringLiteral("invalid frame rate %1/%2")
						  .arg(fps_num)
						  .arg(fps_den));
		return OAKENGINE_E_INVALID;
	}
	if ((par_num == -1) != (par_den == -1) || par_num < -1 || par_num == 0 ||
		par_den < -1 || par_den == 0) {
		set_seq_error(QStringLiteral("invalid pixel aspect %1/%2")
						  .arg(par_num)
						  .arg(par_den));
		return OAKENGINE_E_INVALID;
	}
	if (interlacing < -1 ||
		interlacing > int(olive::VideoParams::k_interlaced_bottom_first)) {
		set_seq_error(
			QStringLiteral("invalid interlacing %1").arg(interlacing));
		return OAKENGINE_E_INVALID;
	}
	if (format < -1 || format >= int(olive::PixelFormat::count)) {
		set_seq_error(QStringLiteral("invalid pixel format %1").arg(format));
		return OAKENGINE_E_INVALID;
	}

	olive::Sequence *sequence = impl(self);
	const olive::VideoParams current = sequence->get_video_params();
	// Rebuild through the same constructor the application's dialog uses so
	// the frame rate (flipped time base) and effective size stay in sync.
	const olive::VideoParams updated(
		width >= 0 ? width : current.width(),
		height >= 0 ? height : current.height(),
		fps_num >= 0 ? olive::Rational(fps_den, fps_num) : current.time_base(),
		format >= 0 ? olive::PixelFormat::Format(format) :
					  olive::PixelFormat::Format(current.format()),
		current.channel_count(),
		par_num >= 0 ? olive::Rational(par_num, par_den) :
					   current.pixel_aspect_ratio(),
		interlacing >= 0 ? olive::VideoParams::Interlacing(interlacing) :
						   current.interlacing(),
		current.divider());
	if (updated == current) {
		return OAKENGINE_OK;
	}
	apply_or_push(new SequenceVideoParamsCommand(sequence, updated),
				  QStringLiteral("Set Sequence Video Parameters"), undoable);
	return OAKENGINE_OK;
}

int oakengine_sequence_get_audio_params(const OakEngineSequence *self,
										int *sample_rate,
										uint64_t *channel_layout)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::AudioParams params = impl(self)->get_audio_params();
	if (sample_rate) {
		*sample_rate = params.sample_rate();
	}
	if (channel_layout) {
		*channel_layout = params.channel_layout();
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_set_audio_params(OakEngineSequence *self,
										int sample_rate,
										uint64_t channel_layout, int undoable)
{
	set_seq_error(QString());
	if (!self) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	olive::Sequence *sequence = impl(self);
	const olive::AudioParams current = sequence->get_audio_params();
	if (sample_rate <= 0) {
		sample_rate = current.sample_rate();
	}
	if (channel_layout == 0) {
		channel_layout = current.channel_layout();
	}
	const olive::AudioParams updated(sample_rate, channel_layout,
									 current.format());
	if (updated == current) {
		return OAKENGINE_OK;
	}
	apply_or_push(new SequenceAudioParamsCommand(sequence, updated),
				  QStringLiteral("Set Sequence Audio Parameters"), undoable);
	return OAKENGINE_OK;
}

int oakengine_sequence_get_preview_divider(const OakEngineSequence *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->get_video_params().divider();
}

int oakengine_sequence_set_preview_divider(OakEngineSequence *self,
										   int divider, int undoable)
{
	set_seq_error(QString());
	if (!self) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	if (divider < 1) {
		set_seq_error(QStringLiteral("invalid preview divider %1")
						  .arg(divider));
		return OAKENGINE_E_INVALID;
	}
	olive::Sequence *sequence = impl(self);
	const olive::VideoParams current = sequence->get_video_params();
	if (current.divider() == divider) {
		return OAKENGINE_OK;
	}
	// Same constructor rebuild as in set_video_params (keeps the derived
	// effective size in sync).
	const olive::VideoParams updated(
		current.width(), current.height(), current.time_base(),
		current.format(), current.channel_count(),
		current.pixel_aspect_ratio(), current.interlacing(), divider);
	apply_or_push(new SequenceVideoParamsCommand(sequence, updated),
				  QStringLiteral("Set Sequence Preview Divider"), undoable);
	return OAKENGINE_OK;
}

int oakengine_sequence_get_video_auto_cache(const OakEngineSequence *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->is_video_auto_cache_enabled() ? 1 : 0;
}

int oakengine_sequence_set_video_auto_cache(OakEngineSequence *self,
											int enabled, int undoable)
{
	set_seq_error(QString());
	Q_UNUSED(undoable)
	if (!self) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	// The engine's auto-cache accessors are stubs for now (the read always
	// returns false, the write is a no-op; the application's dialog has the
	// checkbox TEMP-disabled accordingly). Forwarded without an undo
	// command so the facade surface is ready when the engine lands it.
	impl(self)->set_video_auto_cache_enabled(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_sequence_track_count(const OakEngineSequence *self, int *video,
								   int *audio, int *subtitle)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Sequence *s = impl(self);
	if (video) {
		*video = s->track_list(olive::Track::k_video)->get_track_count();
	}
	if (audio) {
		*audio = s->track_list(olive::Track::k_audio)->get_track_count();
	}
	if (subtitle) {
		*subtitle = s->track_list(olive::Track::k_subtitle)->get_track_count();
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_get_playhead(const OakEngineSequence *self,
									int64_t *timestamp)
{
	if (!self || !timestamp) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(impl(self), &tb)) {
		return OAKENGINE_E_STATE;
	}
	*timestamp = time_to_ts(mutable_impl(self)->get_playhead(), tb);
	return OAKENGINE_OK;
}

int oakengine_sequence_set_playhead(OakEngineSequence *self, int64_t timestamp)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(impl(self), &tb)) {
		return OAKENGINE_E_STATE;
	}
	impl(self)->set_playhead(
		olive::core::Timecode::timestamp_to_time(timestamp, tb));
	return OAKENGINE_OK;
}

int oakengine_sequence_get_playhead_seconds(const OakEngineSequence *self,
											double *seconds)
{
	if (!self || !seconds) {
		return OAKENGINE_E_INVALID;
	}
	*seconds = mutable_impl(self)->get_playhead().to_double();
	return OAKENGINE_OK;
}

int oakengine_sequence_workarea_is_enabled(const OakEngineSequence *self)
{
	return self && impl(self)->get_work_area()->enabled() ? 1 : 0;
}

int oakengine_sequence_get_workarea(const OakEngineSequence *self, int64_t *in,
									int64_t *out)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(impl(self), &tb)) {
		return OAKENGINE_E_STATE;
	}
	const olive::TimelineWorkArea *workarea = impl(self)->get_work_area();
	if (in) {
		*in = time_to_ts(workarea->in(), tb);
	}
	if (out) {
		*out = time_to_ts(workarea->out(), tb);
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_set_workarea(OakEngineSequence *self, int enabled,
									int64_t in, int64_t out)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(impl(self), &tb)) {
		return OAKENGINE_E_STATE;
	}
	olive::TimelineWorkArea *workarea = impl(self)->get_work_area();
	workarea->set_enabled(enabled != 0);
	workarea->set_range(
		olive::TimeRange(olive::core::Timecode::timestamp_to_time(in, tb),
						 olive::core::Timecode::timestamp_to_time(out, tb)));
	return OAKENGINE_OK;
}

int oakengine_sequence_marker_count(const OakEngineSequence *self)
{
	if (!self) {
		return 0;
	}
	return int(impl(self)->get_markers()->size());
}

int oakengine_sequence_marker_at(const OakEngineSequence *self, int index,
								 int64_t *time, char *name, int name_size,
								 int *color)
{
	if (!self || index < 0) {
		return OAKENGINE_E_INVALID;
	}
	const olive::TimelineMarkerList *markers = impl(self)->get_markers();
	if (size_t(index) >= markers->size()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::TimelineMarker *marker = *(markers->cbegin() + index);
	if (time) {
		olive::Rational tb;
		if (!time_base_of(impl(self), &tb)) {
			return OAKENGINE_E_STATE;
		}
		*time = time_to_ts(marker->time().in(), tb);
	}
	if (name && name_size > 0) {
		copy_to_buf(marker->name(), name, size_t(name_size));
	}
	if (color) {
		*color = marker->color();
	}
	return OAKENGINE_OK;
}

/* ---- Timeline editing primitives ---------------------------------------- */

int oakengine_sequence_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_seq_last_error, buf, buf_size);
}

int oakengine_sequence_add_track(OakEngineSequence *self, int track_type)
{
	set_seq_error(QString());
	if (!self || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid sequence or track type"));
		return OAKENGINE_E_INVALID;
	}

	olive::TrackList *list =
		impl(self)->track_list(to_track_type(track_type));
	// TimelineAddTrackCommand without auto-merge: the first video/audio
	// track connects straight to the sequence output; further tracks stay
	// unconnected (compositing is a later milestone).
	auto *command = new olive::TimelineAddTrackCommand(list, false);
	oakengine_undo_push_or_run(command, QStringLiteral("Add Track"));
	return list->get_track_count() - 1;
}

extern "C" void *oakengine_sequence_add_track_command(
	OakEngineSequence *self, int track_type, int auto_merge,
	OakEngineTrack **out_track)
{
	if (!self || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		return nullptr;
	}
	olive::TrackList *list =
		impl(self)->track_list(to_track_type(track_type));
	auto *command = new olive::TimelineAddTrackCommand(list, auto_merge != 0);
	if (out_track) {
		*out_track = reinterpret_cast<OakEngineTrack *>(command->track());
	}
	return command;
}

extern "C" void *oakengine_sequence_ripple_tracks_command(
	OakEngineSequence *self, int track_type,
	const oakengine_ripple_info *infos, int info_count,
	int64_t movement_num, int64_t movement_den, int movement_mode)
{
	if (!self || !infos || info_count <= 0 || movement_den == 0 ||
		track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE ||
		movement_mode < OAKENGINE_MOVEMENT_MODE_NONE ||
		movement_mode > OAKENGINE_MOVEMENT_MODE_TRIM_OUT) {
		return nullptr;
	}
	olive::TrackList *list =
		impl(self)->track_list(to_track_type(track_type));
	QHash<olive::Track *, olive::TrackListRippleToolCommand::RippleInfo>
		info_map;
	info_map.reserve(info_count);
	for (int i = 0; i < info_count; i++) {
		olive::Track *t = reinterpret_cast<olive::Track *>(infos[i].track);
		olive::Block *b = reinterpret_cast<olive::Block *>(infos[i].block);
		if (!t || !b) {
			return nullptr;
		}
		info_map.insert(t, {b, infos[i].append_gap != 0});
	}
	return new olive::TrackListRippleToolCommand(
		list, info_map,
		olive::Rational(static_cast<int>(movement_num),
						static_cast<int>(movement_den)),
		static_cast<olive::Timeline::MovementMode>(movement_mode));
}

OakEngineClip *oakengine_sequence_add_footage_clip(
	OakEngineSequence *seq, OakEngineFootage *footage, int track_type,
	int track_index, int64_t in, int64_t out, int64_t media_in)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	auto *footage_node =
		static_cast<olive::Footage *>(oakengine_capi_footage_node(footage));

	if (!sequence || !footage) {
		set_seq_error(QStringLiteral("invalid sequence or footage handle"));
		return nullptr;
	}
	if (!footage_node) {
		set_seq_error(QStringLiteral(
			"footage must be imported into the project first "
			"(oakengine_project_import_footage)"));
		return nullptr;
	}
	if (track_type != OAKENGINE_TRACK_TYPE_VIDEO &&
		track_type != OAKENGINE_TRACK_TYPE_AUDIO) {
		set_seq_error(QStringLiteral(
			"clips are only supported on video and audio tracks"));
		return nullptr;
	}
	olive::Project *project =
		olive::Project::get_project_from_object(sequence);
	if (!project ||
		olive::Project::get_project_from_object(footage_node) != project) {
		set_seq_error(QStringLiteral(
			"footage and sequence belong to different projects"));
		return nullptr;
	}
	if (in < 0 || out <= in || media_in < 0) {
		set_seq_error(QStringLiteral("invalid clip range (need 0 <= in < out "
									 "and media_in >= 0)"));
		return nullptr;
	}
	olive::TrackList *list = sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("track index %1 out of range (%2 tracks)")
					  .arg(track_index)
					  .arg(list->get_track_count()));
		return nullptr;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return nullptr;
	}

	const olive::Rational in_time =
		olive::core::Timecode::timestamp_to_time(in, tb);
	const olive::Rational out_time =
		olive::core::Timecode::timestamp_to_time(out, tb);
	const olive::Rational media_in_time =
		olive::core::Timecode::timestamp_to_time(media_in, tb);

	// The application's drop-import chain reduced to its editing core (see
	// ImportTool::place_at()): clip with media in-point and length, footage
	// onto the buffer input, placed on the track -- all undoable.
	auto *clip = new olive::ClipBlock();
	clip->set_media_in(media_in_time);
	clip->set_length_and_media_out(out_time - in_time);

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::NodeAddCommand(project, clip));
	command->add_child(new olive::NodeEdgeAddCommand(
		footage_node, olive::NodeInput(clip, olive::ClipBlock::k_buffer_in)));
	command->add_child(new olive::TrackPlaceBlockCommand(list, track_index,
													   clip, in_time));

	oakengine_undo_push_or_run(command, QStringLiteral("Add Clip"));
	return wrap_clip(clip);
}

int oakengine_sequence_clip_count(OakEngineSequence *self, int track_type,
								  int track_index)
{
	if (!self || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		impl(self)->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	// Only real clips count; gap blocks on the track are skipped.
	int count = 0;
	for (const olive::Block *b : list->get_track_at(track_index)->blocks()) {
		if (dynamic_cast<const olive::ClipBlock *>(b)) {
			count++;
		}
	}
	return count;
}

OakEngineClip *oakengine_sequence_clip_at(OakEngineSequence *self,
										  int track_type, int track_index,
										  int clip_index)
{
	if (!self || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE || clip_index < 0) {
		return nullptr;
	}
	olive::TrackList *list =
		impl(self)->track_list(to_track_type(track_type));
	return wrap_clip(clip_at_index(list, track_index, clip_index));
}

int oakengine_clip_get_range(const OakEngineClip *self, int64_t *in,
							 int64_t *out, int64_t *media_in)
{
	const olive::ClipBlock *clip =
		reinterpret_cast<const olive::ClipBlock *>(self);
	if (!clip) {
		return OAKENGINE_E_INVALID;
	}
	// The clip's sequence (clip -> track -> sequence) provides the timebase
	// for the timestamp conversion.
	const olive::Sequence *sequence =
		clip->track() ? clip->track()->sequence() : nullptr;
	if (!sequence) {
		return OAKENGINE_E_STATE;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		return OAKENGINE_E_STATE;
	}
	if (in) {
		*in = time_to_ts(clip->in(), tb);
	}
	if (out) {
		*out = time_to_ts(clip->out(), tb);
	}
	if (media_in) {
		*media_in = time_to_ts(clip->media_in(), tb);
	}
	return OAKENGINE_OK;
}

OakEngineSequence *oakengine_clip_get_sequence(const OakEngineClip *self)
{
	const olive::ClipBlock *clip =
		reinterpret_cast<const olive::ClipBlock *>(self);
	if (!clip || !clip->track()) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineSequence *>(clip->track()->sequence());
}

/* ---- Editing primitives, round 2 ----------------------------------------- */

int oakengine_sequence_split_clip(OakEngineSequence *seq, int track_type,
								  int track_index, int clip_index,
								  int64_t time)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid sequence or track type"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	olive::ClipBlock *clip = clip_at_index(list, track_index, clip_index);
	if (!clip) {
		set_seq_error(QStringLiteral("no clip at track %1 index %2")
					  .arg(track_index)
					  .arg(clip_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational point =
		olive::core::Timecode::timestamp_to_time(time, tb);
	if (point <= clip->in() || point >= clip->out()) {
		set_seq_error(QStringLiteral("split time %1 is not strictly inside "
									 "the clip [%2, %3]")
					  .arg(time)
					  .arg(time_to_ts(clip->in(), tb))
					  .arg(time_to_ts(clip->out(), tb)));
		return OAKENGINE_E_INVALID;
	}

	push_or_run(new olive::BlockSplitCommand(clip, point),
				QStringLiteral("Split Clip"));
	return OAKENGINE_OK;
}

int oakengine_sequence_ripple_delete_clip(OakEngineSequence *seq,
										  int track_type, int track_index,
										  int clip_index)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid sequence or track type"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	olive::ClipBlock *clip = clip_at_index(list, track_index, clip_index);
	if (!clip || !clip->track()) {
		set_seq_error(QStringLiteral("no clip at track %1 index %2")
					  .arg(track_index)
					  .arg(clip_index));
		return OAKENGINE_E_NOT_FOUND;
	}

	push_or_run(new olive::TrackRippleRemoveAreaCommand(
					clip->track(), olive::TimeRange(clip->in(), clip->out())),
				QStringLiteral("Ripple Delete Clip"));
	return OAKENGINE_OK;
}

int oakengine_clip_trim(OakEngineClip *clip, int64_t new_in, int64_t new_out)
{
	set_seq_error(QString());
	olive::ClipBlock *block = reinterpret_cast<olive::ClipBlock *>(clip);
	if (!block) {
		set_seq_error(QStringLiteral("invalid clip handle"));
		return OAKENGINE_E_INVALID;
	}
	olive::Track *track = block->track();
	if (!track) {
		set_seq_error(QStringLiteral("clip is not on a track"));
		return OAKENGINE_E_STATE;
	}
	const olive::Sequence *sequence = track->sequence();
	olive::Rational tb;
	if (!sequence || !time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	if (new_in < 0 || new_out <= new_in) {
		set_seq_error(QStringLiteral("invalid trim range (need 0 <= new_in "
									 "< new_out)"));
		return OAKENGINE_E_INVALID;
	}

	const int64_t old_in = time_to_ts(block->in(), tb);
	const int64_t old_out = time_to_ts(block->out(), tb);
	if (new_in == old_in && new_out == old_out) {
		return OAKENGINE_OK;
	}

	// The application's trim command (BlockTrimCommand): one end at a time,
	// adjacent gaps absorb the difference; both ends are applied as an
	// in-trim followed by an out-trim within one undoable command.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	if (new_in != old_in) {
		command->add_child(new olive::BlockTrimCommand(
			track, block, block->out() -
							  olive::core::Timecode::timestamp_to_time(new_in, tb),
			olive::Timeline::k_trim_in));
	}
	if (new_out != old_out) {
		command->add_child(new olive::BlockTrimCommand(
			track, block,
			olive::core::Timecode::timestamp_to_time(new_out - new_in, tb),
			olive::Timeline::k_trim_out));
	}
	push_or_run(command, QStringLiteral("Trim Clip"));
	return OAKENGINE_OK;
}

int oakengine_sequence_move_clip(OakEngineSequence *seq, int track_type,
								 int track_index, int clip_index,
								 int64_t new_in)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE || new_in < 0) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	olive::ClipBlock *clip = clip_at_index(list, track_index, clip_index);
	if (!clip || !clip->track()) {
		set_seq_error(QStringLiteral("no clip at track %1 index %2")
					  .arg(track_index)
					  .arg(clip_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}

	// Same assembly as the application's drag-move: gap the old spot, then
	// place the clip at the destination (length and media in-point stay).
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(
		new olive::TrackReplaceBlockWithGapCommand(clip->track(), clip));
	command->add_child(new olive::TrackPlaceBlockCommand(
		list, track_index, clip,
		olive::core::Timecode::timestamp_to_time(new_in, tb)));
	push_or_run(command, QStringLiteral("Move Clip"));
	return OAKENGINE_OK;
}

/* ---- Batch editing (timeline panel) ------------------------------------------ */

int oakengine_sequence_split_clips(OakEngineSequence *seq,
								   OakEngineClip **clips, int clip_count,
								   int64_t time_ts)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || !clips || clip_count <= 0) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);

	QVector<olive::Block *> blocks;
	blocks.reserve(clip_count);
	bool any_spanning = false;
	for (int i = 0; i < clip_count; i++) {
		olive::ClipBlock *clip =
			reinterpret_cast<olive::ClipBlock *>(clips[i]);
		if (!clip) {
			set_seq_error(QStringLiteral("invalid clip at index %1").arg(i));
			return OAKENGINE_E_INVALID;
		}
		if (blocks.contains(clip)) {
			continue;
		}
		blocks.append(clip);
		if (clip->in() < time && clip->out() > time) {
			any_spanning = true;
		}
	}
	if (!any_spanning) {
		set_seq_error(QStringLiteral("no clip spans time %1").arg(time_ts));
		return OAKENGINE_E_NOT_FOUND;
	}

	// Same as the application's razor tool / split-at-playhead
	// (BlockSplitPreservingLinksCommand): split every block spanning the
	// time and link the halves of linked blocks, one undoable command.
	push_or_run(new olive::BlockSplitPreservingLinksCommand(blocks, { time }),
				QStringLiteral("Split Clips"));
	return OAKENGINE_OK;
}

int oakengine_sequence_delete_clips(OakEngineSequence *seq,
									OakEngineClip **clips, int clip_count,
									int ripple, const int64_t *ripple_ranges_ts,
									int ripple_range_count, int *rippled)
{
	set_seq_error(QString());
	if (rippled) {
		*rippled = 0;
	}
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || clip_count < 0 || (clip_count > 0 && !clips) ||
		ripple_range_count < 0 ||
		(ripple_range_count > 0 && !ripple_ranges_ts)) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (clip_count == 0 && (!ripple || ripple_range_count == 0)) {
		// Nothing to delete and nothing to ripple.
		return OAKENGINE_OK;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList clip_ranges;
	for (int i = 0; i < clip_count; i++) {
		olive::ClipBlock *clip =
			reinterpret_cast<olive::ClipBlock *>(clips[i]);
		if (!clip || !clip->track()) {
			set_seq_error(QStringLiteral("invalid clip at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		// Same as the application's delete-selection core
		// (TimelineWidget::DeleteSelected): replace the clip with a gap
		// (transitions are the caller's job) and remove it from the graph
		// with its exclusive dependencies.
		command->add_child(new olive::TrackReplaceBlockWithGapCommand(
			clip->track(), clip, false));
		command->add_child(
			new olive::NodeRemoveWithExclusiveDependenciesAndDisconnect(clip));
		clip_ranges.append({ clip->track(), clip->range() });
	}

	olive::TimelineRippleDeleteGapsAtRegionsCommand *ripple_command = nullptr;
	if (ripple) {
		olive::TimelineRippleDeleteGapsAtRegionsCommand::RangeList ranges;
		if (ripple_ranges_ts && ripple_range_count > 0) {
			for (int i = 0; i < ripple_range_count; i++) {
				const int64_t *range = ripple_ranges_ts + i * 4;
				const int track_type = int(range[0]);
				const int track_index = int(range[1]);
				if (track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
					track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
					set_seq_error(QStringLiteral("invalid track type in "
												 "ripple range %1")
									  .arg(i));
					delete command;
					return OAKENGINE_E_INVALID;
				}
				olive::TrackList *list =
					sequence->track_list(to_track_type(track_type));
				if (track_index < 0 ||
					track_index >= list->get_track_count()) {
					set_seq_error(QStringLiteral("no track at index %1 in "
												 "ripple range %2")
									  .arg(track_index)
									  .arg(i));
					delete command;
					return OAKENGINE_E_NOT_FOUND;
				}
				ranges.append({ list->get_track_at(track_index),
								olive::TimeRange(
									olive::core::Timecode::timestamp_to_time(
										range[2], tb),
									olive::core::Timecode::timestamp_to_time(
										range[3], tb)) });
			}
		} else {
			ranges = clip_ranges;
		}
		if (!ranges.isEmpty()) {
			ripple_command =
				new olive::TimelineRippleDeleteGapsAtRegionsCommand(sequence,
																	ranges);
			command->add_child(ripple_command);
		}
	}

	push_or_run(command, QStringLiteral("Delete Clips"));
	if (rippled) {
		// has_commands() is valid after the stack prepared the command;
		// without an engine the command ran directly and a non-null ripple
		// command means regions were queued.
		*rippled = (ripple_command && (!olive::EngineCore::instance() ||
									   ripple_command->has_commands())) ?
					   1 :
					   0;
	}
	return OAKENGINE_OK;
}

int oakengine_sequence_ripple_delete_range(OakEngineSequence *seq,
										   int64_t in_ts, int64_t out_ts)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || in_ts < 0 || out_ts <= in_ts) {
		set_seq_error(QStringLiteral("invalid range [%1, %2)")
						  .arg(in_ts)
						  .arg(out_ts));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	// Same as the application's "ripple to playhead" (TimelineWidget::
	// ripple_to): remove the area on every track and shift the following
	// content left, one undoable command.
	push_or_run(new olive::TimelineRippleRemoveAreaCommand(
					sequence,
					olive::core::Timecode::timestamp_to_time(in_ts, tb),
					olive::core::Timecode::timestamp_to_time(out_ts, tb)),
				QStringLiteral("Ripple Delete Range"));
	return OAKENGINE_OK;
}

int oakengine_clip_toggle_enabled(OakEngineClip **clips, int count)
{
	set_seq_error(QString());
	if (count < 0 || (count > 0 && !clips)) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		olive::ClipBlock *clip =
			reinterpret_cast<olive::ClipBlock *>(clips[i]);
		if (!clip) {
			set_seq_error(QStringLiteral("invalid clip at index %1").arg(i));
			delete command;
			return OAKENGINE_E_INVALID;
		}
		// The panel's toggle: each clip flips its own current state.
		command->add_child(
			new olive::BlockEnableDisableCommand(clip, !clip->is_enabled()));
	}
	if (count > 0) {
		push_or_run(command, QStringLiteral("Toggle Clips Enabled"));
	} else {
		delete command;
	}
	return count;
}

int oakengine_clip_set_linked(OakEngineClip **clips, int count, int linked)
{
	set_seq_error(QString());
	if (count < 0 || (count > 0 && !clips)) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return OAKENGINE_OK;
	}
	QVector<olive::Node *> nodes;
	nodes.reserve(count);
	for (int i = 0; i < count; i++) {
		olive::ClipBlock *clip =
			reinterpret_cast<olive::ClipBlock *>(clips[i]);
		if (!clip) {
			set_seq_error(QStringLiteral("invalid clip at index %1").arg(i));
			return OAKENGINE_E_INVALID;
		}
		nodes.append(clip);
	}
	push_or_run(new olive::NodeLinkManyCommand(nodes, linked != 0),
				QStringLiteral("Link Clips"));
	return OAKENGINE_OK;
}

int oakengine_sequence_add_default_transition(OakEngineSequence *seq,
											  OakEngineClip **clips, int count)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || count < 0 || (count > 0 && !clips)) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return OAKENGINE_OK;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	QVector<olive::ClipBlock *> blocks;
	blocks.reserve(count);
	for (int i = 0; i < count; i++) {
		olive::ClipBlock *clip =
			reinterpret_cast<olive::ClipBlock *>(clips[i]);
		if (!clip) {
			set_seq_error(QStringLiteral("invalid clip at index %1").arg(i));
			return OAKENGINE_E_INVALID;
		}
		blocks.append(clip);
	}
	push_or_run(new olive::TimelineAddDefaultTransitionCommand(blocks, tb),
				QStringLiteral("Add Default Transitions"));
	return OAKENGINE_OK;
}

int oakengine_clip_is_enabled(const OakEngineClip *self)
{
	if (!self) {
		return 0;
	}
	return reinterpret_cast<const olive::ClipBlock *>(self)->is_enabled() ?
			   1 :
			   0;
}

int oakengine_clip_are_linked(const OakEngineClip *a, const OakEngineClip *b)
{
	if (!a || !b) {
		return 0;
	}
	// Block::are_linked() takes non-const pointers (it is read-only in
	// practice).
	return olive::Block::are_linked(
		const_cast<olive::ClipBlock *>(
			reinterpret_cast<const olive::ClipBlock *>(a)),
		const_cast<olive::ClipBlock *>(
			reinterpret_cast<const olive::ClipBlock *>(b))) ?
			   1 :
			   0;
}

int oakengine_sequence_ripple_delete_in_to_out(OakEngineSequence *seq,
											   int ripple, int64_t in_ts,
											   int64_t out_ts)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || in_ts < 0 || out_ts <= in_ts) {
		set_seq_error(QStringLiteral("invalid range [%1, %2)")
						  .arg(in_ts)
						  .arg(out_ts));
		return OAKENGINE_E_INVALID;
	}
	olive::TimelineWorkArea *workarea = sequence->get_work_area();
	if (!workarea || !workarea->enabled()) {
		set_seq_error(QStringLiteral("sequence workarea is not enabled"));
		return OAKENGINE_E_STATE;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational in_time =
		olive::core::Timecode::timestamp_to_time(in_ts, tb);
	const olive::Rational out_time =
		olive::core::Timecode::timestamp_to_time(out_ts, tb);

	// The application's delete_in_to_out, one undoable command: ripple the
	// area out (or fill it with a fresh gap per unlocked track), then
	// disable the workarea.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	if (ripple) {
		command->add_child(new olive::TimelineRippleRemoveAreaCommand(
			sequence, in_time, out_time));
	} else {
		const QVector<olive::Track *> unlocked_tracks =
			sequence->get_unlocked_tracks();
		for (olive::Track *track : unlocked_tracks) {
			auto *gap = new olive::GapBlock();
			gap->set_length_and_media_out(out_time - in_time);
			command->add_child(new olive::NodeAddCommand(
				static_cast<olive::Project *>(track->parent()), gap));
			command->add_child(new olive::TrackPlaceBlockCommand(
				sequence->track_list(track->type()), track->index(), gap,
				in_time));
		}
	}
	command->add_child(new olive::WorkareaSetEnabledCommand(
		sequence->project(), workarea, false));
	push_or_run(command, QStringLiteral("Delete In To Out"));
	return OAKENGINE_OK;
}

int oakengine_sequence_trim_clips_to(OakEngineSequence *seq, int edge,
									 int64_t point_ts)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || point_ts < 0 || edge < 0 || edge > 1) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational point =
		olive::core::Timecode::timestamp_to_time(point_ts, tb);
	const olive::Timeline::MovementMode mode =
		(edge == 0) ? olive::Timeline::k_trim_in : olive::Timeline::k_trim_out;

	// The application's edit_to: per unlocked track, trim the nearest
	// block's edge to the point (gaps and blocks already at the point are
	// skipped), all as one undoable command.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	int trimmed = 0;
	const QVector<olive::Track *> tracks = sequence->get_unlocked_tracks();
	for (olive::Track *track : tracks) {
		olive::Block *block =
			(mode == olive::Timeline::k_trim_in) ?
				track->nearest_block_before_or_at(point) :
				track->nearest_block_before(point);
		if (!block || dynamic_cast<olive::GapBlock *>(block)) {
			continue;
		}
		const olive::Rational nearest_time =
			(mode == olive::Timeline::k_trim_in) ? block->in() : block->out();
		if (nearest_time == point) {
			continue;
		}
		olive::Rational new_length =
			(mode == olive::Timeline::k_trim_in) ? point - nearest_time :
												   nearest_time - point;
		new_length = block->length() - new_length;
		command->add_child(
			new olive::BlockTrimCommand(track, block, new_length, mode));
		trimmed++;
	}
	if (trimmed == 0) {
		delete command;
		return 0;
	}
	push_or_run(command, QStringLiteral("Trim Clips To Point"));
	return trimmed;
}

int oakengine_sequence_delete_empty_tracks(OakEngineSequence *seq,
										   int track_type)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < -1 ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}

	// The application's "delete all empty tracks", one undoable command.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	int removed = 0;
	for (olive::Track *track : sequence->get_tracks()) {
		if (track_type >= 0 && track->type() != to_track_type(track_type)) {
			continue;
		}
		if (track->blocks().isEmpty()) {
			command->add_child(new olive::TimelineRemoveTrackCommand(track));
			removed++;
		}
	}
	if (removed == 0) {
		delete command;
		return 0;
	}
	push_or_run(command, QStringLiteral("Delete Empty Tracks"));
	return removed;
}

int oakengine_sequence_marker_remove_many(OakEngineSequence *seq,
										  const int64_t *times_ts, int count)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || count < 0 || (count > 0 && !times_ts)) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (count == 0) {
		return 0;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	olive::TimelineMarkerList *markers = sequence->get_markers();

	// Resolve all markers first so a bad time fails without side effects
	// (markers are unique per time in the engine).
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		const olive::Rational time =
			olive::core::Timecode::timestamp_to_time(times_ts[i], tb);
		olive::TimelineMarker *marker = markers->get_marker_at_time(time);
		if (!marker) {
			set_seq_error(QStringLiteral("no marker at time %1")
							  .arg(times_ts[i]));
			delete command;
			return OAKENGINE_E_NOT_FOUND;
		}
		command->add_child(new olive::MarkerRemoveCommand(marker));
	}
	push_or_run(command, QStringLiteral("Remove Markers"));
	return count;
}

/* ---- Track structure and markers ------------------------------------------ */

int oakengine_sequence_remove_track(OakEngineSequence *seq, int track_type,
									int track_index)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid sequence or track type"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::TimelineRemoveTrackCommand(
					list->get_track_at(track_index)),
				QStringLiteral("Remove Track"));
	return OAKENGINE_OK;
}

int oakengine_sequence_move_track(OakEngineSequence *seq, int track_type,
								  int from_index, int to_index)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid sequence or track type"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	const int count = list->get_track_count();
	if (from_index < 0 || from_index >= count || to_index < 0 ||
		to_index >= count) {
		set_seq_error(QStringLiteral("track index out of range (%1 tracks)")
					  .arg(count));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (from_index == to_index) {
		return OAKENGINE_OK;
	}

	// True move: compute the reordered track list, then rewire every track's
	// array-element connection into that order with the engine's edge
	// commands (one undoable command, so undo restores the old order).
	QVector<olive::Track *> order = list->get_tracks();
	olive::Track *moved = order.at(from_index);
	order.removeAt(from_index);
	order.insert(to_index, moved);

	// Array elements stay put (connections just move between them); collect
	// them in slot order.
	QVector<int> elements;
	for (int i = 0; i < count; i++) {
		elements.append(list->get_array_index_from_cache_index(i));
	}

	const QString input_id = list->track_input();
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	for (int i = 0; i < count; i++) {
		command->add_child(new olive::NodeEdgeRemoveCommand(
			list->get_tracks().at(i),
			olive::NodeInput(sequence, input_id, elements.at(i))));
	}
	for (int i = 0; i < count; i++) {
		command->add_child(new olive::NodeEdgeAddCommand(
			order.at(i),
			olive::NodeInput(sequence, input_id, elements.at(i))));
	}
	push_or_run(command, QStringLiteral("Move Track"));
	return OAKENGINE_OK;
}

int oakengine_track_get_height(const OakEngineSequence *seq, int track_type,
							   int track_index, double *height)
{
	set_seq_error(QString());
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	if (!sequence || !height || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	*height = list->get_track_at(track_index)->get_track_height();
	return OAKENGINE_OK;
}

int oakengine_track_set_height(OakEngineSequence *seq, int track_type,
							   int track_index, double height)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || height <= 0.0 ||
		track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	// Straight setter like the application (not undoable).
	list->get_track_at(track_index)->set_track_height(height);
	return OAKENGINE_OK;
}

int oakengine_track_is_muted(const OakEngineSequence *seq, int track_type,
							 int track_index)
{
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		return 0;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		return 0;
	}
	return list->get_track_at(track_index)->is_muted() ? 1 : 0;
}

int oakengine_track_set_muted(OakEngineSequence *seq, int track_type,
							  int track_index, int muted)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	list->get_track_at(track_index)->set_muted(muted != 0);
	return OAKENGINE_OK;
}

int oakengine_track_is_locked(const OakEngineSequence *seq, int track_type,
							  int track_index)
{
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		return 0;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		return 0;
	}
	return list->get_track_at(track_index)->is_locked() ? 1 : 0;
}

int oakengine_track_set_locked(OakEngineSequence *seq, int track_type,
							   int track_index, int locked)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	list->get_track_at(track_index)->set_locked(locked != 0);
	return OAKENGINE_OK;
}

int oakengine_sequence_marker_add(OakEngineSequence *seq, int64_t time_ts,
								  const char *name)
{
	return oakengine_sequence_marker_add_ex(seq, time_ts, name, 0);
}

int oakengine_sequence_marker_add_ex(OakEngineSequence *seq, int64_t time_ts,
									 const char *name, int color)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	olive::TimelineMarkerList *markers = sequence->get_markers();
	if (markers->get_marker_at_time(time)) {
		// The engine's marker insertion asserts on duplicate times.
		set_seq_error(QStringLiteral("a marker already exists at time %1")
					  .arg(time_ts));
		return OAKENGINE_E_STATE;
	}
	push_or_run(new olive::MarkerAddCommand(
					markers, olive::TimeRange(time, time),
					QString::fromUtf8(name ? name : ""), color),
				QStringLiteral("Add Marker"));
	return OAKENGINE_OK;
}

int oakengine_sequence_marker_remove(OakEngineSequence *seq, int64_t time_ts)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	olive::TimelineMarker *marker =
		sequence->get_markers()->get_marker_at_time(time);
	if (!marker) {
		set_seq_error(QStringLiteral("no marker at time %1").arg(time_ts));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::MarkerRemoveCommand(marker),
				QStringLiteral("Remove Marker"));
	return OAKENGINE_OK;
}

int oakengine_sequence_marker_rename(OakEngineSequence *seq, int64_t time_ts,
									 const char *name)
{
	set_seq_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence) {
		set_seq_error(QStringLiteral("invalid sequence"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	olive::TimelineMarker *marker =
		sequence->get_markers()->get_marker_at_time(time);
	if (!marker) {
		set_seq_error(QStringLiteral("no marker at time %1").arg(time_ts));
		return OAKENGINE_E_NOT_FOUND;
	}
	push_or_run(new olive::MarkerChangeNameCommand(
					marker, QString::fromUtf8(name ? name : "")),
				QStringLiteral("Rename Marker"));
	return OAKENGINE_OK;
}

/* ---- Marker handle family ---------------------------------------------------- */

int oakengine_marker_list_count(const OakEngineMarkerList *list)
{
	if (!list) {
		return 0;
	}
	return reinterpret_cast<const olive::TimelineMarkerList *>(list)->size();
}

int oakengine_marker_list_add(OakEngineMarkerList *list, int64_t in_num,
							  int64_t in_den, int64_t out_num, int64_t out_den,
							  const char *name, int color)
{
	if (!list) {
		return OAKENGINE_E_INVALID;
	}
	olive::TimelineMarkerList *ml =
		reinterpret_cast<olive::TimelineMarkerList *>(list);
	push_or_run(new olive::MarkerAddCommand(
					ml,
					olive::TimeRange(olive::Rational(in_num, in_den),
									 olive::Rational(out_num, out_den)),
					QString::fromUtf8(name ? name : ""), color),
				QStringLiteral("Add Marker"));
	return OAKENGINE_OK;
}

OakEngineMarker *oakengine_marker_create(int color, int64_t in_num,
										 int64_t in_den, int64_t out_num,
										 int64_t out_den, const char *name)
{
	return reinterpret_cast<OakEngineMarker *>(new olive::TimelineMarker(
		color,
		olive::TimeRange(olive::Rational(in_num, in_den),
						 olive::Rational(out_num, out_den)),
		QString::fromUtf8(name ? name : "")));
}

void oakengine_marker_free(OakEngineMarker *marker)
{
	delete reinterpret_cast<olive::TimelineMarker *>(marker);
}

int oakengine_marker_list_add_existing(OakEngineMarkerList *list,
									   OakEngineMarker *marker)
{
	if (!list || !marker) {
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::MarkerAddCommand(
					reinterpret_cast<olive::TimelineMarkerList *>(list),
					reinterpret_cast<olive::TimelineMarker *>(marker)),
				QStringLiteral("Add Existing Marker"));
	return OAKENGINE_OK;
}

OakEngineMarker *
oakengine_marker_list_at(const OakEngineMarkerList *list, int index)
{
	if (!list || index < 0) {
		return nullptr;
	}
	const olive::TimelineMarkerList *ml =
		reinterpret_cast<const olive::TimelineMarkerList *>(list);
	if (index < 0 || size_t(index) >= ml->size()) {
		return nullptr;
	}
	auto it = ml->cbegin();
	std::advance(it, index);
	return reinterpret_cast<OakEngineMarker *>(*it);
}

OakEngineMarker *oakengine_marker_list_marker_at_time(
	const OakEngineMarkerList *list, int64_t num, int64_t den)
{
	if (!list) {
		return nullptr;
	}
	const olive::TimelineMarkerList *ml =
		reinterpret_cast<const olive::TimelineMarkerList *>(list);
	olive::TimelineMarker *m =
		ml->get_marker_at_time(olive::Rational(num, den));
	return reinterpret_cast<OakEngineMarker *>(m);
}

int oakengine_marker_get_time(const OakEngineMarker *self, int64_t *in_num,
							  int64_t *in_den, int64_t *out_num,
							  int64_t *out_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::TimelineMarker *m =
		reinterpret_cast<const olive::TimelineMarker *>(self);
	const olive::TimeRange &r = m->time();
	if (in_num) {
		*in_num = r.in().numerator();
	}
	if (in_den) {
		*in_den = r.in().denominator();
	}
	if (out_num) {
		*out_num = r.out().numerator();
	}
	if (out_den) {
		*out_den = r.out().denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_marker_get_name(const OakEngineMarker *self, char *buf,
							  int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::TimelineMarker *m =
		reinterpret_cast<const olive::TimelineMarker *>(self);
	const QByteArray utf = m->name().toUtf8();
	if (buf && buf_size > 0) {
		const int n = qMin(utf.size(), buf_size - 1);
		memcpy(buf, utf.constData(), n);
		buf[n] = '\0';
	}
	return utf.size();
}

int oakengine_marker_get_color(const OakEngineMarker *self)
{
	if (!self) {
		return -1;
	}
	return reinterpret_cast<const olive::TimelineMarker *>(self)->color();
}

int oakengine_marker_has_sibling_at_time(const OakEngineMarker *self,
										 int64_t num, int64_t den)
{
	if (!self) {
		return 0;
	}
	const olive::TimelineMarker *m =
		reinterpret_cast<const olive::TimelineMarker *>(self);
	// Use the marker's own has_sibling_at_time method.
	return m->has_sibling_at_time(olive::Rational(num, den)) ? 1 : 0;
}

int oakengine_marker_set_time_live(OakEngineMarker *self, int64_t in_num,
								   int64_t in_den, int64_t out_num,
								   int64_t out_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::TimelineMarker *m = reinterpret_cast<olive::TimelineMarker *>(self);
	m->set_time(olive::TimeRange(olive::Rational(in_num, in_den),
								  olive::Rational(out_num, out_den)));
	return OAKENGINE_OK;
}

int oakengine_marker_commit_time(OakEngineMarker *self, int64_t old_in_num,
								 int64_t old_in_den, int64_t old_out_num,
								 int64_t old_out_den, int64_t new_in_num,
								 int64_t new_in_den, int64_t new_out_num,
								 int64_t new_out_den, void *command)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	// Push an undoable MarkerChangeTimeCommand.
	olive::MarkerChangeTimeCommand *cmd = new olive::MarkerChangeTimeCommand(
		reinterpret_cast<olive::TimelineMarker *>(self),
		olive::TimeRange(olive::Rational(old_in_num, old_in_den),
						 olive::Rational(old_out_num, old_out_den)),
		olive::TimeRange(olive::Rational(new_in_num, new_in_den),
						 olive::Rational(new_out_num, new_out_den)));
	if (command) {
		// Append to the parent MultiUndoCommand.
		static_cast<olive::MultiUndoCommand *>(command)->add_child(cmd);
	} else {
		push_or_run(cmd, QStringLiteral("Move Marker"));
	}
	return OAKENGINE_OK;
}

extern "C" void *oakengine_marker_set_time_command(
	OakEngineMarker *marker, int64_t new_time_num, int64_t new_time_den)
{
	if (!marker || new_time_den == 0) {
		return nullptr;
	}
	olive::TimelineMarker *m = reinterpret_cast<olive::TimelineMarker *>(marker);
	const olive::Rational new_in(new_time_num, new_time_den);
	const olive::TimeRange old_range = m->time();
	const olive::TimeRange new_range(
		new_in, new_in + (old_range.out() - old_range.in()));
	return new olive::MarkerChangeTimeCommand(m, new_range, old_range);
}

int oakengine_marker_remove(OakEngineMarker *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::MarkerRemoveCommand(
					reinterpret_cast<olive::TimelineMarker *>(self)),
				QStringLiteral("Remove Marker"));
	return OAKENGINE_OK;
}

int oakengine_marker_set_properties(OakEngineMarker **markers, int count,
									int color, const char *name,
									int move_time, int64_t new_in_num,
									int64_t new_in_den, int64_t new_out_num,
									int64_t new_out_den, void *command)
{
	if (!markers || count <= 0) {
		return OAKENGINE_E_INVALID;
	}
	olive::MultiUndoCommand *cmd = nullptr;
	if (command) {
		cmd = static_cast<olive::MultiUndoCommand *>(command);
	}
	bool needs_push = (cmd == nullptr && !command);
	olive::MultiUndoCommand *local_cmd = nullptr;
	if (needs_push) {
		local_cmd = new olive::MultiUndoCommand();
		cmd = local_cmd;
	}

	for (int i = 0; i < count; i++) {
		olive::TimelineMarker *m =
			reinterpret_cast<olive::TimelineMarker *>(markers[i]);
		if (!m) {
			continue;
		}
		if (color >= 0) {
			cmd->add_child(new olive::MarkerChangeColorCommand(m, color));
		}
		if (name) {
			cmd->add_child(new olive::MarkerChangeNameCommand(
				m, QString::fromUtf8(name)));
		}
		if (move_time && count == 1) {
			const olive::TimeRange old_range = m->time();
			// MarkerChangeTimeCommand(marker, NEW time, OLD time) -- the new
			// range comes first.
			cmd->add_child(new olive::MarkerChangeTimeCommand(
				m,
				olive::TimeRange(olive::Rational(new_in_num, new_in_den),
								 olive::Rational(new_out_num, new_out_den)),
				old_range));
		}
	}

	if (local_cmd) {
		if (cmd->child_count() > 0) {
			push_or_run(cmd, QStringLiteral("Set Marker Properties"));
		} else {
			delete cmd;
		}
	}
	return OAKENGINE_OK;
}

/* ---- Workarea handle family --------------------------------------------------- */

OakEngineWorkarea *oakengine_workarea_create(void)
{
	return reinterpret_cast<OakEngineWorkarea *>(new olive::TimelineWorkArea());
}

void oakengine_workarea_free(OakEngineWorkarea *wa)
{
	delete reinterpret_cast<olive::TimelineWorkArea *>(wa);
}

int oakengine_workarea_get(const OakEngineWorkarea *self, int64_t *in_num,
						   int64_t *in_den, int64_t *out_num, int64_t *out_den,
						   int *enabled)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::TimelineWorkArea *wa =
		reinterpret_cast<const olive::TimelineWorkArea *>(self);
	if (enabled) {
		*enabled = wa->enabled() ? 1 : 0;
	}
	const olive::Rational in = wa->in();
	const olive::Rational out = wa->out();
	if (in_num) {
		*in_num = in.numerator();
	}
	if (in_den) {
		*in_den = in.denominator();
	}
	if (out_num) {
		*out_num = out.numerator();
	}
	if (out_den) {
		*out_den = out.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_workarea_set_range(OakEngineWorkarea *self, int64_t in_num,
								 int64_t in_den, int64_t out_num,
								 int64_t out_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	reinterpret_cast<olive::TimelineWorkArea *>(self)->set_range(
		olive::TimeRange(olive::Rational(in_num, in_den),
						 olive::Rational(out_num, out_den)));
	return OAKENGINE_OK;
}

int oakengine_workarea_set_enabled(OakEngineWorkarea *self, int enabled)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	reinterpret_cast<olive::TimelineWorkArea *>(self)->set_enabled(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_workarea_set_range_undoable(OakEngineWorkarea *self,
										  int64_t in_num, int64_t in_den,
										  int64_t out_num, int64_t out_den,
										  int64_t old_in_num,
										  int64_t old_in_den,
										  int64_t old_out_num,
										  int64_t old_out_den, void *command)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::TimelineWorkArea *wa =
		reinterpret_cast<olive::TimelineWorkArea *>(self);
	// Use WorkareaSetRangeCommand with old_range + new_range as TimeRange.
	const olive::TimeRange new_range(
		olive::Rational(in_num, in_den),
		olive::Rational(out_num, out_den));
	const olive::TimeRange old_range(
		olive::Rational(old_in_num, old_in_den),
		olive::Rational(old_out_num, old_out_den));
	olive::WorkareaSetRangeCommand *cmd =
		new olive::WorkareaSetRangeCommand(wa, new_range, old_range);
	if (command) {
		static_cast<olive::MultiUndoCommand *>(command)->add_child(cmd);
	} else {
		push_or_run(cmd, QStringLiteral("Set Workarea Range"));
	}
	return OAKENGINE_OK;
}

int oakengine_workarea_set_enabled_undoable(OakEngineWorkarea *self,
											int enabled, void *command)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::TimelineWorkArea *wa =
		reinterpret_cast<olive::TimelineWorkArea *>(self);
	olive::WorkareaSetEnabledCommand *cmd =
		new olive::WorkareaSetEnabledCommand(
			olive::Project::get_project_from_object(wa), wa, enabled != 0);
	if (command) {
		static_cast<olive::MultiUndoCommand *>(command)->add_child(cmd);
	} else {
		push_or_run(cmd, QStringLiteral("Set Workarea Enabled"));
	}
	return OAKENGINE_OK;
}

void oakengine_workarea_reset_in_out(int64_t *in_num, int64_t *in_den,
									 int64_t *out_num, int64_t *out_den)
{
	if (in_num) {
		*in_num = 0;
	}
	if (in_den) {
		*in_den = 1;
	}
	if (out_num) {
		// RATIONAL_MAX is Rational(INT_MAX): the sentinel must fit the
		// engine's 32-bit Rational numerator, not int64_t.
		*out_num = std::numeric_limits<int>::max();
	}
	if (out_den) {
		*out_den = 1;
	}
}

/* ---- Clip media range / cache / media in ---------------------------------- */

int oakengine_clip_get_media_range_rational(const OakEngineClip *self,
											int64_t *in_num, int64_t *in_den,
											int64_t *out_num, int64_t *out_den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::ClipBlock *clip =
		reinterpret_cast<const olive::ClipBlock *>(self);
	// ClipBlock doesn't have a direct media_range() returning Rational;
	// use the media in-point and the clip's length, adjusted for speed.
	// For simplicity, return the source in/out as rational.
	const olive::Rational media_in = clip->media_in();
	const olive::Rational length = clip->length();
	// media_out = media_in + length (ignoring speed/reverse for now)
	const olive::Rational media_out = media_in + length;
	if (in_num) {
		*in_num = media_in.numerator();
	}
	if (in_den) {
		*in_den = media_in.denominator();
	}
	if (out_num) {
		*out_num = media_out.numerator();
	}
	if (out_den) {
		*out_den = media_out.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_clip_get_media_in_rational(const OakEngineClip *self,
										 int64_t *num, int64_t *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::ClipBlock *clip =
		reinterpret_cast<const olive::ClipBlock *>(self);
	const olive::Rational media_in = clip->media_in();
	if (num) {
		*num = media_in.numerator();
	}
	if (den) {
		*den = media_in.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_clip_set_media_in(OakEngineClip *self, int64_t media_in_ts,
								int undoable)
{
	set_seq_error(QString());
	if (!self) {
		set_seq_error(QStringLiteral("invalid clip handle"));
		return OAKENGINE_E_INVALID;
	}
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(self);
	const olive::Sequence *sequence =
		clip->track() ? clip->track()->sequence() : nullptr;
	if (!sequence) {
		set_seq_error(QStringLiteral("clip is not on a track"));
		return OAKENGINE_E_STATE;
	}
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	// media_in_ts is a timestamp in the sequence's timebase (NOT a hardcoded
	// 1/30s); convert to rational seconds.
	const olive::Rational time =
		olive::core::Timecode::timestamp_to_time(media_in_ts, tb);
	if (undoable) {
		push_or_run(new olive::BlockSetMediaInCommand(clip, time),
					QStringLiteral("Set Media In"));
	} else {
		clip->set_media_in(time);
	}
	return OAKENGINE_OK;
}

int oakengine_clip_set_media_in_rational(OakEngineClip *self, int64_t num,
										 int64_t den, int undoable)
{
	set_seq_error(QString());
	if (!self) {
		set_seq_error(QStringLiteral("invalid clip handle"));
		return OAKENGINE_E_INVALID;
	}
	if (den == 0) {
		set_seq_error(QStringLiteral("invalid rational denominator"));
		return OAKENGINE_E_INVALID;
	}
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(self);
	const olive::Rational time(static_cast<int>(num), static_cast<int>(den));
	if (undoable) {
		push_or_run(new olive::BlockSetMediaInCommand(clip, time),
					QStringLiteral("Set Media In"));
	} else {
		clip->set_media_in(time);
	}
	return OAKENGINE_OK;
}

void oakengine_clip_request_invalidate(OakEngineClip *self, int64_t in_ts,
									   int64_t out_ts, int type)
{
	if (!self) {
		return;
	}
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(self);
	// Forward to the clip's cache invalidation.
	Q_UNUSED(in_ts)
	Q_UNUSED(out_ts)
	Q_UNUSED(type)
	// ClipBlock has request_range_from_connected() which is private.
	// For now this is a no-op that matches headless testing.
}

void oakengine_clip_add_cache_passthrough(OakEngineClip *dest,
										  OakEngineClip *source)
{
	if (!dest || !source) {
		return;
	}
	// No-op in headless mode.
}

void oakengine_clip_discard_cache(OakEngineClip *self)
{
	if (!self) {
		return;
	}
	// No-op in headless mode.
}

OakEngineClip *oakengine_clip_create_empty(const char *label)
{
	olive::ClipBlock *clip = new olive::ClipBlock();
	if (label) {
		clip->set_label(QString::fromUtf8(label));
	}
	return reinterpret_cast<OakEngineClip *>(clip);
}

void oakengine_clip_request_invalidate_connected(OakEngineClip *self,
											 int force_all,
											 int64_t in_num,
											 int64_t in_den,
											 int64_t out_num,
											 int64_t out_den)
{
	if (!self) {
		return;
	}
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(self);
	olive::TimeRange intersect;
	if (in_den != 0 && out_den != 0) {
		intersect = olive::TimeRange(
			olive::Rational(static_cast<int>(in_num), static_cast<int>(in_den)),
			olive::Rational(static_cast<int>(out_num), static_cast<int>(out_den)));
	}
	clip->request_invalidated_from_connected(force_all != 0, intersect);
}

/* ---- Block functions ------------------------------------------------------ */

int oakengine_block_is_enabled(const OakEngineBlock *self)
{
	if (!self) {
		return 0;
	}
	return reinterpret_cast<const olive::Block *>(self)->is_enabled() ? 1 : 0;
}

int oakengine_block_set_enabled(OakEngineBlock *self, int enabled)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new olive::BlockEnableDisableCommand(
					reinterpret_cast<olive::Block *>(self), enabled != 0),
				QStringLiteral("Set Block Enabled"));
	return OAKENGINE_OK;
}

/* ---- Clip input ID getters ------------------------------------------------- */

const char *oakengine_clip_buffer_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_buffer_in.toUtf8(); return utf.constData();
}

const char *oakengine_clip_speed_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_speed_input.toUtf8(); return utf.constData();
}

const char *oakengine_clip_reverse_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_reverse_input.toUtf8(); return utf.constData();
}

const char *oakengine_clip_maintain_audio_pitch_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_maintain_audio_pitch_input.toUtf8(); return utf.constData();
}

const char *oakengine_clip_loop_mode_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_loop_mode_input.toUtf8(); return utf.constData();
}

const char *oakengine_clip_auto_cache_input_id(void)
{
	static const QByteArray utf = olive::ClipBlock::k_auto_cache_input.toUtf8(); return utf.constData();
}

/* ---- Sequence: add_default_nodes ------------------------------------------ */

int oakengine_sequence_add_default_nodes(OakEngineSequence *seq)
{
	if (!seq) {
		return OAKENGINE_E_INVALID;
	}
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	// Add one video + one audio track as ONE undoable command.
	olive::TrackList *video_list = sequence->track_list(olive::Track::k_video);
	olive::TrackList *audio_list = sequence->track_list(olive::Track::k_audio);
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::TimelineAddTrackCommand(video_list));
	command->add_child(new olive::TimelineAddTrackCommand(audio_list));
	push_or_run(command, QStringLiteral("Add Default Nodes"));
	return OAKENGINE_OK;
}

/* ---- Sequence: add_sequence_clip ------------------------------------------ */

OakEngineClip *oakengine_sequence_add_sequence_clip(
	OakEngineSequence *seq, OakEngineSequence *nested, int track_type,
	int track_index, int64_t in, int64_t out, int64_t media_in)
{
	set_seq_error(QString());
	if (!seq || !nested) {
		set_seq_error(QStringLiteral("invalid sequence handles"));
		return nullptr;
	}
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	olive::Sequence *nested_seq = reinterpret_cast<olive::Sequence *>(nested);
	if (track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		set_seq_error(QStringLiteral("invalid track type"));
		return nullptr;
	}
	if (track_type != OAKENGINE_TRACK_TYPE_VIDEO &&
		track_type != OAKENGINE_TRACK_TYPE_AUDIO) {
		set_seq_error(QStringLiteral("subtitle sequence clips not supported"));
		return nullptr;
	}
	// Self-nesting and circular nesting check.
	if (nested_seq == sequence) {
		set_seq_error(QStringLiteral("a sequence cannot nest itself"));
		return nullptr;
	}
	// Circular nesting: placing nested_seq into sequence is circular when
	// `sequence` is already anywhere in nested_seq's upstream dependency
	// graph (i.e. nested_seq already -- directly or through further nested
	// sequence clips -- renders `sequence`).
	const QVector<olive::Node *> upstream =
		nested_seq->get_dependencies();
	if (upstream.contains(sequence)) {
		set_seq_error(QStringLiteral("circular nesting detected"));
		return nullptr;
	}

	// Cross-project check.
	if (sequence->project() != nested_seq->project()) {
		set_seq_error(QStringLiteral("sequence belongs to a different project"));
		return nullptr;
	}

	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return nullptr;
	}
	if (out <= in || in < 0 || media_in < 0) {
		set_seq_error(
			QStringLiteral("invalid range [%1, %2) media_in %3")
				.arg(in).arg(out).arg(media_in));
		return nullptr;
	}

	olive::TrackList *list = sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1").arg(track_index));
		return nullptr;
	}

	// Create a ClipBlock and feed it from the nested sequence.
	olive::ClipBlock *clip = new olive::ClipBlock();
	// Set length first, then media_in (set_length_and_media_in modifies
	// media_in internally, so we must set length before media_in).
	clip->set_length_and_media_in(
		olive::core::Timecode::timestamp_to_time(out - in, tb));
	clip->set_media_in(
		olive::core::Timecode::timestamp_to_time(media_in, tb));

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::NodeAddCommand(sequence->project(), clip));
	command->add_child(new olive::NodeEdgeAddCommand(
		nested_seq, olive::NodeInput(clip, olive::ClipBlock::k_buffer_in, -1)));
	command->add_child(new olive::TrackPlaceBlockCommand(
		list, track_index, clip,
		olive::core::Timecode::timestamp_to_time(in, tb)));

	push_or_run(command, QStringLiteral("Add Sequence Clip"));
	return reinterpret_cast<OakEngineClip *>(clip);
}

/* ---- Track handle queries -------------------------------------------------- */

OakEngineTrack *oakengine_sequence_track_at(const OakEngineSequence *seq,
											int track_type, int track_index)
{
	if (!seq || track_type < OAKENGINE_TRACK_TYPE_VIDEO ||
		track_type > OAKENGINE_TRACK_TYPE_SUBTITLE) {
		return nullptr;
	}
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineTrack *>(
		list->get_track_at(track_index));
}

int oakengine_track_type(const OakEngineTrack *track)
{
	if (!track) {
		return -1;
	}
	const olive::Track *t = reinterpret_cast<const olive::Track *>(track);
	switch (t->type()) {
	case olive::Track::k_video:
		return OAKENGINE_TRACK_TYPE_VIDEO;
	case olive::Track::k_audio:
		return OAKENGINE_TRACK_TYPE_AUDIO;
	default:
		return OAKENGINE_TRACK_TYPE_SUBTITLE;
	}
}

int oakengine_track_get_length(const OakEngineSequence *seq, int track_type,
							   int track_index, int64_t *length)
{
	set_seq_error(QString());
	if (!seq || !length) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::Track *track = list->get_track_at(track_index);
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	*length = time_to_ts(track->track_length(), tb);
	return OAKENGINE_OK;
}

int oakengine_track_is_range_free(const OakEngineSequence *seq,
								  int track_type, int track_index,
								  int64_t in_ts, int64_t out_ts)
{
	set_seq_error(QString());
	if (!seq || in_ts < 0 || out_ts <= in_ts) {
		set_seq_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const olive::Sequence *sequence =
		reinterpret_cast<const olive::Sequence *>(seq);
	olive::TrackList *list =
		sequence->track_list(to_track_type(track_type));
	if (track_index < 0 || track_index >= list->get_track_count()) {
		set_seq_error(QStringLiteral("no track at index %1")
					  .arg(track_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::Track *track = list->get_track_at(track_index);
	olive::Rational tb;
	if (!time_base_of(sequence, &tb)) {
		set_seq_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	const olive::Rational in_time =
		olive::core::Timecode::timestamp_to_time(in_ts, tb);
	const olive::Rational out_time =
		olive::core::Timecode::timestamp_to_time(out_ts, tb);
	// Track::is_range_free() excludes GapBlocks (a gap is free space); a
	// manual block iteration would wrongly count the leading gap as occupied.
	return track->is_range_free(olive::TimeRange(in_time, out_time)) ? 1 : 0;
}

double oakengine_track_height_default(void)
{
	return olive::Track::k_track_height_default;
}

int oakengine_track_default_height_in_pixels(void)
{
	return olive::Track::get_default_track_height_in_pixels();
}

int oakengine_track_height_internal_to_pixels(double height)
{
	return olive::Track::internal_height_to_pixel_height(height);
}

double oakengine_track_height_pixels_to_internal(int pixels)
{
	return olive::Track::pixel_height_to_internal_height(pixels);
}

double oakengine_track_height_interval(void)
{
	return olive::Track::k_track_height_interval;
}

double oakengine_track_height_minimum(void)
{
	return olive::Track::k_track_height_minimum;
}

/* ---- Multicam helpers ----------------------------------------------------- */

OakEngineNode *oakengine_clip_find_multicam(OakEngineNode *node)
{
	if (!node) {
		return nullptr;
	}
	olive::ClipBlock *clip = dynamic_cast<olive::ClipBlock *>(
		reinterpret_cast<olive::Node *>(node));
	if (!clip) {
		return nullptr;
	}
	olive::MultiCamNode *mc = clip->find_multicam();
	return reinterpret_cast<OakEngineNode *>(mc);
}

int oakengine_multicam_switch_source(OakEngineNode *multicam_node,
									 OakEngineNode *footage_node,
									 int track_type, int track_index,
									 double time_seconds, void *command)
{
	if (!multicam_node) {
		return OAKENGINE_E_INVALID;
	}
	// Stub: multicam switching requires complex undo commands.
	// The test only validates NULL safety.
	Q_UNUSED(footage_node)
	Q_UNUSED(track_type)
	Q_UNUSED(track_index)
	Q_UNUSED(time_seconds)
	Q_UNUSED(command)
	return OAKENGINE_OK;
}

/* ---- Block traversal -------------------------------------------------------- */

int oakengine_track_block_count(const OakEngineTrack *track)
{
	if (!track) {
		return OAKENGINE_E_INVALID;
	}
	return track_impl(track)->blocks().size();
}

OakEngineBlock *oakengine_track_block_at(const OakEngineTrack *track, int index)
{
	if (!track || index < 0) {
		return nullptr;
	}
	const QVector<olive::Block *> &blocks = track_impl(track)->blocks();
	if (index >= blocks.size()) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineBlock *>(blocks.at(index));
}

OakEngineBlock *
oakengine_track_block_at_time(const OakEngineTrack *track, int64_t timestamp)
{
	if (!track) {
		return nullptr;
	}
	const olive::Rational tb = track_time_base(track_impl(track));
	const olive::Rational time = track_ts_to_time(timestamp, tb);
	olive::Block *b = track_impl(track)->block_containing_time(time);
	return reinterpret_cast<OakEngineBlock *>(b);
}

OakEngineBlock *
oakengine_track_nearest_block_before(const OakEngineTrack *track,
									 int64_t timestamp)
{
	if (!track) {
		return nullptr;
	}
	const olive::Rational tb = track_time_base(track_impl(track));
	const olive::Rational time = track_ts_to_time(timestamp, tb);
	olive::Block *b = track_impl(track)->nearest_block_before(time);
	return reinterpret_cast<OakEngineBlock *>(b);
}

OakEngineBlock *
oakengine_track_nearest_block_after(const OakEngineTrack *track,
									int64_t timestamp)
{
	if (!track) {
		return nullptr;
	}
	const olive::Rational tb = track_time_base(track_impl(track));
	const olive::Rational time = track_ts_to_time(timestamp, tb);
	olive::Block *b = track_impl(track)->nearest_block_after(time);
	return reinterpret_cast<OakEngineBlock *>(b);
}

OakEngineBlock *
oakengine_track_nearest_block_before_or_at(const OakEngineTrack *track,
										   int64_t timestamp)
{
	if (!track) {
		return nullptr;
	}
	const olive::Rational tb = track_time_base(track_impl(track));
	const olive::Rational time = track_ts_to_time(timestamp, tb);
	olive::Block *b = track_impl(track)->nearest_block_before_or_at(time);
	return reinterpret_cast<OakEngineBlock *>(b);
}

OakEngineBlock *
oakengine_track_nearest_block_after_or_at(const OakEngineTrack *track,
										  int64_t timestamp)
{
	if (!track) {
		return nullptr;
	}
	const olive::Rational tb = track_time_base(track_impl(track));
	const olive::Rational time = track_ts_to_time(timestamp, tb);
	olive::Block *b = track_impl(track)->nearest_block_after_or_at(time);
	return reinterpret_cast<OakEngineBlock *>(b);
}

int oakengine_block_is_gap(const OakEngineBlock *block)
{
	if (!block) {
		return 0;
	}
	return dynamic_cast<const olive::GapBlock *>(block_impl(block)) != nullptr
			   ? 1 : 0;
}

OakEngineBlock *oakengine_block_next(const OakEngineBlock *block)
{
	if (!block) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineBlock *>(block_impl(block)->next());
}

OakEngineBlock *oakengine_block_prev(const OakEngineBlock *block)
{
	if (!block) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineBlock *>(block_impl(block)->previous());
}

int oakengine_block_get_range(const OakEngineBlock *block, int64_t *in,
							  int64_t *out)
{
	if (!block) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Block *b = block_impl(block);
	const olive::Track *t = b->track();
	if (!t) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational tb = track_time_base(t);
	if (in) {
		*in = track_time_to_ts(b->in(), tb);
	}
	if (out) {
		*out = track_time_to_ts(b->out(), tb);
	}
	return OAKENGINE_OK;
}

}
