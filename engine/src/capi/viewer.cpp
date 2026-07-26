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

#include "oakengine/viewer.h"

#include <cstring>

#include <QByteArray>
#include <QVector>

#include "oakengine/timeline.h"

#include "node/output/viewer/viewer.h"
#include "node/output/track/track.h"
#include "node/block/clip/clip.h"
#include "node/nodeundo.h"
#include "node/param.h"
#include "render/playbackcache.h"
#include "render/framehashcache.h"
#include "render/videoparams.h"
#include "timeline/timelineworkarea.h"

namespace
{

olive::Node *impl(OakEngineNode *h)
{
	return reinterpret_cast<olive::Node *>(h);
}

const olive::Node *impl(const OakEngineNode *h)
{
	return reinterpret_cast<const olive::Node *>(h);
}

// Validated viewer accessor; nullptr when the handle is not a viewer.
olive::ViewerOutput *viewer_of(OakEngineNode *h)
{
	return h ? dynamic_cast<olive::ViewerOutput *>(impl(h)) : nullptr;
}

const olive::ViewerOutput *viewer_of(const OakEngineNode *h)
{
	return h ? dynamic_cast<const olive::ViewerOutput *>(impl(h)) : nullptr;
}

// ViewerOutput::get_playhead()/get_connected_waveform() are not const in the
// engine; the facade keeps const-correct handles and casts locally (same
// pattern as timeline.cpp's mutable_impl()).
olive::ViewerOutput *mutable_viewer(const OakEngineNode *h)
{
	return const_cast<olive::ViewerOutput *>(viewer_of(h));
}

// olive::VideoParams -> oak_video_params POD (same mapping as
// encoding.cpp's from_cpp()).
void from_cpp(const olive::VideoParams &vp, oak_video_params *out)
{
	out->width = vp.width();
	out->height = vp.height();
	out->time_base_num = vp.time_base().numerator();
	out->time_base_den = vp.time_base().denominator();
	out->format = int(vp.format());
	out->pixel_aspect_num = vp.pixel_aspect_ratio().numerator();
	out->pixel_aspect_den = vp.pixel_aspect_ratio().denominator();
	out->interlacing = int(vp.interlacing());
	out->color_range = int(vp.color_range());
	out->divider = vp.divider();
	out->video_type = int(vp.video_type());
	out->premultiplied_alpha = vp.premultiplied_alpha() ? 1 : 0;
}

void rational_out(const olive::Rational &r, int64_t *num, int64_t *den)
{
	if (num) {
		*num = r.numerator();
	}
	if (den) {
		*den = r.denominator();
	}
}

// Track::Type values match the facade's OAKENGINE_TRACK_TYPE_* constants;
// assert it and convert explicitly anyway (k_none should never appear in an
// enabled-stream list).
static_assert(int(olive::Track::k_video) == OAKENGINE_TRACK_TYPE_VIDEO,
			  "track type mismatch");
static_assert(int(olive::Track::k_audio) == OAKENGINE_TRACK_TYPE_AUDIO,
			  "track type mismatch");
static_assert(int(olive::Track::k_subtitle) == OAKENGINE_TRACK_TYPE_SUBTITLE,
			  "track type mismatch");

int to_c_track_type(olive::Track::Type t)
{
	switch (t) {
	case olive::Track::k_video:
		return OAKENGINE_TRACK_TYPE_VIDEO;
	case olive::Track::k_audio:
		return OAKENGINE_TRACK_TYPE_AUDIO;
	case olive::Track::k_subtitle:
		return OAKENGINE_TRACK_TYPE_SUBTITLE;
	default:
		return -1;
	}
}

} // namespace

