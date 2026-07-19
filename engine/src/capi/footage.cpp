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

#include "oakengine/footage.h"

#include <cstdio>

#include <QByteArray>
#include <QFileInfo>
#include <QString>

#include "codec/decoder.h"
#include "coreengine.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"

namespace
{

// Handle payload: a probe result (owned description) or a project footage
// node (borrowed). See oakengine/footage.h for the two models.
struct OakEngineFootageState {
	// true: `node` is owned by a project and must not be deleted here.
	bool borrowed = false;
	olive::FootageDescription description;
	olive::Footage *node = nullptr;
	QString filename; // media path (probe model; the node has its own)
};

// Last probe/import error per thread (handles are NULL on failure, so the
// reason cannot hang off them like in the renderer family).
thread_local QString g_last_error;

void set_error(const QString &error)
{
	g_last_error = error;
}

OakEngineFootageState *impl(OakEngineFootage *h)
{
	return reinterpret_cast<OakEngineFootageState *>(h);
}

const OakEngineFootageState *impl(const OakEngineFootage *h)
{
	return reinterpret_cast<const OakEngineFootageState *>(h);
}

OakEngineFootage *wrap(OakEngineFootageState *s)
{
	return reinterpret_cast<OakEngineFootage *>(s);
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

// Unified stream access across the two handle models.
int video_stream_count(const OakEngineFootageState *s)
{
	return s->node ? s->node->get_video_stream_count() :
					 s->description.get_video_streams().size();
}

int audio_stream_count(const OakEngineFootageState *s)
{
	return s->node ? s->node->get_audio_stream_count() :
					 s->description.get_audio_streams().size();
}

int subtitle_stream_count(const OakEngineFootageState *s)
{
	return s->node ? s->node->get_subtitle_stream_count() :
					 s->description.get_subtitle_streams().size();
}

olive::VideoParams video_stream_at(const OakEngineFootageState *s, int index)
{
	if (index < 0 || index >= video_stream_count(s)) {
		return olive::VideoParams();
	}
	return s->node ? s->node->get_video_params(index) :
					 s->description.get_video_streams().at(index);
}

olive::AudioParams audio_stream_at(const OakEngineFootageState *s, int index)
{
	if (index < 0 || index >= audio_stream_count(s)) {
		return olive::AudioParams();
	}
	return s->node ? s->node->get_audio_params(index) :
					 s->description.get_audio_streams().at(index);
}

QString filename_of(const OakEngineFootageState *s)
{
	return s->node ? s->node->filename() : s->filename;
}

} // namespace

// Internal cross-family accessor (not part of the public C ABI): returns
// the borrowed project node of an import handle, or nullptr for probe
// handles and NULL. Used by the timeline editing primitives.
extern "C" __attribute__((visibility("hidden"))) void *
oakengine_capi_footage_node(OakEngineFootage *h)
{
	if (!h) {
		return nullptr;
	}
	const OakEngineFootageState *s = impl(h);
	return s->node;
}

extern "C"
{

OakEngineFootage *oakengine_footage_probe(const char *path)
{
	set_error(QString());
	if (!path || !QFileInfo::exists(QString::fromUtf8(path))) {
		set_error(QStringLiteral("file does not exist: %1")
					  .arg(path ? path : "(null)"));
		return nullptr;
	}

	olive::DecoderPtr decoder =
		olive::Decoder::create_from_id(QStringLiteral("ffmpeg"));
	if (!decoder) {
		set_error(QStringLiteral("ffmpeg decoder is not available"));
		return nullptr;
	}

	olive::FootageDescription description =
		decoder->probe(QString::fromUtf8(path), nullptr);
	if (!description.is_valid()) {
		set_error(QStringLiteral("failed to probe \"%1\": unsupported or "
								 "unreadable media file")
					  .arg(path));
		return nullptr;
	}

	auto *state = new OakEngineFootageState();
	state->description = description;
	state->filename = QString::fromUtf8(path);
	return wrap(state);
}

void oakengine_footage_free(OakEngineFootage *self)
{
	// Only the wrapper is deleted; a borrowed node stays with its project.
	delete impl(self);
}

int oakengine_footage_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

int oakengine_footage_get_decoder_name(OakEngineFootage *self, char *buf,
									   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	return string_to_buf(s->node ? s->node->decoder() :
								   s->description.decoder(),
						 buf, buf_size);
}

int oakengine_footage_get_video_stream_count(const OakEngineFootage *self)
{
	return self ? video_stream_count(impl(self)) : 0;
}

int oakengine_footage_get_audio_stream_count(const OakEngineFootage *self)
{
	return self ? audio_stream_count(impl(self)) : 0;
}

int oakengine_footage_get_subtitle_stream_count(const OakEngineFootage *self)
{
	return self ? subtitle_stream_count(impl(self)) : 0;
}

int oakengine_footage_get_video_stream_info(OakEngineFootage *self, int index,
											oak_footage_video_info *out)
{
	if (!self || !out) {
		return OAKENGINE_E_INVALID;
	}
	const olive::VideoParams vp = video_stream_at(impl(self), index);
	if (!vp.is_valid()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::Rational frame_rate = vp.frame_rate();
	const olive::Rational time_base = vp.time_base();
	out->stream_index = vp.stream_index();
	out->width = vp.width();
	out->height = vp.height();
	out->frame_rate_num = frame_rate.numerator();
	out->frame_rate_den = frame_rate.denominator();
	out->duration_ts = vp.duration();
	out->time_base_num = time_base.numerator();
	out->time_base_den = time_base.denominator();
	out->color_primaries = vp.color_primaries();
	out->color_trc = vp.color_transfer();
	out->interlaced =
		vp.interlacing() != olive::VideoParams::k_interlace_none ? 1 : 0;
	return OAKENGINE_OK;
}

int oakengine_footage_get_audio_stream_info(OakEngineFootage *self, int index,
											oak_footage_audio_info *out)
{
	if (!self || !out) {
		return OAKENGINE_E_INVALID;
	}
	const olive::AudioParams ap = audio_stream_at(impl(self), index);
	if (ap.sample_rate() <= 0) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::Rational time_base = ap.time_base();
	out->stream_index = ap.stream_index();
	out->sample_rate = ap.sample_rate();
	out->channel_layout = ap.channel_layout();
	out->channel_count = ap.channel_count();
	out->duration_ts = ap.duration();
	out->time_base_num = time_base.numerator();
	out->time_base_den = time_base.denominator();
	return OAKENGINE_OK;
}

int oakengine_footage_get_duration(OakEngineFootage *self, double *seconds)
{
	if (!self || !seconds) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	double longest = 0.0;
	for (int i = 0; i < video_stream_count(s); i++) {
		const olive::VideoParams vp = video_stream_at(s, i);
		longest = qMax(longest, vp.duration() * vp.time_base().to_double());
	}
	for (int i = 0; i < audio_stream_count(s); i++) {
		const olive::AudioParams ap = audio_stream_at(s, i);
		longest = qMax(longest, ap.duration() * ap.time_base().to_double());
	}
	*seconds = longest;
	return OAKENGINE_OK;
}

int oakengine_footage_is_online(OakEngineFootage *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return QFileInfo::exists(filename_of(impl(self))) ? 1 : 0;
}

int oakengine_footage_get_source_start_time(OakEngineFootage *self, int *num,
											int *den)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	const bool has = s->node ? s->node->has_source_start_time() :
							   s->description.has_source_start_time();
	if (!has) {
		return 0;
	}
	const olive::Rational t = s->node ? s->node->source_start_time() :
										s->description.source_start_time();
	if (num) {
		*num = t.numerator();
	}
	if (den) {
		*den = t.denominator();
	}
	return 1;
}

OakEngineFootage *oakengine_project_import_footage(OakEngineProject *project,
												   const char *path)
{
	set_error(QString());
	olive::Project *p = reinterpret_cast<olive::Project *>(project);
	if (!p || !p->root() || !path) {
		set_error(QStringLiteral("invalid project or path"));
		return nullptr;
	}

	const QFileInfo file_info(QString::fromUtf8(path));
	if (!file_info.exists()) {
		set_error(QStringLiteral("file does not exist: %1").arg(path));
		return nullptr;
	}

	// Non-UI core of ProjectImportTask: assigning the filename probes the
	// media (Footage::reprobe()); invalid media is rejected. Numbered stills
	// are imported as single frames -- there is no image-sequence
	// confirmation handler behind this facade.
	auto *footage = new olive::Footage();
	footage->set_filename(file_info.absoluteFilePath());
	footage->set_label(file_info.fileName());
	if (!footage->is_valid()) {
		set_error(QStringLiteral("failed to probe \"%1\": unsupported or "
								 "unreadable media file")
					  .arg(path));
		delete footage;
		return nullptr;
	}

	olive::MultiUndoCommand *command = new olive::MultiUndoCommand();
	command->add_child(new olive::NodeAddCommand(p, footage));
	command->add_child(new olive::FolderAddChild(p->root(), footage));

	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->push(
			command, QStringLiteral("Import Footage"));
	} else {
		command->redo_now();
		delete command;
	}

	auto *state = new OakEngineFootageState();
	state->borrowed = true;
	state->node = footage;
	return wrap(state);
}

} // extern "C"
