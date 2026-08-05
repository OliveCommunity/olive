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

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "block/clip/clip.h"
#include "block/transition/transition.h"
#include "project.h"
#include "rendermanager.h"
#include "framehashcache.h"
#include "plugin/pluginrenderer.h"
#include "plugins/plugin.h"
#include "ipc/frameslotpool.h"
#include "texturehandle.h"

namespace olive
{

#define super NodeTraverser

static int64_t file_last_modified_ms(const std::string &path)
{
	std::error_code ec;
	const auto t = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return 0;
	}
	// file clock -> system_clock conversion (QFileInfo::lastModified equivalent)
	const auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(
		t - decltype(t)::clock::now() + std::chrono::system_clock::now());
	return sys.time_since_epoch().count();
}

RenderProcessor::RenderProcessor(RenderTicketPtr ticket, Renderer *render_ctx,
								 DecoderCache *decoder_cache,
								 ShaderCache *shader_cache)
	: ticket_(ticket)
	, render_ctx_(render_ctx)
	, decoder_cache_(decoder_cache)
	, shader_cache_(shader_cache)
{
}

TexturePtr RenderProcessor::generate_texture(const Rational &time,
											const Rational &frame_length)
{
	TimeRange range = TimeRange(time, time + frame_length);

	NodeValueTable table;
	if (Node *node = ticket_->property("node").value<Node *>()) {
		table = generate_table(node, range);
	}

	NodeValue tex_val = table.get(NodeValue::k_texture);

	resolve_jobs(tex_val);

	return tex_val.to_texture();
}

FramePtr RenderProcessor::generate_frame(TexturePtr texture,
										const Rational &time)
{
	// Set up output frame parameters
	VideoParams frame_params = get_cache_video_params();

	FrameSize frame_size = ticket_->property("size").value<FrameSize>();
	if (!frame_size.is_null()) {
		frame_params.set_width(frame_size.width());
		frame_params.set_height(frame_size.height());
	}

	PixelFormat frame_format =
		static_cast<PixelFormat::Format>(ticket_->property("format").to_int());
	if (frame_format != PixelFormat::invalid) {
		frame_params.set_format(frame_format);
	}

	int force_channel_count = ticket_->property("channelcount").to_int();
	if (force_channel_count != 0) {
		frame_params.set_channel_count(force_channel_count);
	} else {
		frame_params.set_channel_count(texture ?
										   texture->channel_count() :
										   VideoParams::k_rgba_channel_count);
	}

	FramePtr frame = Frame::create();
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
			TexturePtr transform_tex = render_ctx_->create_texture(tex_params);
			ColorTransformJob job;

			job.set_color_processor(output_color_transform);
			job.set_input_texture(texture);
			job.set_input_alpha_association(
				OAK_CONFIG("ReassocLinToNonLin").toBool() ? k_alpha_associated :
															  k_alpha_none);

			render_ctx_->blit_color_managed(job, transform_tex.get());

			texture = transform_tex;
		}

		if (tex_params.effective_width() != frame_params.effective_width() ||
			tex_params.effective_height() != frame_params.effective_height() ||
			tex_params.format() != frame_params.format()) {
			TexturePtr blit_tex = render_ctx_->create_texture(frame_params);

			Matrix4x4 matrix = ticket_->property("matrix").value<Matrix4x4>();

			// No color transform, just blit
			ShaderJob job;
			job.insert("ove_maintex",
					   NodeValue(NodeValue::k_texture,
								 Variant::from_value(texture)));
			job.insert("ove_mvpmat",
					   NodeValue(NodeValue::k_matrix, matrix));

			render_ctx_->blit_to_texture(render_ctx_->get_default_shader(), job,
									   blit_tex.get());

			// Replace texture that we're going to download in the next step
			texture = blit_tex;
		}

		render_ctx_->download_from_texture(texture->id(), texture->params(),
										 frame->data(),
										 frame->linesize_pixels());
		if (output_color_transform) {
			VideoParams display_params = frame->video_params();
			display_params.set_colorspace(std::string("display:") +
										  output_color_transform->id());
			frame->set_video_params(display_params);
		}
	}

	return frame;
}