extern "C"
{

OakEngineNode *oakengine_viewer_from_node(OakEngineNode *node)
{
	return viewer_of(node) ? node : nullptr;
}

const OakEngineNode *oakengine_viewer_from_const_node(const OakEngineNode *node)
{
	return viewer_of(node) ? node : nullptr;
}

const char *oakengine_viewer_video_params_input_id(void)
{
	static const QByteArray s =
		olive::ViewerOutput::k_video_params_input.toUtf8();
	return s.constData();
}

const char *oakengine_viewer_audio_params_input_id(void)
{
	static const QByteArray s =
		olive::ViewerOutput::k_audio_params_input.toUtf8();
	return s.constData();
}

const char *oakengine_viewer_subtitle_params_input_id(void)
{
	static const QByteArray s =
		olive::ViewerOutput::k_subtitle_params_input.toUtf8();
	return s.constData();
}

const char *oakengine_viewer_texture_input_id(void)
{
	static const QByteArray s = olive::ViewerOutput::k_texture_input.toUtf8();
	return s.constData();
}

const char *oakengine_viewer_samples_input_id(void)
{
	static const QByteArray s = olive::ViewerOutput::k_samples_input.toUtf8();
	return s.constData();
}

int oakengine_viewer_default_sample_format(void)
{
	return int(olive::core::SampleFormat::Format(
		olive::ViewerOutput::k_default_sample_format));
}

int oakengine_viewer_get_playhead(const OakEngineNode *self, int64_t *num,
								  int64_t *den)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	rational_out(mutable_viewer(self)->get_playhead(), num, den);
	return OAKENGINE_OK;
}

int oakengine_viewer_set_playhead(OakEngineNode *self, int64_t num,
								  int64_t den)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	v->set_playhead(olive::Rational(num, den));
	return OAKENGINE_OK;
}

int oakengine_viewer_set_video_params(OakEngineNode *self,
                                      const oak_video_params *params,
                                      int index)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v || !params) {
		return OAKENGINE_E_INVALID;
	}
	olive::VideoParams vp(
		params->width, params->height,
		olive::Rational(params->time_base_num, params->time_base_den),
		static_cast<olive::core::PixelFormat::Format>(params->format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(params->pixel_aspect_num, params->pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(params->interlacing),
		params->divider);
	v->set_video_params(vp, index);
	return OAKENGINE_OK;
}

int oakengine_viewer_set_audio_params(OakEngineNode *self, int sample_rate,
                                      uint64_t channel_layout, int format,
                                      int index)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	olive::AudioParams ap;
	ap.set_sample_rate(sample_rate);
	ap.set_channel_layout(channel_layout);
	ap.set_format(static_cast<olive::core::SampleFormat::Format>(format));
	v->set_audio_params(ap, index);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_length(const OakEngineNode *self, int64_t *num,
								int64_t *den)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	rational_out(v->get_length(), num, den);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_video_length(const OakEngineNode *self, int64_t *num,
									  int64_t *den)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	rational_out(v->get_video_length(), num, den);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_audio_length(const OakEngineNode *self, int64_t *num,
									  int64_t *den)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	rational_out(v->get_audio_length(), num, den);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_video_params(const OakEngineNode *self, int index,
									  oak_video_params *out)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || !out) {
		return OAKENGINE_E_INVALID;
	}
	memset(out, 0, sizeof(*out));
	from_cpp(v->get_video_params(index), out);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_audio_params(const OakEngineNode *self, int index,
									  int *sample_rate,
									  uint64_t *channel_layout, int *format)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	if (sample_rate) {
		*sample_rate = 0;
	}
	if (channel_layout) {
		*channel_layout = 0;
	}
	if (format) {
		*format = 0;
	}
	if (index < 0 || index >= v->get_audio_stream_count()) {
		return OAKENGINE_OK;
	}
	const olive::AudioParams params = v->get_audio_params(index);
	if (sample_rate) {
		*sample_rate = params.sample_rate();
	}
	if (channel_layout) {
		*channel_layout = params.channel_layout();
	}
	if (format) {
		*format = int(olive::core::SampleFormat::Format(params.format()));
	}
	return OAKENGINE_OK;
}

int oakengine_viewer_get_video_stream_count(const OakEngineNode *self)
{
	const olive::ViewerOutput *v = viewer_of(self);
	return v ? v->get_video_stream_count() : 0;
}

int oakengine_viewer_get_audio_stream_count(const OakEngineNode *self)
{
	const olive::ViewerOutput *v = viewer_of(self);
	return v ? v->get_audio_stream_count() : 0;
}

int oakengine_viewer_get_subtitle_stream_count(const OakEngineNode *self)
{
	const olive::ViewerOutput *v = viewer_of(self);
	return v ? v->get_subtitle_stream_count() : 0;
}

