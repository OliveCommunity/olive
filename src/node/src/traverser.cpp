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

#include "traverser.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "node.h"
#include "block/clip/clip.h"
#include "project/footage/footage.h"
#include "render/job/footagejob.h"
#include "render/rendermanager.h"
#include "render/job/pluginjob.h"

namespace olive
{

NodeValueDatabase NodeTraverser::generate_database(const Node *node,
												  const TimeRange &range)
{
	NodeValueDatabase database;

	// HACK: Pick up loop mode from clips
	LoopMode old_loop_mode = loop_mode_;
	if (const ClipBlock *clip = dynamic_cast<const ClipBlock *>(node)) {
		loop_mode_ = clip->loop_mode();
	}

	// We need to insert tables into the database for each input
	auto ignore = node->ignore_inputs_for_rendering();
	for (const std::string &input : node->inputs()) {
		if (is_cancelled()) {
			return NodeValueDatabase();
		}

		if (std::find(ignore.begin(), ignore.end(), input) != ignore.end()) {
			continue;
		}

		database.insert(input, process_input(node, input, range));
	}

	loop_mode_ = old_loop_mode;

	return database;
}

NodeValueRow NodeTraverser::generate_row(NodeValueDatabase *database,
										const Node *node,
										const TimeRange &range)
{
	// Generate row
	NodeValueRow row;
	for (auto it = database->begin(); it != database->end(); it++) {
		// Get hint for which value should be pulled
		NodeValue value = generate_row_value(node, it->first, &it->second, range);
		row.insert({ it->first, value });
	}

	// TEMP: Audio needs to be refactored to work with new job system. But refactoring hasn't been
	//       done yet, so we emulate old behavior here JUST FOR AUDIO.
	for (auto it = row.begin(); it != row.end(); it++) {
		NodeValue &val = it->second;
		if (val.type() == NodeValue::k_samples) {
			resolve_jobs(val);
		}
	}
	// END TEMP

	return row;
}

NodeValueRow NodeTraverser::generate_row(const Node *node,
										const TimeRange &range)
{
	// Generate database of input values of node
	NodeValueDatabase database = generate_database(node, range);

	return generate_row(&database, node, range);
}

NodeValue NodeTraverser::generate_row_value(const Node *node,
										  const std::string &input,
										  NodeValueTable *table,
										  const TimeRange &time)
{
	NodeValue value = generate_row_value_element(node, input, -1, table, time);

	if (value.array()) {
		// Resolve each element of array
		NodeValueTableArray tables = value.value<NodeValueTableArray>();
		NodeValueArray output;

		for (auto it = tables.begin(); it != tables.end(); it++) {
			output[it->first] = generate_row_value_element(node, input, it->first,
														&it->second, time);
		}

		value = NodeValue(value.type(), output,
						  value.source(), value.array(), value.tag());
	}

	return value;
}

NodeValue NodeTraverser::generate_row_value_element(const Node *node,
												 const std::string &input,
												 int element,
												 NodeValueTable *table,
												 const TimeRange &time)
{
	int value_index =
		generate_row_value_element_index(node->get_value_hint_for_input(input, element),
									 node->get_input_data_type(input), table);

	if (value_index == -1) {
		// If value was -1, try getting the last value
		value_index = table->count() - 1;
	}

	if (value_index == -1) {
		// If value is still -1, assume the table is empty and return nothing
		return NodeValue();
	}

	NodeValue value = table->take_at(value_index);

	if (value.type() == NodeValue::k_texture && use_cache()) {
		if (TexturePtr tex = value.to_texture()) {
			std::lock_guard<std::mutex> locker(node->video_frame_cache()->mutex());

			node->video_frame_cache()->load_state();

			std::string cache =
				node->video_frame_cache()->get_valid_cache_filename(time.in());
			if (!cache.empty()) {
				value.set_value(tex->to_job(CacheJob(cache, value)));
			}
		}
	}

	return value;
}

int NodeTraverser::generate_row_value_element_index(const Node::ValueHint &hint,
												NodeValue::Type preferred_type,
												const NodeValueTable *table)
{
	std::vector<NodeValue::Type> types = hint.types();

	if (types.empty()) {
		types.push_back(preferred_type);
	}

	if (hint.index() == -1) {
		// Get most recent value with this type and tag
		return table->get_value_index(types, hint.tag());
	} else {
		// Try to find value at this index
		int index = table->count() - 1 - hint.index();
		int diff = 0;

		while (index + diff < table->count() && index - diff >= 0) {
			if (index + diff < table->count() &&
				std::find(types.begin(), types.end(),
						  table->at(index + diff).type()) != types.end()) {
				return index + diff;
			}
			if (index - diff >= 0 &&
				std::find(types.begin(), types.end(),
						  table->at(index - diff).type()) != types.end()) {
				return index - diff;
			}
			diff++;
		}

		return -1;
	}
}

int NodeTraverser::generate_row_value_element_index(const Node *node,
												const std::string &input,
												int element,
												const NodeValueTable *table)
{
	return generate_row_value_element_index(node->get_value_hint_for_input(input,
																   element),
										node->get_input_data_type(input), table);
}

void NodeTraverser::transform(Matrix4x4 *transform, const Node *start,
							  const Node *end, const TimeRange &range)
{
	transform_ = transform;
	transform_start_ = start;
	transform_now_ = nullptr;

	generate_table(end, range);

	transform_ = nullptr;
}

NodeValueTable NodeTraverser::process_input(const Node *node,
										   const std::string &input,
										   const TimeRange &range)
{
	// If input is connected, retrieve value directly
	if (node->is_input_connected_for_render(input)) {
		TimeRange adjusted_range =
			node->input_time_adjustment(input, -1, range, true);

		// Value will equal something from the connected node, follow it
		Node *output = node->get_connected_render_output(input);
		NodeValueTable table = generate_table(output, adjusted_range, node);
		return table;

	} else {
		// Store node
		Variant return_val;
		bool is_array = node->input_is_array(input);

		if (is_array) {
			// Value is an array, we will return a list of NodeValueTables
			NodeValueTableArray array_tbl;

			Node::ActiveElements a =
				node->get_active_elements_at_time(input, range);
			if (a.mode() == Node::ActiveElements::k_all_elements) {
				int sz = node->input_array_size(input);
				for (int i = 0; i < sz; i++) {
					process_input_element(array_tbl, node, input, i, range);
				}
			} else if (a.mode() == Node::ActiveElements::k_specified) {
				for (int ele : a.elements()) {
					process_input_element(array_tbl, node, input, ele, range);
				}
			}

			return_val = Variant::from_value(array_tbl);

		} else {
			// Not connected or an array, just pull the immediate
			TimeRange adjusted_range =
				node->input_time_adjustment(input, -1, range, true);

			return_val = node->get_value_at_time(input, adjusted_range.in());
		}

		NodeValueTable return_table;
		return_table.push(node->get_input_data_type(input), return_val, node,
						  is_array);
		return return_table;
	}
}

void NodeTraverser::process_input_element(NodeValueTableArray &array_tbl,
										const Node *node, const std::string &input,
										int element, const TimeRange &range)
{
	NodeValueTable &sub_tbl = array_tbl[element];
	TimeRange adjusted_range =
		node->input_time_adjustment(input, element, range, true);

	if (node->is_input_connected_for_render(input, element)) {
		Node *output = node->get_connected_render_output(input, element);
		sub_tbl = generate_table(output, adjusted_range, node);
	} else {
		Variant input_value =
			node->get_value_at_time(input, adjusted_range.in(), element);
		sub_tbl.push(node->get_input_data_type(input), input_value, node);
	}
}

NodeTraverser::NodeTraverser()
	: cancel_(nullptr)
	, transform_(nullptr)
	, loop_mode_(LoopMode::k_loop_mode_off)
{
}

class GTTTime {
public:
	GTTTime(const Node *n)
	{
		t = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
				.count();
		node = n;
	}