void RenderProcessor::run()
{
	// Depending on the render ticket type, start a job
	RenderManager::TicketType type =
		RenderManager::TicketType(ticket_->property("type").to_int());

	set_cancel_pointer(ticket_->get_cancel_atom());

	VideoParams params = ticket_->property("vparam").value<VideoParams>();
	params.set_format(PixelFormat::f32);
	set_cache_video_params(params);
	set_cache_audio_params(ticket_->property("aparam").value<AudioParams>());

	if (is_cancelled()) {
		ticket_->finish();
		return;
	}

	// if is a plugin
	/*Node *node=ticket_->property("node").value<Node*>();
	if (node && node->getPlugin()) {
		std::shared_ptr<OFX::Host::ImageEffect::ImageEffectPlugin> plugin
			= node->getPlugin();
		std::unique_ptr<OFX::Host::ImageEffect::Instance> instance(plugin->createInstance(kOfxImageEffectContextFilter, NULL));


	}
	*/

	switch (type) {
	case RenderManager::k_type_video: {
		Rational time = ticket_->property("time").value<Rational>();

		Rational frame_length = get_cache_video_params().frame_rate_as_time_base();
		if (get_cache_video_params().interlacing() !=
			VideoParams::k_interlace_none) {
			frame_length /= 2;
		}

		TexturePtr texture = generate_texture(time, frame_length);

		if (!render_ctx_) {
			ticket_->finish();
		} else {
			if (get_cache_video_params().interlacing() !=
				VideoParams::k_interlace_none) {
				// Get next between frame and interlace it
				TexturePtr top = texture;
				TexturePtr bottom =
					generate_texture(time + frame_length, frame_length);

				if (get_cache_video_params().interlacing() ==
					VideoParams::k_interlaced_bottom_first) {
					std::swap(top, bottom);
				}

				texture = render_ctx_->interlace_texture(top, bottom,
														get_cache_video_params());
			}

			if (heard_cancel()) {
				// Finish cancelled ticket with nothing since we can't guarantee the frame we generated
				// is actually "complete
				ticket_->finish();
			} else {
				FramePtr frame;
				std::string cache = ticket_->property("cache").to_string();
				RenderManager::ReturnType return_type =
					RenderManager::ReturnType(
						ticket_->property("return").to_int());

				if (return_type == RenderManager::k_frame || !cache.empty()) {
					// Convert to CPU frame
					frame = generate_frame(texture, time);

					// Save to cache if requested
					if (!cache.empty()) {
						Rational timebase =
							ticket_->property("cachetimebase").value<Rational>();
						std::string uuid =
							ticket_->property("cacheid").value<std::string>();
						bool cache_result = FrameHashCache::save_cache_frame(
							cache, uuid, time, timebase, frame);
						ticket_->set_property("cached", cache_result);
					}
				}

				if (return_type == RenderManager::k_texture) {
					// Return GPU texture
					if (!texture) {
						texture =
							render_ctx_->create_texture(get_cache_video_params());
						render_ctx_->clear_destination(texture.get());
					}

					render_ctx_->flush();
					ticket_->finish(Variant::from_value(texture));
				} else {
					ticket_->finish(Variant::from_value(frame));
				}
			}
		}
		break;
	}
	case RenderManager::k_type_audio: {
		TimeRange time = ticket_->property("time").value<TimeRange>();

		NodeValueTable table;
		if (Node *node = ticket_->property("node").value<Node *>()) {
			table = generate_table(node, time);
		}

		NodeValue sample_val = table.get(NodeValue::k_samples);

		resolve_jobs(sample_val);

		SampleBuffer samples = sample_val.to_samples();
		if (samples.is_allocated()) {
			if (ticket_->property("clamp").to_bool() && !is_cancelled()) {
				samples.clamp();
			}

			if (ticket_->property("enablewaveforms").to_bool() &&
				!is_cancelled()) {
				AudioVisualWaveform vis;
				vis.set_channel_count(samples.audio_params().channel_count());
				vis.overwrite_samples(samples,
									 samples.audio_params().sample_rate());
				ticket_->set_property("waveform", Variant::from_value(vis));
			}
		}

		if (heard_cancel()) {
			ticket_->finish();
		} else {
			ticket_->finish(Variant::from_value(samples));
		}
		break;
	}
	default:
		// Fail
		ticket_->finish();
	}
}