int oakengine_viewer_get_stream_enabled(const OakEngineNode *self,
										int track_type, int index)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}

	switch (track_type) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		return index >= 0 && index < v->get_video_stream_count() &&
			   v->get_video_params(index).enabled();
	case OAKENGINE_TRACK_TYPE_AUDIO:
		return index >= 0 && index < v->get_audio_stream_count() &&
			   v->get_audio_params(index).enabled();
	case OAKENGINE_TRACK_TYPE_SUBTITLE:
		return index >= 0 && index < v->get_subtitle_stream_count() &&
			   v->get_subtitle_params(index).enabled();
	default:
		return OAKENGINE_E_INVALID;
	}
}

int oakengine_viewer_get_subtitle_count(const OakEngineNode *self, int index)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || index < 0 || index >= v->get_subtitle_stream_count()) {
		return OAKENGINE_E_INVALID;
	}

	return int(v->get_subtitle_params(index).size());
}

const void *oakengine_viewer_get_subtitle_at(const OakEngineNode *self,
											 int index, int sub_index)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || index < 0 || index >= v->get_subtitle_stream_count()) {
		return nullptr;
	}

	const olive::SubtitleParams &sp = v->get_subtitle_params(index);
	if (sub_index < 0 || sub_index >= int(sp.size())) {
		return nullptr;
	}

	return &sp[size_t(sub_index)];
}

int oakengine_viewer_has_enabled_streams(const OakEngineNode *self,
										 int track_type)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return 0;
	}
	switch (track_type) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		return v->has_enabled_video_streams() ? 1 : 0;
	case OAKENGINE_TRACK_TYPE_AUDIO:
		return v->has_enabled_audio_streams() ? 1 : 0;
	case OAKENGINE_TRACK_TYPE_SUBTITLE:
		return v->has_enabled_subtitle_streams() ? 1 : 0;
	default:
		return 0;
	}
}

int oakengine_viewer_get_first_enabled_video_stream(const OakEngineNode *self,
													oak_video_params *out)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || !out) {
		return OAKENGINE_E_INVALID;
	}
	memset(out, 0, sizeof(*out));
	from_cpp(v->get_first_enabled_video_stream(), out);
	return OAKENGINE_OK;
}

int oakengine_viewer_get_enabled_stream_count(const OakEngineNode *self)
{
	const olive::ViewerOutput *v = viewer_of(self);
	return v ? int(v->get_enabled_streams_as_references().size()) : 0;
}

int oakengine_viewer_get_enabled_streams(const OakEngineNode *self, int *types,
										 int *indices, int max)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || max < 0) {
		return 0;
	}
	const QVector<olive::Track::Reference> refs =
		v->get_enabled_streams_as_references();
	if (types && indices) {
		const int n = qMin(int(refs.size()), max);
		for (int i = 0; i < n; i++) {
			types[i] = to_c_track_type(refs.at(i).type());
			indices[i] = refs.at(i).index();
		}
	}
	return int(refs.size());
}

int oakengine_viewer_get_workarea(const OakEngineNode *self,
								  oakengine_viewer_workarea *out)
{
	const olive::ViewerOutput *v = viewer_of(self);
	if (!v || !out) {
		return OAKENGINE_E_INVALID;
	}
	const olive::TimelineWorkArea *workarea = v->get_work_area();
	out->in_num = workarea->in().numerator();
	out->in_den = workarea->in().denominator();
	out->out_num = workarea->out().numerator();
	out->out_den = workarea->out().denominator();
	out->enabled = workarea->enabled() ? 1 : 0;
	return OAKENGINE_OK;
}

int oakengine_viewer_set_workarea_range(OakEngineNode *self, int64_t in_num,
										int64_t in_den, int64_t out_num,
										int64_t out_den)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	v->get_work_area()->set_range(
		olive::TimeRange(olive::Rational(in_num, in_den),
						 olive::Rational(out_num, out_den)));
	return OAKENGINE_OK;
}

int oakengine_viewer_set_workarea_enabled(OakEngineNode *self, int enabled)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	v->get_work_area()->set_enabled(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_viewer_set_default_parameters(OakEngineNode *self)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	v->set_default_parameters();
	return OAKENGINE_OK;
}

