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
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineworkarea.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"

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

	// Same undoable creation as the application's "Create New Sequence"
	// action (app/core.cpp), minus opening a viewer. Without an EngineCore
	// (library not initialized) the command is executed non-undoably.
	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::NodeAddCommand(p, sequence));
	command->add_child(new olive::FolderAddChild(p->root(), sequence));

	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->push(
			command, QStringLiteral("Create Sequence"));
	} else {
		command->redo_now();
		delete command;
	}

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
								 int64_t *time, char *name, int name_size)
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
	return OAKENGINE_OK;
}

} // extern "C"
