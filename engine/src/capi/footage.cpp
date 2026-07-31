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
#include "oakengine/timeline.h"

#include <atomic>
#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QThread>

#include "codec/decoder.h"
#include "codec/proxymanager.h"
#include "coreengine.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"
#include "undointernal.h"

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

// The footage node of a borrowed handle, or nullptr (probe handles have no
// node). Shared validation for the media-management section; sets the
// error string.
olive::Footage *borrowed_node(OakEngineFootage *self)
{
	if (!self) {
		set_error(QStringLiteral("invalid footage handle"));
		return nullptr;
	}
	olive::Footage *node = impl(self)->node;
	if (!node) {
		set_error(QStringLiteral(
			"probe handles carry no project node; import the media first"));
		return nullptr;
	}
	return node;
}

// Push an undoable command onto the global undo stack when the engine is
// initialized, otherwise execute it directly.
void push_or_run(olive::UndoCommand *command, const QString &name)
{
	oakengine_undo_push_or_run(command, name);
}

// Undo commands for footage stream overrides. The engine has no undo
// commands for these (the application's footage properties dialog carries
// them at the app layer), so the facade carries read-modify-write
// equivalents with the same semantics.
class FootageVideoParamsCommand : public olive::UndoCommand {
public:
	FootageVideoParamsCommand(olive::Footage *footage, int index,
							  const olive::VideoParams &params)
		: footage_(footage)
		, index_(index)
		, new_params_(params)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return footage_->project();
	}

protected:
	virtual void redo() override
	{
		old_params_ = footage_->get_video_params(index_);
		footage_->set_video_params(new_params_, index_);
	}

	virtual void undo() override
	{
		footage_->set_video_params(old_params_, index_);
	}

private:
	olive::Footage *footage_;
	int index_;
	olive::VideoParams old_params_;
	olive::VideoParams new_params_;
};

class FootageAudioParamsCommand : public olive::UndoCommand {
public:
	FootageAudioParamsCommand(olive::Footage *footage, int index,
							  const olive::AudioParams &params)
		: footage_(footage)
		, index_(index)
		, new_params_(params)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return footage_->project();
	}

protected:
	virtual void redo() override
	{
		old_params_ = footage_->get_audio_params(index_);
		footage_->set_audio_params(new_params_, index_);
	}

	virtual void undo() override
	{
		footage_->set_audio_params(old_params_, index_);
	}

private:
	olive::Footage *footage_;
	int index_;
	olive::AudioParams old_params_;
	olive::AudioParams new_params_;
};

class FootageSubtitleParamsCommand : public olive::UndoCommand {
public:
	FootageSubtitleParamsCommand(olive::Footage *footage, int index,
								 const olive::SubtitleParams &params)
		: footage_(footage)
		, index_(index)
		, new_params_(params)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return footage_->project();
	}

protected:
	virtual void redo() override
	{
		old_params_ = footage_->get_subtitle_params(index_);
		footage_->set_subtitle_params(new_params_, index_);
	}

	virtual void undo() override
	{
		footage_->set_subtitle_params(old_params_, index_);
	}

private:
	olive::Footage *footage_;
	int index_;
	olive::SubtitleParams old_params_;
	olive::SubtitleParams new_params_;
};

class FootageSourceStartTimeCommand : public olive::UndoCommand {
public:
	FootageSourceStartTimeCommand(olive::Footage *footage, bool enabled,
								  const olive::Rational &time)
		: footage_(footage)
		, new_enabled_(enabled)
		, new_time_(time)
	{
	}

	virtual olive::Project *get_relevant_project() const override
	{
		return footage_->project();
	}

protected:
	virtual void redo() override
	{
		old_enabled_ = footage_->has_source_start_time();
		old_time_ = footage_->source_start_time();
		old_source_ = footage_->source_start_time_source();

		if (new_enabled_) {
			footage_->set_source_start_time(new_time_,
											QStringLiteral("manual"));
		} else {
			footage_->clear_source_start_time();
		}
	}

	virtual void undo() override
	{
		if (old_enabled_) {
			footage_->set_source_start_time(old_time_, old_source_);
		} else {
			footage_->clear_source_start_time();
		}
	}

private:
	olive::Footage *footage_;
	bool new_enabled_;
	olive::Rational new_time_;
	bool old_enabled_ = false;
	olive::Rational old_time_;
	QString old_source_;
};