DecoderPtr
RenderProcessor::resolve_decoder_from_input(const std::string &decoder_id,
									 const Decoder::CodecStream &stream)
{
	if (!stream.is_valid()) {
		fprintf(stderr, "Attempted to resolve the decoder of a null stream\n");
		return nullptr;
	}

	if (!decoder_cache_) {
		fprintf(stderr, "Cannot resolve decoder for %s without a decoder cache\n",
				stream.filename().c_str());
		return nullptr;
	}

	std::unique_lock<std::mutex> locker(decoder_cache_->mutex());

	// std::map-based cache: a missing stream yields a default DecoderPair
	auto cache_it = decoder_cache_->find(stream);
	DecoderPair decoder =
		cache_it == decoder_cache_->end() ? DecoderPair() : cache_it->second;

	int64_t file_last_modified = file_last_modified_ms(stream.filename());

	DecoderPtr dec = nullptr;

	if (decoder.decoder && decoder.last_modified == file_last_modified) {
		dec = decoder.decoder;
	} else {
		// No decoder
		decoder.decoder = dec = Decoder::create_from_id(decoder_id);
		decoder.last_modified = file_last_modified;
		decoder_cache_->insert_or_assign(stream, decoder);
		locker.unlock();

		if (!dec->open(stream)) {
			fprintf(stderr, "Failed to open decoder for %s::%d\n",
					stream.filename().c_str(), stream.stream());
			return nullptr;
		}

		if (!render_ctx_) {
			// Assume dry run and increment access time
			decoder.decoder->increment_access_time(
				RenderManager::k_dry_run_interval.to_double() * 1000);
		}
	}

	return dec;
}

NodeValueDatabase RenderProcessor::generate_database(const Node *node,
													const TimeRange &range)
{
	NodeValueDatabase db = super::generate_database(node, range);

	if (const MultiCamNode *multicam =
			dynamic_cast<const MultiCamNode *>(node)) {
		if (ticket_->property("multicam").value<MultiCamNode *>() == multicam) {
			int sz = multicam->get_source_count();
			std::vector<void *> multicam_tex(sz);
			for (int i = 0; i < sz; i++) {
				NodeValueTable t =
					generate_table(multicam->get_connected_render_output(
									  multicam->k_sources_input, i),
								  range, multicam);
				NodeValue val = generate_row_value_element(
					multicam, multicam->k_sources_input, i, &t, range);
				resolve_jobs(val);

				TexturePtr tp = val.to_texture();
				// Store as opaque retained handle for the C ABI app layer
				multicam_tex[i] = oakrender_internal_wrap_texture(tp);
			}
			ticket_->set_property("multicam_output",
								 Variant::from_value(multicam_tex));
		}
	}

	return db;
}

void RenderProcessor::process(RenderTicketPtr ticket, Renderer *render_ctx,
							  DecoderCache *decoder_cache,
							  ShaderCache *shader_cache)
{
	RenderProcessor p(ticket, render_ctx, decoder_cache, shader_cache);
	p.run();
}

