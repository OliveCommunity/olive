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

#ifndef OAK_RENDERPROCESSOR_H
#define OAK_RENDERPROCESSOR_H

#include "node/block/clip/clip.h"
#include <memory>
#include "node/traverser.h"
#include "render/renderer.h"
#include "rendercache.h"
#include "renderticket.h"

namespace olive
{

namespace plugin
{
class PluginRenderer;
}

class RenderProcessor : public NodeTraverser {
public:
	virtual NodeValueDatabase generate_database(const Node *node,
											   const TimeRange &range) override;

	static void process(RenderTicketPtr ticket, Renderer *render_ctx,
						DecoderCache *decoder_cache, ShaderCache *shader_cache);

	struct RenderedWaveform {
		const ClipBlock *block;
		AudioVisualWaveform waveform;
		TimeRange range;
		bool silence;
	};

protected:
	virtual void process_video_footage(TexturePtr destination,
									 const FootageJob *stream,
									 const Rational &input_time) override;

	virtual void process_audio_footage(SampleBuffer &destination,
									 const FootageJob *stream,
									 const TimeRange &input_time) override;

	virtual void process_shader(TexturePtr destination, const Node *node,
							   const ShaderJob *job) override;

	virtual void process_samples(SampleBuffer &destination, const Node *node,
								const TimeRange &range,
								const SampleJob &job) override;

	virtual void process_color_transform(TexturePtr destination, const Node *node,
									   const ColorTransformJob *job) override;

	virtual void process_frame_generation(TexturePtr destination,
										const Node *node,
										const GenerateJob *job) override;

	virtual TexturePtr process_plugin_job(TexturePtr texture,
										TexturePtr destination,
										const Node *node) override;

	virtual TexturePtr process_video_cache_job(const CacheJob *val) override;

	virtual TexturePtr create_texture(const VideoParams &p) override;

	virtual SampleBuffer create_sample_buffer(const AudioParams &params,
											int sample_count) override
	{
		return SampleBuffer(params, sample_count);
	}

	virtual void convert_to_reference_space(TexturePtr destination,
										 TexturePtr source,
										 const QString &input_cs) override;

	virtual bool use_cache() const override;

private:
	RenderProcessor(RenderTicketPtr ticket, Renderer *render_ctx,
					DecoderCache *decoder_cache, ShaderCache *shader_cache);

	TexturePtr generate_texture(const Rational &time,
							   const Rational &frame_length);

	FramePtr generate_frame(TexturePtr texture, const Rational &time);

	void run();

	DecoderPtr resolve_decoder_from_input(const QString &decoder_id,
									   const Decoder::CodecStream &stream);

	RenderTicketPtr ticket_;

	Renderer *render_ctx_;

	std::unique_ptr<olive::plugin::PluginRenderer> plugin_renderer_;

	DecoderCache *decoder_cache_;

	ShaderCache *shader_cache_;
};

}

Q_DECLARE_METATYPE(olive::RenderProcessor::RenderedWaveform)

#endif // OAK_RENDERPROCESSOR_H