// Stream access by facade track type; returns false for unknown indexes.
bool video_stream_at(const olive::Footage *f, int index,
					 olive::VideoParams *out)
{
	if (index < 0 || index >= f->get_video_stream_count()) {
		return false;
	}
	*out = f->get_video_params(index);
	return true;
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

	oakengine_undo_push_or_run(command, QStringLiteral("Import Footage"));

	auto *state = new OakEngineFootageState();
	state->borrowed = true;
	state->node = footage;
	return wrap(state);
}

OakEngineFootage *oakengine_footage_borrow(OakEngineNode *node)
{
	set_error(QString());
	auto *footage =
		dynamic_cast<olive::Footage *>(reinterpret_cast<olive::Node *>(node));
	if (!footage) {
		set_error(QStringLiteral("node is not a footage node"));
		return nullptr;
	}
	auto *state = new OakEngineFootageState();
	state->borrowed = true;
	state->node = footage;
	return wrap(state);
}

int oakengine_footage_is_valid(const OakEngineNode *node)
{
	if (!node) {
		return 0;
	}
	const auto *footage = dynamic_cast<const olive::Footage *>(
		reinterpret_cast<const olive::Node *>(node));
	return footage && footage->is_valid() ? 1 : 0;
}

int oakengine_footage_relink(OakEngineFootage *footage, const char *new_path)
{
	set_error(QString());
	if (!new_path) {
		set_error(QStringLiteral("invalid path"));
		return OAKENGINE_E_INVALID;
	}
	olive::Footage *node = borrowed_node(footage);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	const QString path = QString::fromUtf8(new_path);
	if (!QFileInfo::exists(path)) {
		set_error(QStringLiteral("file does not exist: %1").arg(path));
		return OAKENGINE_E_NOT_FOUND;
	}

	// Same as the application's relink action: set_filename() triggers
	// clear() (streams, decoder and proxy state reset) and a reprobe. The
	// label is intentionally left alone (relinking changes the path, not
	// the user's naming).
	node->set_filename(path);
	if (!node->is_valid()) {
		set_error(QStringLiteral("failed to probe \"%1\" as media")
					  .arg(path));
		return OAKENGINE_E_FAILED;
	}
	return OAKENGINE_OK;
}

int oakengine_project_find_offline_footage(OakEngineProject *project,
										   const char *search_dir)
{
	set_error(QString());
	olive::Project *p = reinterpret_cast<olive::Project *>(project);
	if (!p || !search_dir) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	const QDir dir(QString::fromUtf8(search_dir));
	if (!dir.exists()) {
		set_error(QStringLiteral("directory does not exist: %1")
					  .arg(search_dir));
		return OAKENGINE_E_INVALID;
	}

	int relinked = 0;
	for (olive::Node *n : p->nodes()) {
		olive::Footage *footage = dynamic_cast<olive::Footage *>(n);
		if (!footage) {
			continue;
		}
		const QString filename = footage->filename();
		if (filename.isEmpty() || QFileInfo::exists(filename)) {
			continue;
		}
		// Exact file-name match in the search directory (no recursion). The
		// label is intentionally left alone.
		const QString candidate =
			dir.filePath(QFileInfo(filename).fileName());
		if (QFileInfo::exists(candidate)) {
			footage->set_filename(candidate);
			if (footage->is_valid()) {
				relinked++;
			}
		}
	}
	return relinked;
}

int oakengine_footage_proxy_get_state(OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	return int(impl(self)->node->proxy_state());
}