void RenderProcessor::process_video_footage(TexturePtr destination,
										  const FootageJob *stream,
										  const Rational &input_time)
{
	if (RenderManager::TicketType(ticket_->property("type").to_int()) !=
		RenderManager::k_type_video) {
		// Video cannot contribute to audio, so we do nothing here
		return;
	}

	// Check the still frame cache. On large frames such as high resolution still images, uploading
	// and color managing them for every frame is a waste of time, so we implement a small cache here
	// to optimize such a situation
	VideoParams stream_data = stream->video_params();

	ColorManager *color_manager =
		ticket_->property("colormanager").value<ColorManager *>();

	std::string using_colorspace = stream_data.colorspace();

	if (using_colorspace.empty() && color_manager) {
		using_colorspace = color_manager->get_default_input_color_space();
	}

	if (using_colorspace.empty()) {
		fprintf(stderr,
				"RenderProcessor ProcessVideoFootage: no input colorspace "
				"available\n");
	}

	auto blit_color_managed = [&](const TexturePtr &unmanaged_texture,
								  const VideoParams &texture_params) {
		if (!render_ctx_ || !unmanaged_texture || is_cancelled()) {
			return;
		}

		// We convert to our rendering pixel format, since that will always be float-based which
		// is necessary for correct color conversion
		ColorProcessorPtr processor =
			ColorProcessor::create(color_manager, using_colorspace,
								   color_manager->get_reference_color_space());

		ColorTransformJob job;
		job.set_color_processor(processor);
		job.set_input_texture(unmanaged_texture);

		if (texture_params.channel_count() != VideoParams::k_rgba_channel_count ||
			texture_params.colorspace() ==
				color_manager->get_reference_color_space()) {
			job.set_input_alpha_association(k_alpha_none);
		} else if (texture_params.premultiplied_alpha()) {
			job.set_input_alpha_association(k_alpha_associated);
		} else {
			job.set_input_alpha_association(k_alpha_unassociated);
		}

		render_ctx_->blit_color_managed(job, destination.get());
		// macOS TBDR: ensure tile writeback completes before the texture
		// is read back in a potentially different shared OpenGL context.
		render_ctx_->flush();
	};

	auto *input_pool =
		ticket_->property("ipc_input_pool").value<ipc::FrameSlotPool *>();
	int input_slot = -1;
	const std::vector<int> input_slots =
		ticket_->property("ipc_input_slots").value<std::vector<int>>();
	if (!input_slots.empty()) {
		const Variant cursor_value = ticket_->property("ipc_input_slot_cursor");
		const int cursor = !cursor_value.is_null() ? cursor_value.to_int() : 0;
		if (cursor >= 0 && cursor < int(input_slots.size())) {
			input_slot = input_slots[size_t(cursor)];
			ticket_->set_property("ipc_input_slot_cursor", int64_t(cursor + 1));
		}
	} else {
		const Variant input_slot_value = ticket_->property("ipc_input_slot");
		input_slot = !input_slot_value.is_null() ? input_slot_value.to_int() : -1;
	}
	if (render_ctx_ && input_pool && input_slot >= 0) {
		if (input_slot >= int(input_pool->slot_count())) {
			fprintf(stderr,
					"RenderProcessor received out-of-range IPC input frame slot "
					"%d\n",
					input_slot);
			return;
		}

		const ipc::FrameSlotMeta *meta = input_pool->meta(uint32_t(input_slot));
		if (meta && meta->width > 0 && meta->height > 0 &&
			meta->data_size > 0 &&
			meta->data_size <= int(input_pool->slot_data_bytes())) {
			VideoParams input_params = stream_data;
			input_params.set_width(meta->width);
			input_params.set_height(meta->height);
			input_params.set_format(PixelFormat::Format(meta->format));
			input_params.set_channel_count(meta->channel_count);
			// The decoder may leave depth at 0 for 2D frames, but the renderer
			// needs depth >= 1 to compute image size and upload the texture.
			if (input_params.depth() <= 0) {
				input_params.set_depth(1);
			}

			// Prefer the colorspace that the main process used when decoding this
			// frame. The FootageJob reconstructed in the worker may have stale or
			// empty colorspace if the project snapshot was saved before stream
			// metadata was fully resolved.
			const std::string ipc_colorspace(meta->colorspace);
			if (!ipc_colorspace.empty()) {
				input_params.set_colorspace(ipc_colorspace);
				using_colorspace = ipc_colorspace;
			}

			const int bytes_per_pixel = input_params.get_bytes_per_pixel();
			const int linesize_pixels = bytes_per_pixel > 0 ?
											meta->linesize / bytes_per_pixel :
											input_params.effective_width();

			const void *slot_data = input_pool->slot_data(uint32_t(input_slot));
			TexturePtr unmanaged_texture = render_ctx_->create_texture(
				input_params, slot_data, linesize_pixels);

			blit_color_managed(unmanaged_texture, input_params);
			return;
		}
		fprintf(stderr,
				"RenderProcessor received invalid IPC input frame slot %d\n",
				input_slot);
		return;
	}

	if (!decoder_cache_) {
		fprintf(stderr,
				"RenderProcessor has no decoder cache or IPC input frame for %s\n",
				stream->filename().c_str());
		return;
	}

	const bool use_proxy = stream->should_use_proxy(
		static_cast<RenderMode::Mode>(ticket_->property("mode").to_int()));
	const std::string decode_filename = use_proxy ? stream->proxy_filename() :
													stream->filename();
	const std::string decoder_id = use_proxy ? stream->proxy_decoder() :
											   stream->decoder();
	const int stream_index = use_proxy ? stream->proxy_stream_index() :
										 stream_data.stream_index();

	Decoder::CodecStream default_codec_stream(decode_filename, stream_index,
											  get_current_block());

	DecoderPtr decoder = nullptr;

	switch (stream_data.video_type()) {
	case VideoParams::k_video_type_video:
	case VideoParams::k_video_type_still:
		decoder = resolve_decoder_from_input(decoder_id, default_codec_stream);
		break;
	case VideoParams::k_video_type_image_sequence: {
		if (render_ctx_) {
			// Since image sequences involve multiple files, we don't engage the decoder cache
			decoder = Decoder::create_from_id(decoder_id);

			std::string frame_filename;

			int64_t frame_number =
				stream_data.get_time_in_timebase_units(input_time);
			frame_filename = Decoder::transform_image_sequence_file_name(
				decode_filename, frame_number);

			// Decoder will close automatically since it's a stream_ptr
			decoder->open(Decoder::CodecStream(frame_filename, stream_index,
											   get_current_block()));
		}
		break;
	}
	}

	if (decoder && render_ctx_) {
		Decoder::RetrieveVideoParams p;
		p.divider = stream->video_params().divider();
		p.maximum_format = destination->format();

		if (!is_cancelled()) {
			VideoParams tex_params = stream->video_params();

			if (tex_params.is_valid()) {
				TexturePtr unmanaged_texture;

				p.renderer = render_ctx_;
				p.time =
					(stream_data.video_type() == VideoParams::k_video_type_video) ?
						input_time :
						Decoder::k_any_timecode;
				p.cancelled = get_cancel_pointer();
				p.force_range = stream_data.color_range();
				p.src_interlacing = stream_data.interlacing();

				unmanaged_texture = decoder->retrieve_video(p);

				if (!is_cancelled() && unmanaged_texture) {
					blit_color_managed(unmanaged_texture, stream_data);
				}
			}
		}
	}
}