extern "C" void *oakengine_viewer_set_preview_divider_command(
	OakEngineNode *self, int divider)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v || divider < 1) {
		return nullptr;
	}
	olive::VideoParams current = v->get_video_params();
	if (current.divider() == divider) {
		return nullptr;
	}
	const olive::VideoParams updated(
		current.width(), current.height(), current.time_base(),
		current.format(), current.channel_count(),
		current.pixel_aspect_ratio(), current.interlacing(), divider);
	return new olive::NodeParamSetStandardValueCommand(
		olive::NodeKeyframeTrackReference(
			olive::NodeInput(v, olive::ViewerOutput::k_video_params_input), 0),
		QVariant::fromValue(updated));
}

int oakengine_viewer_set_parameters_from_footage(
	OakEngineNode *self, OakEngineNode *const *footage, int count)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v || count < 0 || (count > 0 && !footage)) {
		return OAKENGINE_E_INVALID;
	}
	QVector<olive::ViewerOutput *> viewers;
	viewers.reserve(count);
	for (int i = 0; i < count; i++) {
		olive::ViewerOutput *f = viewer_of(footage[i]);
		if (!f) {
			return OAKENGINE_E_INVALID;
		}
		viewers.append(f);
	}
	v->set_parameters_from_footage(viewers);
	return OAKENGINE_OK;
}

int oakengine_viewer_set_waveform_enabled(OakEngineNode *self, int enabled)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return OAKENGINE_E_INVALID;
	}
	v->set_waveform_enabled(enabled != 0);
	return OAKENGINE_OK;
}

const void *oakengine_viewer_get_connected_waveform(const OakEngineNode *self)
{
	olive::ViewerOutput *v = mutable_viewer(self);
	if (!v) {
		return nullptr;
	}
	return static_cast<const void *>(v->get_connected_waveform());
}

OakEngineMarkerList *oakengine_viewer_get_marker_list(OakEngineNode *self)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineMarkerList *>(v->get_markers());
}

OakEngineWorkarea *oakengine_viewer_get_workarea_handle(OakEngineNode *self)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineWorkarea *>(v->get_work_area());
}

/* ---- Playback cache / frame cache ------------------------------------------ */

OakEnginePlaybackCache *
oakengine_viewer_get_playback_cache(OakEngineNode *self)
{
	if (!self) {
		return nullptr;
	}
	olive::ClipBlock *clip = dynamic_cast<olive::ClipBlock *>(
		reinterpret_cast<olive::Node *>(self));
	if (!clip) {
		return nullptr;
	}
	return reinterpret_cast<OakEnginePlaybackCache *>(
		clip->connected_video_cache());
}

int oakengine_playback_cache_indicator_height(void)
{
	return olive::PlaybackCache::get_cache_indicator_height();
}

int oakengine_playback_cache_valid_ranges(OakEnginePlaybackCache *cache,
										  int64_t *ranges, int max)
{
	if (!cache) {
		return OAKENGINE_E_INVALID;
	}
	olive::PlaybackCache *pc = reinterpret_cast<olive::PlaybackCache *>(cache);
	const olive::TimeRangeList &valid = pc->get_validated_ranges();
	const int count = qMin(max, int(valid.size()));
	for (int i = 0; i < count; i++) {
		ranges[i * 4 + 0] = valid.at(i).in().numerator();
		ranges[i * 4 + 1] = valid.at(i).in().denominator();
		ranges[i * 4 + 2] = valid.at(i).out().numerator();
		ranges[i * 4 + 3] = valid.at(i).out().denominator();
	}
	return count;
}

OakEngineFrameCache *oakengine_viewer_get_frame_cache(OakEngineNode *self)
{
	olive::ViewerOutput *v = viewer_of(self);
	if (!v) {
		return nullptr;
	}
	// For a clip, get its connected video cache as a FrameHashCache.
	if (olive::ClipBlock *clip = dynamic_cast<olive::ClipBlock *>(v)) {
		return reinterpret_cast<OakEngineFrameCache *>(
			clip->connected_video_cache());
	}
	return nullptr;
}

} // extern "C"