	~GTTTime()
	{
		fprintf(stderr, "GT for %p took %lld\n", (const void *)node,
				(long long)(std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch())
								.count() -
							t));
	}

	int64_t t;
	const Node *node;
};

NodeValueTable NodeTraverser::generate_table(const Node *n,
											const TimeRange &range,
											const Node *next_node)
{
	// NOTE: Times how long a node takes to process, useful for profiling.
	//GTTTime gtt(n);(void)gtt;

	// Use table cache to skip processing where available
	auto cache_it = value_cache_.find(n);
	if (cache_it != value_cache_.end()) {
		auto &node_value_map = cache_it->second;
		auto range_it = node_value_map.find(range);
		if (range_it != node_value_map.end()) {
			return range_it->second;
		}
	}

	// Generate row for node
	NodeValueDatabase database = generate_database(n, range);

	// Check for bypass
	bool is_enabled;
	if (!database[Node::k_enabled_input].has(NodeValue::k_boolean)) {
		// Fallback if we couldn't find a bool value
		is_enabled = true;
	} else {
		is_enabled =
			database[Node::k_enabled_input].get(NodeValue::k_boolean).to_bool();
	}

	NodeValueTable table;

	if (is_enabled) {
		NodeValueRow row = generate_row(&database, n, range);

		// Generate output table
		table = database.merge();

		// By this point, the node should have all the inputs it needs to render correctly
		NodeGlobals globals(video_params_, audio_params_, range, loop_mode_);
		n->value(row, globals, &table);

		// `transform_now_` is the next node in the path that needs to be traversed. It only ever goes
		// "down" the graph so that any traversing going back up doesn't unnecessarily transform
		// from unrelated nodes or the same node twice
		if (transform_) {
			if (transform_now_ == n || transform_start_ == n) {
				if (transform_now_ == n) {
					Matrix4x4 t = n->gizmo_transformation(row, globals);
					if (!t.is_identity()) {
						(*transform_) *= t;
					}
				}

				transform_now_ = next_node;
			}
		}
	} else {
		// If this node has an effect input, ensure that is pushed last
		NodeValueTable primary;
		if (!n->get_effect_input_id().empty()) {
			primary = database.take(n->get_effect_input_id());
		}

		table = database.merge();
		table.push(primary);
	}

	value_cache_[n][range] = table;

	return table;
}

