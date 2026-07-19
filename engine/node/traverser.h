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

#ifndef OAK_NODETRAVERSER_H
#define OAK_NODETRAVERSER_H

#include <QVector2D>

#include "codec/decoder.h"
#include "common/cancelableobject.h"
#include "node/output/track/track.h"
#include "render/job/cachejob.h"
#include "render/cancelatom.h"
#include "render/job/footagejob.h"
#include "render/job/colortransformjob.h"
#include "render/job/footagejob.h"
#include "value.h"
#include "render/job/pluginjob.h"

namespace olive
{

class NodeTraverser {
public:
	NodeTraverser();

	NodeValueTable generate_table(const Node *n, const TimeRange &range,
								 const Node *next_node = nullptr);

	virtual NodeValueDatabase generate_database(const Node *node,
											   const TimeRange &range);

	NodeValueRow generate_row(NodeValueDatabase *database, const Node *node,
							 const TimeRange &range);
	NodeValueRow generate_row(const Node *node, const TimeRange &range);

	NodeValue generate_row_value(const Node *node, const QString &input,
							   NodeValueTable *table, const TimeRange &time);
	NodeValue generate_row_value_element(const Node *node, const QString &input,
									  int element, NodeValueTable *table,
									  const TimeRange &time);
	int generate_row_value_element_index(const Node::ValueHint &hint,
									 NodeValue::Type preferred_type,
									 const NodeValueTable *table);
	int generate_row_value_element_index(const Node *node, const QString &input,
									 int element, const NodeValueTable *table);

	void transform(QTransform *transform, const Node *start, const Node *end,
				   const TimeRange &range);

	const VideoParams &get_cache_video_params() const
	{
		return video_params_;
	}

	void set_cache_video_params(const VideoParams &params)
	{
		video_params_ = params;
	}

	const AudioParams &get_cache_audio_params() const
	{
		return audio_params_;
	}

	void set_cache_audio_params(const AudioParams &params)
	{
		audio_params_ = params;
	}

protected:
	NodeValueTable process_input(const Node *node, const QString &input,
								const TimeRange &range);

	void process_input_element(NodeValueTableArray &array_tbl, const Node *node,
							 const QString &input, int element,
							 const TimeRange &range);

	virtual void process_video_footage(TexturePtr destination,
									 const FootageJob *stream,
									 const Rational &input_time)
	{
	}

	virtual void process_audio_footage(SampleBuffer &destination,
									 const FootageJob *stream,
									 const TimeRange &input_time)
	{
	}

	virtual void process_shader(TexturePtr destination, const Node *node,
							   const ShaderJob *job)
	{
	}

	virtual void process_color_transform(TexturePtr destination, const Node *node,
									   const ColorTransformJob *job)
	{
	}

	virtual void process_samples(SampleBuffer &destination, const Node *node,
								const TimeRange &range, const SampleJob &job)
	{
	}

	virtual void process_frame_generation(TexturePtr destination,
										const Node *node,
										const GenerateJob *job)
	{
	}

	virtual void convert_to_reference_space(TexturePtr destination,
										 TexturePtr source,
										 const QString &input_cs)
	{
	}

	virtual TexturePtr process_video_cache_job(const CacheJob *val);

	virtual TexturePtr create_texture(const VideoParams &p)
	{
		return create_dummy_texture(p);
	}

	virtual SampleBuffer create_sample_buffer(const AudioParams &params,
											int sample_count)
	{
		// Return dummy by default
		return SampleBuffer();
	}

	virtual TexturePtr process_plugin_job(TexturePtr texture,
										TexturePtr destination,
										const Node *node);
	SampleBuffer create_sample_buffer(const AudioParams &params,
									const Rational &length)
	{
		if (params.is_valid()) {
			return create_sample_buffer(params, params.time_to_samples(length));
		} else {
			return SampleBuffer();
		}
	}

	QVector2D generate_resolution() const;

	bool is_cancelled()
	{
		return cancel_ && cancel_->is_cancelled();
	}

	bool heard_cancel() const
	{
		return cancel_ && cancel_->heard_cancel();
	}

	CancelAtom *get_cancel_pointer() const
	{
		return cancel_;
	}
	void set_cancel_pointer(CancelAtom *cancel)
	{
		cancel_ = cancel;
	}

	void resolve_jobs(NodeValue &value);
	void resolve_audio_jobs(NodeValue &value);

	Block *get_current_block() const
	{
		return block_stack_.empty() ? nullptr : block_stack_.back();
	}

	LoopMode loop_mode() const
	{
		return loop_mode_;
	}

	virtual bool use_cache() const
	{
		return false;
	}

private:
	TexturePtr create_dummy_texture(const VideoParams &p);

	VideoParams video_params_;

	AudioParams audio_params_;

	CancelAtom *cancel_;

	const Node *transform_start_;
	const Node *transform_now_;
	QTransform *transform_;

	std::list<Block *> block_stack_;

	LoopMode loop_mode_;

	QHash<const Node *, QHash<TimeRange, NodeValueTable>> value_cache_;
	QHash<Texture *, TexturePtr> resolved_texture_cache_;
};

}

#endif // OAK_NODETRAVERSER_H
