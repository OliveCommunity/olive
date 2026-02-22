/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "renderprocessor.h"

#include <QOpenGLContext>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "audio/audioprocessor.h"
#include "node/block/clip/clip.h"
#include "node/block/transition/transition.h"
#include "node/project.h"
#include "rendermanager.h"
#include "render/framehashcache.h"
#include "render/framememorycache.h"
#include "render/opengl/openglrenderer.h"
#include "render/plugin/pluginrenderer.h"
#include "pluginSupport/OliveClip.h"
#include "pluginSupport/OliveHost.h"
#include "olive/core/util/timecodefunctions.h"
#include <mutex>
#include <unordered_map>

namespace olive
{

#define super NodeTraverser

namespace {

FrameMemCache *ProjectFrameMemCache(Project *project)
{
	if (!project) {
		return nullptr;
	}

	static std::mutex caches_mutex;
	static std::unordered_map<std::string, std::unique_ptr<FrameMemCache>>
		project_caches;

	const std::string key =
		project->GetUuid().toString(QUuid::WithoutBraces).toStdString();

	std::lock_guard<std::mutex> lock(caches_mutex);
	std::unique_ptr<FrameMemCache> &entry = project_caches[key];
	if (!entry) {
		entry = std::make_unique<FrameMemCache>();
	}
	return entry.get();
}

} // namespace

RenderProcessor::RenderProcessor(RenderTicketPtr ticket, OpenGLThread *gl_thread,
								 DecoderCache *decoder_cache,
								 ShaderCache *shader_cache)
	: ticket_(ticket)
	, gl_thread_(gl_thread)
	, decoder_cache_(decoder_cache)
	, shader_cache_(shader_cache)
{
}

TexturePtr RenderProcessor::GenerateTexture(const rational &time,
											const rational &frame_length)
{
	TimeRange range = TimeRange(time, time + frame_length);

	NodeValueTable table;
	if (Node *node = QtUtils::ValueToPtr<Node>(ticket_->property("node"))) {
		table = GenerateTable(node, range);
	}

	NodeValue tex_val = table.Get(NodeValue::kTexture);

	ResolveJobs(tex_val);

	return tex_val.toTexture();
}

FramePtr RenderProcessor::GenerateFrame(TexturePtr texture,
										const rational &time)
{
	// Set up output frame parameters
	VideoParams frame_params = GetCacheVideoParams();

	QSize frame_size = ticket_->property("size").value<QSize>();
	if (!frame_size.isNull()) {
		frame_params.set_width(frame_size.width());
		frame_params.set_height(frame_size.height());
	}

	PixelFormat frame_format =
		static_cast<PixelFormat::Format>(ticket_->property("format").toInt());
	if (frame_format != PixelFormat::INVALID) {
		frame_params.set_format(frame_format);
	}

	int force_channel_count = ticket_->property("channelcount").toInt();
	if (force_channel_count != 0) {
		frame_params.set_channel_count(force_channel_count);
	} else {
		frame_params.set_channel_count(texture ?
										   texture->channel_count() :
										   VideoParams::kRGBAChannelCount);
	}

	FramePtr frame = Frame::Create();
	frame->set_timestamp(time);
	frame->set_video_params(frame_params);
	frame->allocate();

	if (!texture) {
		// Blank frame out
		memset(frame->data(), 0, frame->allocated_size());
	} else {
		// Dump texture contents to frame
		ColorProcessorPtr output_color_transform =
			ticket_->property("coloroutput").value<ColorProcessorPtr>();
		const VideoParams &tex_params = texture->params();

		if (output_color_transform) {
			TexturePtr transform_tex = gl_thread_->CreateTexture(tex_params);
			ColorTransformJob job;

			job.SetColorProcessor(output_color_transform);
			job.SetInputTexture(texture);
			job.SetInputAlphaAssociation(
				OLIVE_CONFIG("ReassocLinToNonLin").toBool() ? kAlphaAssociated :
														  kAlphaNone);

			gl_thread_->BlitColorManaged(job, transform_tex, tex_params);

			texture = transform_tex;
		}

		if (tex_params.effective_width() != frame_params.effective_width() ||
			tex_params.effective_height() != frame_params.effective_height() ||
			tex_params.format() != frame_params.format()) {
			TexturePtr blit_tex = gl_thread_->CreateTexture(frame_params);

			QMatrix4x4 matrix = ticket_->property("matrix").value<QMatrix4x4>();

			// No color transform, just blit
			ShaderJob job;
			job.Insert(QStringLiteral("ove_maintex"),
					   NodeValue(NodeValue::kTexture,
								 QVariant::fromValue(texture)));
			job.Insert(QStringLiteral("ove_mvpmat"),
					   NodeValue(NodeValue::kMatrix, matrix));

			// 获取默认 shader
			QVariant default_shader = gl_thread_->CreateShader(ShaderCode());
			gl_thread_->BlitShader(default_shader, job, blit_tex, frame_params, true);
			gl_thread_->DestroyShader(default_shader);

			// Replace texture that we're going to download in the next step
			texture = blit_tex;
		}

		gl_thread_->Flush();

		gl_thread_->DownloadTexture(texture, frame->data(),
									frame->linesize_pixels());
	}

	return frame;
}

void RenderProcessor::Run()
{
	// Depending on the render ticket type, start a job
	RenderManager::TicketType type =
		ticket_->property("type").value<RenderManager::TicketType>();

	SetCancelPointer(ticket_->GetCancelAtom());

	// 根据任务类型设置参数，避免无效参数导致崩溃
	if (type == RenderManager::kTypeVideo) {
		SetCacheVideoParams(ticket_->property("vparam").value<VideoParams>());
		SetCacheAudioParams(ticket_->property("aparam").value<AudioParams>());
	} else {
		// 音频任务只设置音频参数
		SetCacheAudioParams(ticket_->property("aparam").value<AudioParams>());
	}

	if (IsCancelled()) {
		ticket_->Finish();
		return;
	}

	switch (type) {
	case RenderManager::kTypeVideo: {
		rational time = ticket_->property("time").value<rational>();

		rational frame_length = GetCacheVideoParams().frame_rate_as_time_base();
		if (GetCacheVideoParams().interlacing() !=
			VideoParams::kInterlaceNone) {
			frame_length /= 2;
		}

		TexturePtr texture = GenerateTexture(time, frame_length);

		if (!gl_thread_) {
			ticket_->Finish();
		} else {
			if (GetCacheVideoParams().interlacing() !=
				VideoParams::kInterlaceNone) {
				// Get next between frame and interlace it
				TexturePtr top = texture;
				TexturePtr bottom =
					GenerateTexture(time + frame_length, frame_length);

				if (GetCacheVideoParams().interlacing() ==
					VideoParams::kInterlacedBottomFirst) {
					std::swap(top, bottom);
				}

				texture = gl_thread_->InterlaceTexture(top, bottom,
													   GetCacheVideoParams());
			}

			if (HeardCancel()) {
				// Finish cancelled ticket with nothing since we can't guarantee the frame we generated
				// is actually "complete
				ticket_->Finish();
			} else {
				FramePtr frame;
				QString cache = ticket_->property("cache").toString();
				RenderManager::ReturnType return_type =
					RenderManager::ReturnType(
						ticket_->property("return").toInt());

				if (return_type == RenderManager::kFrame || !cache.isEmpty()) {
					// Convert to CPU frame
					frame = GenerateFrame(texture, time);

					// Save to cache if requested
					if (!cache.isEmpty()) {
						rational timebase =
							ticket_->property("cachetimebase").value<rational>();
						QUuid uuid =
							ticket_->property("cacheid").value<QUuid>();
						bool cache_result = false;
						if (!uuid.isNull() && frame) {
							rational tb = timebase;
							if (tb.isNull() || tb.isNaN()) {
								tb = GetCacheVideoParams().time_base();
							}
							if (tb.isNull() || tb.isNaN()) {
								tb = GetCacheVideoParams().frame_rate_as_time_base();
							}
							if (tb.isNull() || tb.isNaN()) {
								tb = rational(1, 1);
							}
							const int64_t timestamp = Timecode::time_to_timestamp(
								time, tb, Timecode::kRound);

							Project *project = nullptr;
							if (Node *render_node = QtUtils::ValueToPtr<Node>(
									ticket_->property("node"))) {
								project = render_node->project();
							}
							if (FrameMemCache *mem_cache =
									ProjectFrameMemCache(project)) {
								mem_cache->SetUuid(uuid);
								mem_cache->SetBackingCachePath(cache);
								cache_result =
									mem_cache->SaveCacheFrame(timestamp, frame);
							}
						}
						ticket_->setProperty("cached", cache_result);
					}
				}

				if (return_type == RenderManager::kTexture) {
					// Return GPU texture
					if (!texture) {
						texture = gl_thread_->CreateTexture(GetCacheVideoParams());
						gl_thread_->ClearDestination(texture, 0, 0, 0, 0);
					}

					gl_thread_->Flush();

					ticket_->Finish(QVariant::fromValue(texture));
				} else {
					ticket_->Finish(QVariant::fromValue(frame));
				}
			}
		}
		break;
	}
	case RenderManager::kTypeAudio: {
		TimeRange time = ticket_->property("time").value<TimeRange>();

		NodeValueTable table;
		if (Node *node = QtUtils::ValueToPtr<Node>(ticket_->property("node"))) {
			table = GenerateTable(node, time);
		}

		NodeValue sample_val = table.Get(NodeValue::kSamples);

		ResolveJobs(sample_val);

		SampleBuffer samples = sample_val.toSamples();
		if (samples.is_allocated()) {
			if (ticket_->property("clamp").toBool() && !IsCancelled()) {
				samples.clamp();
			}

			if (ticket_->property("enablewaveforms").toBool() &&
				!IsCancelled()) {
				AudioVisualWaveform vis;
				vis.set_channel_count(samples.audio_params().channel_count());
				vis.OverwriteSamples(samples,
									 samples.audio_params().sample_rate());
				ticket_->setProperty("waveform", QVariant::fromValue(vis));
			}
		}

		if (HeardCancel()) {
			ticket_->Finish();
		} else {
			ticket_->Finish(QVariant::fromValue(samples));
		}
		break;
	}
	default:
		// Fail
		ticket_->Finish();
	}
}

DecoderPtr
RenderProcessor::ResolveDecoderFromInput(const QString &decoder_id,
										 const Decoder::CodecStream &stream)
{
	if (!stream.IsValid()) {
		qWarning() << "Attempted to resolve the decoder of a null stream";
		return nullptr;
	}

	QMutexLocker locker(decoder_cache_->mutex());

	DecoderPair decoder = decoder_cache_->value(stream);

	qint64 file_last_modified =
		QFileInfo(stream.filename()).lastModified().toMSecsSinceEpoch();

	DecoderPtr dec = nullptr;

	if (decoder.decoder && decoder.last_modified == file_last_modified) {
		dec = decoder.decoder;
	} else {
		// No decoder
		decoder.decoder = dec = Decoder::CreateFromID(decoder_id);
		decoder.last_modified = file_last_modified;
		decoder_cache_->insert(stream, decoder);
		locker.unlock();

		if (!dec->Open(stream)) {
			qWarning() << "Failed to open decoder for" << stream.filename()
					   << "::" << stream.stream();
			return nullptr;
		}

		if (!gl_thread_) {
			// Assume dry run and increment access time
			decoder.decoder->IncrementAccessTime(
				RenderManager::kDryRunInterval.toDouble() * 1000);
		}
	}

	return dec;
}

NodeValueDatabase RenderProcessor::GenerateDatabase(const Node *node,
													const TimeRange &range)
{
	NodeValueDatabase db = super::GenerateDatabase(node, range);

	if (const MultiCamNode *multicam =
			dynamic_cast<const MultiCamNode *>(node)) {
		if (QtUtils::ValueToPtr<MultiCamNode>(ticket_->property("multicam")) ==
			multicam) {
			int sz = multicam->GetSourceCount();
			QVector<TexturePtr> multicam_tex(sz);
			for (int i = 0; i < sz; i++) {
				NodeValueTable t =
					GenerateTable(multicam->GetConnectedRenderOutput(
									  multicam->kSourcesInput, i),
								  range, multicam);
				NodeValue val = GenerateRowValueElement(
					multicam, multicam->kSourcesInput, i, &t, range);
				ResolveJobs(val);

				multicam_tex[i] = val.toTexture();
			}
			ticket_->setProperty("multicam_output",
								 QVariant::fromValue(multicam_tex));
		}
	}

	return db;
}

void RenderProcessor::Process(RenderTicketPtr ticket, OpenGLThread *gl_thread,
							  DecoderCache *decoder_cache,
							  ShaderCache *shader_cache)
{
	RenderProcessor p(ticket, gl_thread, decoder_cache, shader_cache);
	p.Run();
}

void RenderProcessor::ProcessVideoFootage(TexturePtr destination,
										  const FootageJob *stream,
										  const rational &input_time)
{
	if (ticket_->property("type").value<RenderManager::TicketType>() !=
		RenderManager::kTypeVideo) {
		// Video cannot contribute to audio, so we do nothing here
		return;
	}

	// Check the still frame cache. On large frames such as high resolution still images, uploading
	// and color managing them for every frame is a waste of time, so we implement a small cache here
	// to optimize such a situation
	VideoParams stream_data = stream->video_params();

	ColorManager *color_manager =
		QtUtils::ValueToPtr<ColorManager>(ticket_->property("colormanager"));

	QString using_colorspace = stream_data.colorspace();

	if (using_colorspace.isEmpty()) {
		// FIXME:
		qWarning() << "HAVEN'T GOTTEN DEFAULT INPUT COLORSPACE";
	}

	Decoder::CodecStream default_codec_stream(
		stream->filename(), stream_data.stream_index(), GetCurrentBlock());

	QString decoder_id = stream->decoder();

	DecoderPtr decoder = nullptr;

	switch (stream_data.video_type()) {
	case VideoParams::kVideoTypeVideo:
	case VideoParams::kVideoTypeStill:
		decoder = ResolveDecoderFromInput(decoder_id, default_codec_stream);
		break;
	case VideoParams::kVideoTypeImageSequence: {
		if (gl_thread_) {
			// Since image sequences involve multiple files, we don't engage the decoder cache
			decoder = Decoder::CreateFromID(decoder_id);

			QString frame_filename;

			int64_t frame_number =
				stream_data.get_time_in_timebase_units(input_time);
			frame_filename = Decoder::TransformImageSequenceFileName(
				stream->filename(), frame_number);

			// Decoder will close automatically since it's a stream_ptr
			decoder->Open(Decoder::CodecStream(
				frame_filename, stream_data.stream_index(), GetCurrentBlock()));
		}
		break;
	}
	}

	if (decoder && gl_thread_) {
		Decoder::RetrieveVideoParams p;
		p.divider = stream->video_params().divider();
		p.maximum_format = destination->format();

		if (!IsCancelled()) {
			VideoParams tex_params = stream->video_params();

			if (tex_params.is_valid()) {
				TexturePtr unmanaged_texture;

				p.renderer = nullptr;  // 通过 GL 线程处理
				p.gl_thread = gl_thread_;
				p.time =
					(stream_data.video_type() == VideoParams::kVideoTypeVideo) ?
						input_time :
						Decoder::kAnyTimecode;
				p.cancelled = GetCancelPointer();
				p.force_range = stream_data.color_range();
				p.src_interlacing = stream_data.interlacing();

				unmanaged_texture = decoder->RetrieveVideo(p);

				if (!IsCancelled() && unmanaged_texture) {
					// We convert to our rendering pixel format, since that will always be float-based which
					// is necessary for correct color conversion
					ColorProcessorPtr processor = ColorProcessor::Create(
						color_manager, using_colorspace,
						color_manager->GetReferenceColorSpace());

					ColorTransformJob job;

					job.SetColorProcessor(processor);
					job.SetInputTexture(unmanaged_texture);

					if (stream_data.channel_count() !=
							VideoParams::kRGBAChannelCount ||
						stream_data.colorspace() ==
							color_manager->GetReferenceColorSpace()) {
						job.SetInputAlphaAssociation(kAlphaNone);
					} else if (stream_data.premultiplied_alpha()) {
						job.SetInputAlphaAssociation(kAlphaAssociated);
					} else {
						job.SetInputAlphaAssociation(kAlphaUnassociated);
					}

					gl_thread_->BlitColorManaged(job, destination, destination->params());
				}
			}
		}
	}
}

void RenderProcessor::ProcessAudioFootage(SampleBuffer &destination,
										  const FootageJob *stream,
										  const TimeRange &input_time)
{
	DecoderPtr decoder = ResolveDecoderFromInput(
		stream->decoder(),
		Decoder::CodecStream(stream->filename(),
							 stream->audio_params().stream_index(), nullptr));

	if (decoder) {
		const AudioParams &audio_params = GetCacheAudioParams();

		Decoder::RetrieveAudioStatus status = decoder->RetrieveAudio(
			destination, input_time, audio_params, stream->cache_path(),
			loop_mode(),
			static_cast<RenderMode::Mode>(ticket_->property("mode").toInt()));

		if (status == Decoder::kWaitingForConform) {
			ticket_->setProperty("incomplete", true);
		}
	}
}

void RenderProcessor::ProcessShader(TexturePtr destination, const Node *node,
									const ShaderJob *job)
{
	if (!gl_thread_) {
		return;
	}

	QString full_shader_id =
		QStringLiteral("%1:%2").arg(node->id(), job->GetShaderID());

	QMutexLocker locker(shader_cache_->mutex());

	QVariant shader = shader_cache_->value(full_shader_id);

	if (shader.isNull()) {
		locker.unlock();
		// Since we have shader code, compile it now
		shader = gl_thread_->CreateShader(
			node->GetShaderCode(job->GetShaderID()));
		locker.relock();

		if (shader.isNull()) {
			// Couldn't find or build the shader required
			return;
		}

		shader_cache_->insert(full_shader_id, shader);
	}

	locker.unlock();

	// Run shader
	gl_thread_->BlitShader(shader, const_cast<ShaderJob&>(*job), destination,
						   destination->params(), true);
}

void RenderProcessor::ProcessSamples(SampleBuffer &destination,
									 const Node *node, const TimeRange &range,
									 const SampleJob &job)
{
	if (!job.samples().is_allocated()) {
		return;
	}

	NodeValueRow value_db;

	const AudioParams &audio_params = GetCacheAudioParams();

	for (size_t i = 0; i < job.samples().sample_count(); i++) {
		// Calculate the exact rational time at this sample
		double sample_to_second =
			static_cast<double>(i) /
			static_cast<double>(audio_params.sample_rate());

		rational this_sample_time =
			rational::fromDouble(range.in().toDouble() + sample_to_second);

		// Update all non-sample and non-footage inputs
		for (auto j = job.GetValues().constBegin();
			 j != job.GetValues().constEnd(); j++) {
			TimeRange r = TimeRange(this_sample_time, this_sample_time);
			NodeValueTable value = ProcessInput(node, j.key(), r);

			value_db.insert(j.key(),
							GenerateRowValue(node, j.key(), &value, r));
		}

		node->ProcessSamples(value_db, job.samples(), destination, i);
	}
}

void RenderProcessor::ProcessColorTransform(TexturePtr destination,
											const Node *node,
											const ColorTransformJob *job)
{
	if (!gl_thread_) {
		return;
	}

	gl_thread_->BlitColorManaged(*job, destination, destination->params());
}

void RenderProcessor::ProcessFrameGeneration(TexturePtr destination,
											 const Node *node,
											 const GenerateJob *job)
{
	if (!gl_thread_) {
		return;
	}

	FramePtr frame = Frame::Create();

	frame->set_video_params(destination->params());
	frame->allocate();

	node->GenerateFrame(frame, *job);

	gl_thread_->UploadTexture(destination, frame->data(), frame->linesize_pixels());
}

TexturePtr RenderProcessor::ProcessPluginJob(TexturePtr texture,
											 TexturePtr destination,
											 const Node *node)
{
	if (!gl_thread_ || !texture || !destination) {
		return destination;
	}

	auto *plugin_job =
		dynamic_cast<plugin::PluginJob *>(texture->job());
	if (!plugin_job) {
		return destination;
	}

	// FIXME: Plugin rendering needs to be adapted to use OpenGLThread
	// For now, skip plugin rendering
	return destination;
}

TexturePtr RenderProcessor::ProcessVideoCacheJob(const CacheJob *val)
{
	const QUuid uuid = val->GetUuid();
	const int64_t timestamp = val->GetTime();
	if (uuid.isNull()) {
		return nullptr;
	}

	Project *project = nullptr;
	if (Node *render_node = QtUtils::ValueToPtr<Node>(ticket_->property("node"))) {
		project = render_node->project();
	}
	if (!project && val->GetFallback().source()) {
		project = val->GetFallback().source()->project();
	}

	if (FrameMemCache *mem_cache = ProjectFrameMemCache(project)) {
		mem_cache->SetUuid(uuid);
		if (project) {
			mem_cache->SetBackingCachePath(project->cache_path());
		}
		FramePtr mem_frame = mem_cache->LoadCacheFrame(timestamp);
		if (mem_frame) {
			TexturePtr tex = CreateTexture(mem_frame->video_params());
			if (tex) {
				gl_thread_->UploadTexture(tex, mem_frame->data(), mem_frame->linesize_pixels());
				return tex;
			}
		}
	}

	QStringList s = ticket_->property("badcache").toStringList();
	s.append(QStringLiteral("%1/%2")
				 .arg(uuid.toString(QUuid::WithoutBraces))
				 .arg(timestamp));
	ticket_->setProperty("badcache", s);

	return nullptr;
}

TexturePtr RenderProcessor::CreateTexture(const VideoParams &p)
{
	if (gl_thread_) {
		return gl_thread_->CreateTexture(p);
	} else {
		return super::CreateTexture(p);
	}
}

void RenderProcessor::ConvertToReferenceSpace(TexturePtr destination,
											  TexturePtr source,
											  const QString &input_cs)
{
	if (!gl_thread_) {
		return;
	}

	ColorManager *color_manager =
		QtUtils::ValueToPtr<ColorManager>(ticket_->property("colormanager"));
	ColorProcessorPtr cp = ColorProcessor::Create(
		color_manager, input_cs, color_manager->GetReferenceColorSpace());

	ColorTransformJob ctj;

	ctj.SetColorProcessor(cp);
	ctj.SetInputTexture(source);
	ctj.SetInputAlphaAssociation(kAlphaAssociated);

	gl_thread_->BlitColorManaged(ctj, destination, destination->params());
}

bool RenderProcessor::UseCache() const
{
	return static_cast<RenderMode::Mode>(ticket_->property("mode").toInt()) ==
		   RenderMode::kOffline;
}

} // namespace olive