void RenderProcessor::process_audio_footage(SampleBuffer &destination,
										  const FootageJob *stream,
										  const TimeRange &input_time)
{
	// The worker process has no decoder cache and does not decode audio. Bail
	// out gracefully rather than letting ResolveDecoderFromInput crash.
	if (!decoder_cache_) {
		return;
	}

	// Mirror the video path: use the proxy (when enabled, ready, and containing
	// audio) for offline renders only, never for export
	const bool use_proxy = stream->should_use_proxy(
		static_cast<RenderMode::Mode>(ticket_->property("mode").to_int()));
	const std::string decode_filename = use_proxy ? stream->proxy_filename() :
													stream->filename();
	const std::string decoder_id = use_proxy ? stream->proxy_decoder() :
											   stream->decoder();
	const int stream_index = use_proxy ?
								 stream->proxy_stream_index() :
								 stream->audio_params().stream_index();

	DecoderPtr decoder = resolve_decoder_from_input(
		decoder_id,
		Decoder::CodecStream(decode_filename, stream_index, nullptr));

	if (decoder) {
		const AudioParams &audio_params = get_cache_audio_params();

		Decoder::RetrieveAudioStatus status = decoder->retrieve_audio(
			destination, input_time, audio_params, stream->cache_path(),
			loop_mode(),
			static_cast<RenderMode::Mode>(ticket_->property("mode").to_int()));

		if (status == Decoder::k_waiting_for_conform) {
			ticket_->set_property("incomplete", true);
		}
	}
}