TexturePtr NodeTraverser::process_video_cache_job(const CacheJob *val)
{
	return nullptr;
}

TexturePtr NodeTraverser::process_plugin_job(TexturePtr texture,
										   TexturePtr destination,
										   const Node *node)
{
	// TODO
	return nullptr;
}
Vector2D NodeTraverser::generate_resolution() const
{
	return Vector2D(video_params_.square_pixel_width(),
					video_params_.height());
}

/**
 * Resolve Jobs. I need to add a PluginJob here and move the plugin code here.
 * @param val
 */
void NodeTraverser::resolve_jobs(NodeValue &val)
{
	if (val.type() == NodeValue::k_texture) {
		if (TexturePtr job_tex = val.to_texture()) {
			if (AcceleratedJob *base_job = job_tex->job()) {
				auto resolved_it = resolved_texture_cache_.find(job_tex.get());
				if (resolved_it != resolved_texture_cache_.end()) {
					val.set_value(resolved_it->second);
				} else {
					// Resolve any sub-jobs
					for (auto it = base_job->get_values().begin();
						 it != base_job->get_values().end(); it++) {
						// Jobs will almost always be submitted with one of these types
						NodeValue &subval = it->second;
						resolve_jobs(subval);
					}

					if (CacheJob *cj = dynamic_cast<CacheJob *>(base_job)) {
						TexturePtr tex = process_video_cache_job(cj);
						if (tex) {
							val.set_value(tex);
						} else {
							val.set_value(cj->get_fallback());
						}

					} else if (ColorTransformJob *ctj =
								   dynamic_cast<ColorTransformJob *>(
									   base_job)) {
						VideoParams ctj_params = job_tex->params();

						ctj_params.set_format(get_cache_video_params().format());

						TexturePtr dest = create_texture(ctj_params);

						// Resolve input texture
						NodeValue v = ctj->get_input_texture();
						resolve_jobs(v);
						ctj->set_input_texture(v);

						process_color_transform(dest, val.source(), ctj);

						val.set_value(dest);

					} else if (ShaderJob *sj =
								   dynamic_cast<ShaderJob *>(base_job)) {
						VideoParams tex_params = job_tex->params();

						TexturePtr tex = create_texture(tex_params);

						process_shader(tex, val.source(), sj);

						val.set_value(tex);

					} else if (GenerateJob *gj =
								   dynamic_cast<GenerateJob *>(base_job)) {
						VideoParams tex_params = job_tex->params();

						TexturePtr tex = create_texture(tex_params);

						process_frame_generation(tex, val.source(), gj);

						// Convert to reference space
						const std::string &colorspace = tex_params.colorspace();
						if (!colorspace.empty()) {
							// Set format to primary format
							tex_params.set_format(
								get_cache_video_params().format());

							TexturePtr dest = create_texture(tex_params);

							convert_to_reference_space(dest, tex, colorspace);

							tex = dest;
						}

						val.set_value(tex);

					} else if (FootageJob *fj =
								   dynamic_cast<FootageJob *>(base_job)) {
						Rational footage_time = Footage::adjust_time_by_loop_mode(
							fj->time().in(), fj->loop_mode(), fj->length(),
							fj->video_params().video_type(),
							fj->video_params().frame_rate_as_time_base());

						TexturePtr tex;

						if (footage_time.isNaN()) {
							// Push dummy texture
							tex = create_dummy_texture(fj->video_params());
						} else {
							VideoParams managed_params = fj->video_params();
							managed_params.set_format(
								get_cache_video_params().format());

							tex = create_texture(managed_params);
							if (tex) {
								process_video_footage(tex, fj, footage_time);
							}
						}

						val.set_value(tex);
					} else if (dynamic_cast<plugin::PluginJob *>(base_job)) {
						VideoParams tex_params = job_tex->params();
						// Force internal working format (F32) for plugin processing,
						// matching FootageJob/GenerateJob behavior.
						tex_params.set_format(get_cache_video_params().format());
						tex_params.set_channel_count(
							VideoParams::k_rgba_channel_count);

						TexturePtr tex = create_texture(tex_params);

						process_plugin_job(job_tex, tex, val.source());
						val.set_value(tex);
					}

					// Cache resolved value
					resolved_texture_cache_[job_tex.get()] = val.to_texture();
				}
			}
		}

	} else if (val.type() == NodeValue::k_samples) {
		if (val.can_convert<SampleJob>()) {
			SampleJob job = val.value<SampleJob>();
			SampleBuffer output_buffer = create_sample_buffer(
				job.samples().audio_params(), job.samples().sample_count());
			process_samples(output_buffer, val.source(), job.time(), job);
			val.set_value(output_buffer);

		} else if (val.can_convert<FootageJob>()) {
			FootageJob job = val.value<FootageJob>();
			SampleBuffer buffer =
				create_sample_buffer(get_cache_audio_params(), job.time().length());
			process_audio_footage(buffer, &job, job.time());
			val.set_value(buffer);
		}
	}
}

TexturePtr NodeTraverser::create_dummy_texture(const VideoParams &p)
{
	return std::make_shared<Texture>(p);
}

}