int oakengine_footage_proxy_generate(OakEngineFootage *self)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	if (!olive::ProxyManager::instance()) {
		set_error(QStringLiteral("engine not initialized"));
		return OAKENGINE_E_STATE;
	}
	olive::Project *project = node->project();
	if (!project) {
		set_error(QStringLiteral("footage is not part of a project"));
		return OAKENGINE_E_INVALID;
	}
	const QString filename = node->filename();
	if (!QFileInfo::exists(filename)) {
		set_error(QStringLiteral("source file does not exist: %1")
					  .arg(filename));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (node->get_video_stream_count() < 1) {
		set_error(QStringLiteral("footage has no video stream to proxy"));
		return OAKENGINE_E_INVALID;
	}

	const int stream_index = node->get_video_params(0).stream_index();
	const olive::ProxyManager::ProxyParams params =
		olive::ProxyManager::proxy_params_from_config();

	// Same assembly as the application's proxy dialog.
	olive::ProxyManager::Proxy proxy =
		olive::ProxyManager::instance()->get_or_start_proxy(
			project->cache_path(), filename, stream_index, params);
	node->set_proxy(proxy.filename, proxy.state, stream_index,
					params.version, true);
	node->invalidate_all(olive::Footage::k_filename_input);

	if (proxy.state == olive::ProxyManager::k_proxy_ready) {
		return OAKENGINE_OK;
	}

	// Wait for completion with an event loop, like the export family: the
	// proxy task finishes on the TaskManager thread and its completion
	// signal is queued back to this thread (up to 120 s).
	QElapsedTimer timer;
	timer.start();
	while (node->proxy_state() == olive::ProxyManager::k_proxy_generating &&
		   !timer.hasExpired(120000)) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(5);
	}

	if (node->proxy_state() == olive::ProxyManager::k_proxy_ready) {
		return OAKENGINE_OK;
	}
	if (node->proxy_state() == olive::ProxyManager::k_proxy_failed) {
		set_error(QStringLiteral("proxy generation failed for \"%1\"")
					  .arg(filename));
		return OAKENGINE_E_FAILED;
	}
	set_error(QStringLiteral("proxy generation timed out for \"%1\"")
				  .arg(filename));
	return OAKENGINE_E_FAILED;
}

int oakengine_footage_proxy_delete(OakEngineFootage *self)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	// Mirrors the application's proxy dialog: remove the finished and the
	// in-progress working file, then reset the proxy state.
	if (!node->proxy_path().isEmpty()) {
		QFile::remove(node->proxy_path());
		QFile::remove(
			olive::ProxyManager::get_working_proxy_filename(
				node->proxy_path()));
	}
	node->clear_proxy();
	node->invalidate_all(olive::Footage::k_filename_input);
	return OAKENGINE_OK;
}

int oakengine_footage_proxy_is_enabled(OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	return impl(self)->node->proxy_enabled() ? 1 : 0;
}

int oakengine_footage_proxy_set_enabled(OakEngineFootage *self, int enabled)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	node->set_proxy_enabled(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_footage_proxy_get_path(OakEngineFootage *self, char *buf,
									 int buf_size)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->node->proxy_path(), buf, buf_size);
}

/* ---- Stream parameter overrides --------------------------------------------- */

int oakengine_footage_get_video_stream_overrides(
	OakEngineFootage *self, int stream_index, char *colorspace_buf,
	int colorspace_size, int *color_range, int *interlacing,
	int *premultiplied)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (colorspace_buf) {
		string_to_buf(vp.colorspace(), colorspace_buf, colorspace_size);
	}
	if (color_range) {
		*color_range = int(vp.color_range());
	}
	if (interlacing) {
		*interlacing = int(vp.interlacing());
	}
	if (premultiplied) {
		*premultiplied = vp.premultiplied_alpha() ? 1 : 0;
	}
	return OAKENGINE_OK;
}

int oakengine_footage_set_video_stream_overrides(
	OakEngineFootage *self, int stream_index, const char *colorspace,
	int color_range, int interlacing, int premultiplied)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (colorspace) {
		vp.set_colorspace(QString::fromUtf8(colorspace));
	}
	if (color_range >= 0) {
		vp.set_color_range(
			static_cast<olive::VideoParams::ColorRange>(color_range));
	}
	if (interlacing >= 0) {
		vp.set_interlacing(
			static_cast<olive::VideoParams::Interlacing>(interlacing));
	}
	if (premultiplied >= 0) {
		vp.set_premultiplied_alpha(premultiplied != 0);
	}
	push_or_run(new FootageVideoParamsCommand(node, stream_index, vp),
				QStringLiteral("Set Video Stream Overrides"));
	return OAKENGINE_OK;
}