void RenderProcessor::process_shader(TexturePtr destination, const Node *node,
									const ShaderJob *job)
{
	if (!render_ctx_) {
		return;
	}

	std::string full_shader_id = node->id() + ":" + job->get_shader_id();

	std::unique_lock<std::mutex> locker(shader_cache_->mutex());

	Variant shader;
	{
		auto it = shader_cache_->find(full_shader_id);
		if (it != shader_cache_->end()) {
			shader = it->second;
		}
	}

	if (shader.is_null()) {
		// Since we have shader code, compile it now
		shader = render_ctx_->create_native_shader(
			node->get_shader_code(job->get_shader_id()));

		if (shader.is_null()) {
			// Couldn't find or build the shader required
			return;
		}

		shader_cache_->insert_or_assign(full_shader_id, shader);
	}

	locker.unlock();

	// Run shader
	render_ctx_->blit_to_texture(shader, const_cast<ShaderJob &>(*job),
							   destination.get());
}

void RenderProcessor::process_samples(SampleBuffer &destination,
									 const Node *node, const TimeRange &range,
									 const SampleJob &job)
{
	if (!job.samples().is_allocated()) {
		return;
	}

	NodeValueRow value_db;

	const AudioParams &audio_params = get_cache_audio_params();

	for (size_t i = 0; i < job.samples().sample_count(); i++) {
		// Calculate the exact Rational time at this sample
		double sample_to_second =
			static_cast<double>(i) /
			static_cast<double>(audio_params.sample_rate());

		Rational this_sample_time =
			Rational::from_double(range.in().to_double() + sample_to_second);

		// Update all non-sample and non-footage inputs
		for (auto j = job.get_values().cbegin(); j != job.get_values().cend();
			 j++) {
			TimeRange r = TimeRange(this_sample_time, this_sample_time);
			NodeValueTable value = process_input(node, j->first, r);

			value_db[j->first] = generate_row_value(node, j->first, &value, r);
		}

		node->process_samples(value_db, job.samples(), destination, i);
	}
}

void RenderProcessor::process_color_transform(TexturePtr destination,
											const Node *node,
											const ColorTransformJob *job)
{
	if (!render_ctx_) {
		return;
	}

	render_ctx_->blit_color_managed(*job, destination.get());
}

void RenderProcessor::process_frame_generation(TexturePtr destination,
											 const Node *node,
											 const GenerateJob *job)
{
	if (!render_ctx_) {
		return;
	}

	FramePtr frame = Frame::create();

	frame->set_video_params(destination->params());
	frame->allocate();

	node->generate_frame(frame, *job);

	destination->upload(frame->data(), frame->linesize_pixels());
}