int oakengine_footage_get_pixel_aspect(OakEngineFootage *self,
									   int stream_index, int *num, int *den)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (num) {
		*num = vp.pixel_aspect_ratio().numerator();
	}
	if (den) {
		*den = vp.pixel_aspect_ratio().denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_footage_set_pixel_aspect(OakEngineFootage *self,
									   int stream_index, int num, int den)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	if (num <= 0 || den <= 0) {
		set_error(QStringLiteral("invalid pixel aspect ratio %1/%2")
					  .arg(num)
					  .arg(den));
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	vp.set_pixel_aspect_ratio(olive::Rational(num, den));
	push_or_run(new FootageVideoParamsCommand(node, stream_index, vp),
				QStringLiteral("Set Pixel Aspect Ratio"));
	return OAKENGINE_OK;
}

int oakengine_footage_get_image_sequence_params(
	OakEngineFootage *self, int stream_index, int64_t *start_index,
	int64_t *duration, int *frame_rate_num, int *frame_rate_den)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	if (start_index) {
		*start_index = vp.start_time();
	}
	if (duration) {
		*duration = vp.duration();
	}
	if (frame_rate_num) {
		*frame_rate_num = vp.frame_rate().numerator();
	}
	if (frame_rate_den) {
		*frame_rate_den = vp.frame_rate().denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_footage_set_image_sequence_params(
	OakEngineFootage *self, int stream_index, int64_t start_index,
	int64_t duration, int frame_rate_num, int frame_rate_den)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	if (start_index < 0 || duration <= 0 || frame_rate_num <= 0 ||
		frame_rate_den <= 0) {
		set_error(QStringLiteral(
			"invalid image sequence parameters (need start >= 0, duration > "
			"0, positive frame rate)"));
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp;
	if (!video_stream_at(node, stream_index, &vp)) {
		set_error(QStringLiteral("no video stream at index %1")
					  .arg(stream_index));
		return OAKENGINE_E_NOT_FOUND;
	}
	vp.set_start_time(start_index);
	vp.set_duration(duration);
	const olive::Rational frame_rate(frame_rate_num, frame_rate_den);
	vp.set_frame_rate(frame_rate);
	vp.set_time_base(frame_rate.flipped());
	push_or_run(new FootageVideoParamsCommand(node, stream_index, vp),
				QStringLiteral("Set Image Sequence Parameters"));
	return OAKENGINE_OK;
}

int oakengine_footage_get_stream_enabled(OakEngineFootage *self,
										 int track_type, int index)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Footage *node = impl(self)->node;
	switch (track_type) {
	case 0:
		if (index >= 0 && index < node->get_video_stream_count()) {
			return node->get_video_params(index).enabled() ? 1 : 0;
		}
		break;
	case 1:
		if (index >= 0 && index < node->get_audio_stream_count()) {
			return node->get_audio_params(index).enabled() ? 1 : 0;
		}
		break;
	case 2:
		if (index >= 0 && index < node->get_subtitle_stream_count()) {
			return node->get_subtitle_params(index).enabled() ? 1 : 0;
		}
		break;
	default:
		break;
	}
	return OAKENGINE_E_NOT_FOUND;
}

int oakengine_footage_set_stream_enabled(OakEngineFootage *self,
										 int track_type, int index,
										 int enabled)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	switch (track_type) {
	case 0:
		if (index >= 0 && index < node->get_video_stream_count()) {
			olive::VideoParams vp = node->get_video_params(index);
			vp.set_enabled(enabled != 0);
			push_or_run(
				new FootageVideoParamsCommand(node, index, vp),
				QStringLiteral("Set Stream Enabled"));
			return OAKENGINE_OK;
		}
		break;
	case 1:
		if (index >= 0 && index < node->get_audio_stream_count()) {
			olive::AudioParams ap = node->get_audio_params(index);
			ap.set_enabled(enabled != 0);
			push_or_run(
				new FootageAudioParamsCommand(node, index, ap),
				QStringLiteral("Set Stream Enabled"));
			return OAKENGINE_OK;
		}
		break;
	case 2:
		if (index >= 0 && index < node->get_subtitle_stream_count()) {
			olive::SubtitleParams sp = node->get_subtitle_params(index);
			sp.set_enabled(enabled != 0);
			push_or_run(
				new FootageSubtitleParamsCommand(node, index, sp),
				QStringLiteral("Set Stream Enabled"));
			return OAKENGINE_OK;
		}
		break;
	default:
		set_error(QStringLiteral("unknown track type %1").arg(track_type));
		return OAKENGINE_E_INVALID;
	}
	set_error(QStringLiteral("no stream of type %1 at index %2")
				  .arg(track_type)
				  .arg(index));
	return OAKENGINE_E_NOT_FOUND;
}