TexturePtr RenderProcessor::process_plugin_job(TexturePtr texture,
											 TexturePtr destination,
											 const Node *node)
{
	(void)node;

	if (!render_ctx_ || !texture || !destination) {
		return destination;
	}

	auto *plugin_job = dynamic_cast<plugin::PluginJob *>(texture->job());
	if (!plugin_job) {
		return destination;
	}

	plugin::PluginRenderer plugin_renderer(render_ctx_);
	if (!plugin_renderer.renderer()) {
		return destination;
	}

	NodeValueRow &values = plugin_job->get_values();

	// QHash::value() semantics: a missing key yields a default NodeValue
	auto find_value = [](const NodeValueRow &row,
						 const std::string &key) -> NodeValue {
		auto it = row.find(key);
		return it == row.end() ? NodeValue() : it->second;
	};

	auto is_usable_texture = [](const TexturePtr &tex) {
		if (!tex) {
			return false;
		}
		if (!tex->is_dummy() && tex->renderer()) {
			return true;
		}
		AVFramePtr frame = tex->frame();
		return frame && frame->data(0);
	};

	TexturePtr src = nullptr;
	std::string effect_input_id;
	if (plugin_job->node()) {
		effect_input_id = plugin_job->node()->get_effect_input_id();
	}
	if (!effect_input_id.empty()) {
		if (TexturePtr effect_tex = find_value(values, effect_input_id).to_texture();
			is_usable_texture(effect_tex)) {
			src = effect_tex;
		}
	}
	if (!src) {
		const std::string source_key(kOfxImageEffectSimpleSourceClipName);
		if (TexturePtr source_tex = find_value(values, source_key).to_texture();
			is_usable_texture(source_tex)) {
			src = source_tex;
		} else if (TexturePtr effect_tex =
					   find_value(values, plugin::k_texture_input).to_texture();
				   is_usable_texture(effect_tex)) {
			src = effect_tex;
		}
	}
	if (!src) {
		for (auto it = values.cbegin(); it != values.cend(); ++it) {
			if (it->second.type() == NodeValue::k_texture) {
				if (TexturePtr any_tex = it->second.to_texture();
					is_usable_texture(any_tex)) {
					src = any_tex;
					break;
				}
			}
		}
	}

	plugin_renderer.render_plugin(src, *plugin_job, destination,
								 destination->params(), true, false);

	return destination;
}

TexturePtr RenderProcessor::process_video_cache_job(const CacheJob *val)
{
	FramePtr frame = FrameHashCache::load_cache_frame(val->get_filename());
	if (frame) {
		// Auto-detect and discard black/empty cached frames (macOS TBDR artifact)
		bool all_black = true;
		if (frame->data() && frame->allocated_size() > 0) {
			const uint8_t *pixels =
				reinterpret_cast<const uint8_t *>(frame->data());
			size_t alloc_size = static_cast<size_t>(frame->allocated_size());
			size_t check_bytes = std::min(alloc_size, size_t(4096));
			for (size_t i = 0; i < check_bytes; ++i) {
				if (pixels[i] != 0) {
					all_black = false;
					break;
				}
			}
		}
		if (all_black) {
			fprintf(stderr,
					"[CACHE] Discarding black cached frame: %s time=%f "
					"size=%lld\n",
					val->get_filename().c_str(), frame->timestamp().to_double(),
					(long long)frame->allocated_size());
			std::error_code ec;
			std::filesystem::remove(val->get_filename(), ec);
			return nullptr;
		}

		TexturePtr tex = create_texture(frame->video_params());
		if (tex) {
			tex->upload(frame->data(), frame->linesize_pixels());
			return tex;
		}
	} else {
		StringList s = ticket_->property("badcache").to_string_list();
		s.push_back(val->get_filename());
		ticket_->set_property("badcache", Variant::from_value(s));
	}

	return nullptr;
}

TexturePtr RenderProcessor::create_texture(const VideoParams &p)
{
	if (render_ctx_) {
		return render_ctx_->create_texture(p);
	} else {
		return super::create_texture(p);
	}
}

void RenderProcessor::convert_to_reference_space(TexturePtr destination,
											  TexturePtr source,
											  const std::string &input_cs)
{
	if (!render_ctx_) {
		return;
	}

	ColorManager *color_manager =
		ticket_->property("colormanager").value<ColorManager *>();
	ColorProcessorPtr cp = ColorProcessor::create(
		color_manager, input_cs, color_manager->get_reference_color_space());

	ColorTransformJob ctj;

	ctj.set_color_processor(cp);
	ctj.set_input_texture(source);
	ctj.set_input_alpha_association(k_alpha_associated);

	render_ctx_->blit_color_managed(ctj, destination.get());
}

bool RenderProcessor::use_cache() const
{
	return static_cast<RenderMode::Mode>(ticket_->property("mode").to_int()) ==
		   RenderMode::k_offline;
}

}