int oakengine_footage_set_source_start_time(OakEngineFootage *self,
											int enabled, int64_t num,
											int64_t den)
{
	set_error(QString());
	olive::Footage *node = borrowed_node(self);
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	if (enabled && den == 0) {
		set_error(QStringLiteral("invalid time denominator 0"));
		return OAKENGINE_E_INVALID;
	}
	push_or_run(new FootageSourceStartTimeCommand(
					node, enabled != 0,
					olive::Rational::from_double(
						den != 0 ? double(num) / double(den) : 0.0)),
				QStringLiteral("Set Source Start Time"));
	return OAKENGINE_OK;
}

int oakengine_footage_get_source_start_time_source(OakEngineFootage *self,
												   char *buf, int buf_size)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->node->source_start_time_source(), buf,
						 buf_size);
}

/* ---- Colorspace candidates ----------------------------------------------------- */

int oakengine_footage_colorspace_count(const OakEngineFootage *self)
{
	if (!self || !impl(self)->node || !impl(self)->node->project()) {
		return 0;
	}
	return impl(self)->node->project()->color_manager()->get_config()
		->getNumColorSpaces();
}

int oakengine_footage_colorspace_at(const OakEngineFootage *self, int index,
									char *buf, int buf_size)
{
	if (!self || !impl(self)->node || !impl(self)->node->project()) {
		return OAKENGINE_E_INVALID;
	}
	const ocio::ConstConfigRcPtr config =
		impl(self)->node->project()->color_manager()->get_config();
	if (index < 0 || index >= config->getNumColorSpaces()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(config->getColorSpaceNameByIndex(index), buf,
						 buf_size);
}

/* ---- Footage extras ------------------------------------------------------- */

int oakengine_footage_get_filename(const OakEngineFootage *self, char *buf,
								   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	if (!s->node) {
		// Probed footage has no node; filename is not applicable.
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s->node->filename(), buf, buf_size);
}

int oakengine_footage_get_stream_reference(const OakEngineFootage *self,
										   int flat_index, int *out_track_type,
										   int *out_stream_index)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	if (!s->node) {
		return OAKENGINE_E_INVALID; // probed-only footage
	}
	const int vc = video_stream_count(s);
	const int ac = audio_stream_count(s);
	if (flat_index < vc) {
		if (out_track_type) {
			*out_track_type = OAKENGINE_TRACK_TYPE_VIDEO;
		}
		if (out_stream_index) {
			*out_stream_index = flat_index;
		}
		return OAKENGINE_OK;
	}
	flat_index -= vc;
	if (flat_index < ac) {
		if (out_track_type) {
			*out_track_type = OAKENGINE_TRACK_TYPE_AUDIO;
		}
		if (out_stream_index) {
			*out_stream_index = flat_index;
		}
		return OAKENGINE_OK;
	}
	flat_index -= ac;
	const int sc = subtitle_stream_count(s);
	if (flat_index >= sc) {
		return OAKENGINE_E_NOT_FOUND;
	}
	if (out_track_type) {
		*out_track_type = OAKENGINE_TRACK_TYPE_SUBTITLE;
	}
	if (out_stream_index) {
		*out_stream_index = flat_index;
	}
	return OAKENGINE_OK;
}

int oakengine_footage_describe_video_stream(const OakEngineFootage *self,
											int video_stream_index, char *buf,
											int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	if (!s->node) {
		// Import-handle-only family: probe handles are rejected (probe
		// metadata is read through the oak_footage_*_info accessors).
		return OAKENGINE_E_INVALID;
	}
	if (video_stream_index < 0 || video_stream_index >= video_stream_count(s)) {
		return OAKENGINE_E_NOT_FOUND;
	}
	olive::VideoParams vp;
	if (s->node) {
		vp = s->node->get_video_params(video_stream_index);
	} else {
		const auto &streams = s->description.get_video_streams();
		if (video_stream_index < streams.size()) {
			vp = streams.at(video_stream_index);
		} else {
			return OAKENGINE_E_NOT_FOUND;
		}
	}
	return string_to_buf(olive::Footage::describe_video_stream(vp), buf,
						 buf_size);
}

int oakengine_footage_describe_audio_stream(const OakEngineFootage *self,
											int audio_stream_index, char *buf,
											int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const OakEngineFootageState *s = impl(self);
	if (!s->node) {
		return OAKENGINE_E_INVALID;
	}
	if (audio_stream_index < 0 || audio_stream_index >= audio_stream_count(s)) {
		return OAKENGINE_E_NOT_FOUND;
	}
	olive::AudioParams ap;
	if (s->node) {
		ap = s->node->get_audio_params(audio_stream_index);
	} else {
		const auto &streams = s->description.get_audio_streams();
		if (audio_stream_index < streams.size()) {
			ap = streams.at(audio_stream_index);
		} else {
			return OAKENGINE_E_NOT_FOUND;
		}
	}
	return string_to_buf(olive::Footage::describe_audio_stream(ap), buf,
						 buf_size);
}

int oakengine_footage_stream_type_name(int track_type, char *buf, int buf_size)
{
	switch (track_type) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		return string_to_buf(QStringLiteral("Video"), buf, buf_size);
	case OAKENGINE_TRACK_TYPE_AUDIO:
		return string_to_buf(QStringLiteral("Audio"), buf, buf_size);
	case OAKENGINE_TRACK_TYPE_SUBTITLE:
		return string_to_buf(QStringLiteral("Subtitle"), buf, buf_size);
	default:
		return string_to_buf(QStringLiteral("Unknown"), buf, buf_size);
	}
}

int oakengine_footage_has_custom_proxy_params(const OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	return impl(self)->node->has_custom_proxy_params() ? 1 : 0;
}

int oakengine_footage_get_effective_proxy_params(const OakEngineFootage *self,
												 oak_proxy_params *out)
{
	if (!self || !out || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	const olive::ProxyManager::ProxyParams pp =
		impl(self)->node->get_effective_proxy_params();
	out->width = pp.width;
	out->height = pp.height;
	out->divider = pp.divider;
	out->version = pp.version;
	out->crf = pp.crf;
	out->include_audio = pp.include_audio ? 1 : 0;
	string_to_buf(pp.extension, out->extension, sizeof(out->extension));
	string_to_buf(pp.preset, out->preset, sizeof(out->preset));
	return OAKENGINE_OK;
}

int oakengine_footage_set_custom_proxy_params(OakEngineFootage *self,
											  const oak_proxy_params *params)
{
	if (!self || !params || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	olive::ProxyManager::ProxyParams pp;
	pp.width = params->width;
	pp.height = params->height;
	pp.divider = params->divider;
	pp.version = params->version;
	pp.crf = params->crf;
	pp.include_audio = params->include_audio != 0;
	pp.extension = QString::fromUtf8(params->extension);
	pp.preset = QString::fromUtf8(params->preset);
	impl(self)->node->set_custom_proxy_params(pp);
	return OAKENGINE_OK;
}

int oakengine_footage_clear_custom_proxy_params(OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->node->clear_custom_proxy_params();
	return OAKENGINE_OK;
}

int oakengine_footage_set_proxy(OakEngineFootage *self,
								const char *path, int state,
								int stream_index, int enabled, int version)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	olive::Footage *node = impl(self)->node;
	node->set_proxy(QString::fromUtf8(path ? path : ""),
					static_cast<olive::ProxyManager::ProxyState>(state),
					stream_index, version, enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_footage_clear_proxy(OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->node->clear_proxy();
	return OAKENGINE_OK;
}

int oakengine_footage_invalidate(OakEngineFootage *self)
{
	if (!self || !impl(self)->node) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->node->clear();
	return OAKENGINE_OK;
}

} // extern "C"
