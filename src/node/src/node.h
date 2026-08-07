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

#ifndef OAK_NODE_H
#define OAK_NODE_H

#include <algorithm>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "codec/frame.h"
#include "gizmo/draggable.h"
#include "globals.h"
#include "keyframe.h"
#include "inputimmediate.h"
#include "param.h"
#include "ofxhImageEffectAPI.h"
#include "olive/core/util/timerange.h"
#include "render/cache.h"
#include "render/job/generatejob.h"
#include "render/job/samplejob.h"
#include "render/job/shaderjob.h"
#include "render/shadercode.h"
#include "splitvalue.h"
#include "undocommand.h"
#include "xmlutils.h"

namespace olive
{

using core::TimeRange;

#define NODE_DEFAULT_FUNCTIONS(x) \
	NODE_DEFAULT_DESTRUCTOR(x)    \
	NODE_COPY_FUNCTION(x)

#define NODE_DEFAULT_DESTRUCTOR(x) \
	virtual ~x() override          \
	{                              \
		disconnect_all();           \
	}

#define NODE_COPY_FUNCTION(x)           \
	virtual Node *copy() const override \
	{                                   \
		return new x();                 \
	}

class Folder;
class Project;
struct SerializedData;

/**
 * @brief A single processing unit that can be connected with others to create intricate processing systems
 *
 * A cornerstone of "visual programming", a node is a single "function" that takes input and returns an output that can
 * be connected to other nodes. Inputs can be either user-set or retrieved from the output of another node. By joining
 * several nodes together, intricate, highly customizable, and infinitely extensible systems can be made for processing
 * data. It can also all be exposed to the user without forcing them to write code or compile anything.
 *
 * A major example in Olive is the entire rendering workflow. To render a frame, Olive will work through a node graph
 * that can be infinitely customized by the user to create images.
 *
 * This is a simple base class designed to contain all the functionality for this kind of processing connective unit.
 * It is an abstract class intended to be subclassed to create nodes with actual functionality.
 */
class Node {
public:
	enum CategoryID {
		k_category_unknown = -1,

		k_category_output,
		k_category_generator,
		k_category_math,
		k_category_keying,
		k_category_filter,
		k_category_color,
		k_category_time,
		k_category_timeline,
		k_category_transition,
		k_category_distort,
		k_category_project,
		k_category_open_fx,

		k_category_count
	};

	enum Flag {
		k_none = 0,
		k_dont_show_in_param_view = 0x1,
		k_video_effect = 0x2,
		k_audio_effect = 0x4,
		k_dont_show_in_create_menu = 0x8,
		k_is_item = 0x10
	};

	struct ContextPair {
		Node *node;
		Node *context;
	};

	Node();

	virtual ~Node();

	/**
   * @brief Creates a clone of the Node
   *
   * By default, the clone will NOT have the values and connections of the original node. The caller is responsible for
   * copying that data with functions like CopyInputs() as copies may be done for different reasons.
   */
	virtual Node *copy() const = 0;

	/**
   * @brief Convenience function - returns the graph this node was added to
   */
	Project *parent() const
	{
		return parent_;
	}

	/**
   * @brief Set the graph this node belongs to (replaces QObject::setParent)
   */
	void set_parent(Project *p)
	{
		parent_ = p;
	}

	Project *project() const
	{
		return parent_;
	}

	const uint64_t &get_flags() const
	{
		return flags_;
	}

	/**
   * @brief Return the name of the node
   *
   * This is the node's name shown to the user. This must be overridden by subclasses, and preferably run through the
   * translator.
   */
	virtual std::string name() const = 0;

	/**
   * @brief Returns a shortened name of this node if applicable
   *
   * Defaults to returning Name() but can be overridden.
   */
	virtual std::string short_name() const;

	/**
   * @brief Return the unique identifier of the node
   *
   * This is used in save files and any other times a specific node must be picked out at runtime. This must be an ID
   * completely unique to this node, and preferably in bundle identifier format (e.g. "org.company.Name"). This string
   * should NOT be translated.
   */
	virtual std::string id() const = 0;

	/**
   * @brief Return the category this node is in (optional for subclassing, but recommended)
   *
   * In any organized node menus, show the node in this category. If this node should be in a subfolder of a subfolder,
   * use a "/" to separate categories (e.g. "Distort/Noise"). The string should not start with a "/" as this will be
   * interpreted as an empty string category. This value should be run through a translator as its largely user
   * oriented.
   */
	virtual std::vector<CategoryID> category() const = 0;

	/**
	 * @brief Return a sub-category string for secondary grouping
	 *        within the primary category (e.g. "Filter" under "OpenFX").
	 */
	virtual std::string sub_category() const
	{
		return std::string();
	}

	/**
   * @brief Return a description of this node's purpose (optional for subclassing, but recommended)
   *
   * A short (1-2 sentence) description of what this node should do to help the user understand its purpose. This should
   * be run through a translator.
   */
	virtual std::string description() const;

	Folder *folder() const
	{
		return folder_;
	}

	bool is_item() const
	{
		return flags_ & k_is_item;
	}

	/**
   * @brief Function called to retranslate parameter names (should be overridden in derivatives)
   */
	virtual void retranslate();

	enum DataType {
		icon,
		duration,
		created_time,
		modified_time,
		frequency_rate,
		tooltip
	};

	virtual Variant data(const DataType &d) const;

	const std::vector<std::string> &inputs() const
	{
		return input_ids_;
	}

	virtual std::vector<std::string> ignore_inputs_for_rendering() const
	{
		return std::vector<std::string>();
	}

	class ActiveElements {
	public:
		enum Mode { k_all_elements, k_specified, k_no_elements };

		ActiveElements(Mode m = k_all_elements)
		{
			mode_ = m;
		}

		Mode mode() const
		{
			return mode_;
		}
		std::list<int> elements() const
		{
			return elements_;
		}

		void add(int e)
		{
			elements_.push_back(e);
			mode_ = k_specified;
		}

	private:
		Mode mode_;
		std::list<int> elements_;
	};

	virtual ActiveElements get_active_elements_at_time(const std::string &input,
												   const TimeRange &r) const
	{
		return ActiveElements::k_all_elements;
	}

	bool has_input_with_id(const std::string &id) const
	{
		return std::find(input_ids_.begin(), input_ids_.end(), id) !=
			   input_ids_.end();
	}

	bool has_param_with_id(const std::string &id) const
	{
		return has_input_with_id(id);
	}

	/**
	 * @brief Borrowed copies of the cache handles owned by this node
	 *        (oakrender caches). Callers must NOT free them; addref
	 *        first to keep one beyond the node's lifetime.
	 */
	const OakRenderCache &video_frame_cache() const
	{
		return video_cache_;
	}

	const OakRenderCache &thumbnail_cache() const
	{
		return thumbnail_cache_;
	}

	const OakRenderCache &audio_playback_cache() const
	{
		return audio_cache_;
	}

	const OakRenderCache &waveform_cache() const
	{
		return waveform_cache_;
	}

	virtual TimeRange get_video_cache_range() const
	{
		return TimeRange();
	}
	virtual TimeRange get_audio_cache_range() const
	{
		return TimeRange();
	}

	struct Position {
		Position(const PointF &p = PointF(0, 0), bool e = false)
		{
			position = p;
			expanded = e;
		}

		bool load(XmlStreamReader *reader);
		void save(XmlStreamWriter *writer) const;

		PointF position;
		bool expanded;

		inline Position &operator+=(const Position &p)
		{
			position += p.position;
			return *this;
		}

		inline Position &operator-=(const Position &p)
		{
			position -= p.position;
			return *this;
		}

		friend inline const Position operator+(Position a, const Position &b)
		{
			a += b;
			return a;
		}

		friend inline const Position operator-(Position a, const Position &b)
		{
			a -= b;
			return a;
		}
	};

	using PositionMap = std::map<Node *, Position>;
	const PositionMap &get_context_positions() const
	{
		return context_positions_;
	}

	bool is_node_expanded_in_context(Node *node) const
	{
		auto it = context_positions_.find(node);
		return it != context_positions_.end() && it->second.expanded;
	}

	bool context_contains_node(Node *node) const
	{
		return context_positions_.count(node);
	}

	Position get_node_position_data_in_context(Node *node)
	{
		auto it = context_positions_.find(node);
		return it != context_positions_.end() ? it->second : Position();
	}

	PointF get_node_position_in_context(Node *node)
	{
		return get_node_position_data_in_context(node).position;
	}

	bool set_node_position_in_context(Node *node, const PointF &pos);

	bool set_node_position_in_context(Node *node, const Position &pos);

	void set_node_expanded_in_context(Node *node, bool e)
	{
		context_positions_[node].expanded = e;
	}

	bool remove_node_from_context(Node *node);

	/**
   * @brief Retrieve the color of this node
   */
	Color color() const;

	int get_override_color() const
	{
		return override_color_;
	}

	/**
   * @brief Sets the override color. Set to -1 for no override color.
   */
	void set_override_color(int index)
	{
		if (override_color_ != index) {
			override_color_ = index;
		}
	}

	static void connect_edge(Node *output, const NodeInput &input);

	static void disconnect_edge(Node *output, const NodeInput &input,
								bool silent = false);

	void copy_cache_uuids_from(Node *n);

	bool are_caches_enabled() const
	{
		return caches_enabled_;
	}
	void set_caches_enabled(bool e)
	{
		caches_enabled_ = e;
	}

	virtual std::string get_input_name(const std::string &id) const;

	void set_input_name(const std::string &id, const std::string &name);

	bool is_input_hidden(const std::string &input) const;
	bool is_input_connectable(const std::string &input) const;
	bool is_input_keyframable(const std::string &input) const;

	bool is_input_keyframing(const std::string &input, int element = -1) const;
	bool is_input_keyframing(const NodeInput &input) const
	{
		return is_input_keyframing(input.input(), input.element());
	}

	void set_input_is_keyframing(const std::string &input, bool e, int element = -1);
	void set_input_is_keyframing(const NodeInput &input, bool e)
	{
		set_input_is_keyframing(input.input(), e, input.element());
	}

	bool is_input_connected(const std::string &input, int element = -1) const;
	bool is_input_connected(const NodeInput &input) const
	{
		return is_input_connected(input.input(), input.element());
	}

	virtual bool is_input_connected_for_render(const std::string &input,
										   int element = -1) const
	{
		return is_input_connected(input, element);
	}
	bool is_input_connected_for_render(const NodeInput &input) const
	{
		return is_input_connected_for_render(input.input(), input.element());
	}

	bool is_input_static(const std::string &input, int element = -1) const
	{
		return !is_input_connected(input, element) &&
			   !is_input_keyframing(input, element);
	}

	bool is_input_static(const NodeInput &input) const
	{
		return is_input_static(input.input(), input.element());
	}

	Node *get_connected_output(const std::string &input, int element = -1) const;

	Node *get_connected_output(const NodeInput &input) const
	{
		return get_connected_output(input.input(), input.element());
	}

	virtual Node *get_connected_render_output(const std::string &input,
										   int element = -1) const
	{
		return get_connected_output(input, element);
	}

	Node *get_connected_render_output(const NodeInput &input) const
	{
		return get_connected_render_output(input.input(), input.element());
	}

	bool is_using_standard_value(const std::string &input, int track,
							  int element = -1) const;

	NodeValue::Type get_input_data_type(const std::string &id) const;
	void set_input_data_type(const std::string &id, const NodeValue::Type &type);

	bool has_input_property(const std::string &id, const std::string &name) const;
	std::map<std::string, Variant> get_input_properties(const std::string &id) const;
	Variant get_input_property(const std::string &id, const std::string &name) const;
	void set_input_property(const std::string &id, const std::string &name,
						  const Variant &value);

	Variant get_value_at_time(const std::string &input, const Rational &time,
							int element = -1) const
	{
		NodeValue::Type type = get_input_data_type(input);

		return NodeValue::combine_track_values_into_normal_value(
			type, get_split_value_at_time(input, time, element));
	}

	Variant get_value_at_time(const NodeInput &input, const Rational &time)
	{
		return get_value_at_time(input.input(), time, input.element());
	}

	SplitValue get_split_value_at_time(const std::string &input, const Rational &time,
								   int element = -1) const;

	SplitValue get_split_value_at_time(const NodeInput &input, const Rational &time)
	{
		return get_split_value_at_time(input.input(), time, input.element());
	}

	Variant get_split_value_at_time_on_track(const std::string &input,
										const Rational &time, int track,
										int element = -1) const;
	Variant get_split_value_at_time_on_track(const NodeInput &input,
										const Rational &time, int track) const
	{
		return get_split_value_at_time_on_track(input.input(), time, track,
										  input.element());
	}

	Variant get_split_value_at_time_on_track(const NodeKeyframeTrackReference &input,
										const Rational &time) const
	{
		return get_split_value_at_time_on_track(input.input(), time, input.track());
	}

	Variant get_default_value(const std::string &input) const;
	SplitValue get_split_default_value(const std::string &input) const;
	Variant get_split_default_value_on_track(const std::string &input, int track) const;

	void set_default_value(const std::string &input, const Variant &val);
	void set_split_default_value(const std::string &input, const SplitValue &val);
	void set_split_default_value_on_track(const std::string &input, const Variant &val,
									 int track);

	const std::vector<NodeKeyframeTrack> &get_keyframe_tracks(const std::string &input,
														int element) const;
	const std::vector<NodeKeyframeTrack> &
	get_keyframe_tracks(const NodeInput &input) const
	{
		return get_keyframe_tracks(input.input(), input.element());
	}

	std::vector<NodeKeyframe *> get_keyframes_at_time(const std::string &input,
											   const Rational &time,
											   int element = -1) const;
	std::vector<NodeKeyframe *> get_keyframes_at_time(const NodeInput &input,
											   const Rational &time) const
	{
		return get_keyframes_at_time(input.input(), time, input.element());
	}

	NodeKeyframe *get_keyframe_at_time_on_track(const std::string &input,
										   const Rational &time, int track,
										   int element = -1) const;
	NodeKeyframe *get_keyframe_at_time_on_track(const NodeInput &input,
										   const Rational &time,
										   int track) const
	{
		return get_keyframe_at_time_on_track(input.input(), time, track,
										input.element());
	}

	NodeKeyframe *
	get_keyframe_at_time_on_track(const NodeKeyframeTrackReference &input,
							 const Rational &time) const
	{
		return get_keyframe_at_time_on_track(input.input(), time, input.track());
	}

	NodeKeyframe::Type
	get_best_keyframe_type_for_time_on_track(const std::string &input,
									  const Rational &time, int track,
									  int element = -1) const;

	NodeKeyframe::Type get_best_keyframe_type_for_time_on_track(const NodeInput &input,
														 const Rational &time,
														 int track) const
	{
		return get_best_keyframe_type_for_time_on_track(input.input(), time, track,
												 input.element());
	}

	NodeKeyframe::Type
	get_best_keyframe_type_for_time_on_track(const NodeKeyframeTrackReference &input,
									  const Rational &time) const
	{
		return get_best_keyframe_type_for_time_on_track(input.input(), time,
												 input.track());
	}

	int get_number_of_keyframe_tracks(const std::string &id) const;
	int get_number_of_keyframe_tracks(const NodeInput &id) const
	{
		return get_number_of_keyframe_tracks(id.input());
	}

	NodeKeyframe *get_earliest_keyframe(const std::string &id,
									  int element = -1) const;
	NodeKeyframe *get_earliest_keyframe(const NodeInput &id) const
	{
		return get_earliest_keyframe(id.input(), id.element());
	}

	NodeKeyframe *get_latest_keyframe(const std::string &id, int element = -1) const;
	NodeKeyframe *get_latest_keyframe(const NodeInput &id) const
	{
		return get_latest_keyframe(id.input(), id.element());
	}

	NodeKeyframe *get_closest_keyframe_before_time(const std::string &id,
											   const Rational &time,
											   int element = -1) const;
	NodeKeyframe *get_closest_keyframe_before_time(const NodeInput &id,
											   const Rational &time) const
	{
		return get_closest_keyframe_before_time(id.input(), time, id.element());
	}

	NodeKeyframe *get_closest_keyframe_after_time(const std::string &id,
											  const Rational &time,
											  int element = -1) const;
	NodeKeyframe *get_closest_keyframe_after_time(const NodeInput &id,
											  const Rational &time) const
	{
		return get_closest_keyframe_after_time(id.input(), time, id.element());
	}

	bool has_keyframe_at_time(const std::string &id, const Rational &time,
						   int element = -1) const;
	bool has_keyframe_at_time(const NodeInput &id, const Rational &time) const
	{
		return has_keyframe_at_time(id.input(), time, id.element());
	}

	StringList get_combo_box_strings(const std::string &id) const;

	Variant get_standard_value(const std::string &id, int element = -1) const;
	Variant get_standard_value(const NodeInput &id) const
	{
		return get_standard_value(id.input(), id.element());
	}

	SplitValue get_split_standard_value(const std::string &id, int element = -1) const;
	SplitValue get_split_standard_value(const NodeInput &id) const
	{
		return get_split_standard_value(id.input(), id.element());
	}

	Variant get_split_standard_value_on_track(const std::string &input, int track,
										  int element = -1) const;
	Variant
	get_split_standard_value_on_track(const NodeKeyframeTrackReference &id) const
	{
		return get_split_standard_value_on_track(id.input().input(), id.track(),
											id.input().element());
	}

	void set_standard_value(const std::string &id, const Variant &value,
						  int element = -1);
	void set_standard_value(const NodeInput &id, const Variant &value)
	{
		set_standard_value(id.input(), value, id.element());
	}

	void set_split_standard_value(const std::string &id, const SplitValue &value,
							   int element = -1);
	void set_split_standard_value(const NodeInput &id, const SplitValue &value)
	{
		set_split_standard_value(id.input(), value, id.element());
	}

	void set_split_standard_value_on_track(const std::string &id, int track,
									  const Variant &value, int element = -1);
	void set_split_standard_value_on_track(const NodeKeyframeTrackReference &id,
									  const Variant &value)
	{
		set_split_standard_value_on_track(id.input().input(), id.track(), value,
									 id.input().element());
	}

	bool input_is_array(const std::string &id) const;

	void input_array_insert(const std::string &id, int index);
	void input_array_resize(const std::string &id, int size);
	void input_array_remove(const std::string &id, int index);

	void input_array_append(const std::string &id)
	{
		input_array_resize(id, input_array_size(id) + 1);
	}

	void input_array_prepend(const std::string &id)
	{
		input_array_insert(id, 0);
	}

	void input_array_remove_last(const std::string &id)
	{
		input_array_resize(id, input_array_size(id) - 1);
	}

	int input_array_size(const std::string &id) const;

	NodeInputImmediate *get_immediate(const std::string &input, int element) const;

	NodeInput get_effect_input()
	{
		return effect_input_.empty() ? NodeInput() :
									   NodeInput(this, effect_input_);
	}

	const std::string &get_effect_input_id() const
	{
		return effect_input_;
	}

	class ValueHint {
	public:
		explicit ValueHint(
			const std::vector<NodeValue::Type> &types = std::vector<NodeValue::Type>(),
			int index = -1, const std::string &tag = std::string())
			: type_(types)
			, index_(index)
			, tag_(tag)
		{
		}

		explicit ValueHint(const std::vector<NodeValue::Type> &types,
						   const std::string &tag)
			: type_(types)
			, index_(-1)
			, tag_(tag)
		{
		}

		explicit ValueHint(int index)
			: index_(index)
		{
		}

		explicit ValueHint(const std::string &tag)
			: index_(-1)
			, tag_(tag)
		{
		}

		const std::vector<NodeValue::Type> &types() const
		{
			return type_;
		}
		const int &index() const
		{
			return index_;
		}
		const std::string &tag() const
		{
			return tag_;
		}

		void set_type(const std::vector<NodeValue::Type> &type)
		{
			type_ = type;
		}
		void set_index(const int &index)
		{
			index_ = index;
		}
		void set_tag(const std::string &tag)
		{
			tag_ = tag;
		}

		bool load(XmlStreamReader *reader);
		void save(XmlStreamWriter *writer) const;

	private:
		std::vector<NodeValue::Type> type_;
		int index_;
		std::string tag_;
	};

	const std::map<InputElementPair, ValueHint> &get_value_hints() const
	{
		return value_hints_;
	}

	virtual ValueHint get_value_hint_for_input(const std::string &input,
										   int element = -1) const
	{
		auto it = value_hints_.find({ input, element });
		return it != value_hints_.end() ? it->second : ValueHint();
	}

	void set_value_hint_for_input(const std::string &input, const ValueHint &hint,
							  int element = -1);

	const NodeKeyframeTrack &get_track_from_keyframe(NodeKeyframe *key) const;

	using InputConnections = std::map<NodeInput, Node *>;

	/**
   * @brief Return map of input connections
   *
   * Inputs can only have one connection, so the key is the input connected and the value is the
   * output that it's connected to.
   */
	const InputConnections &input_connections() const
	{
		return input_connections_;
	}

	using OutputConnection = std::pair<Node *, NodeInput>;
	using OutputConnections = std::vector<OutputConnection>;

	/**
   * @brief Return list of output connections
   *
   * An output can connect an infinite amount of inputs, so in this map, the key is the output and
   * the value is a vector of inputs.
   */
	const OutputConnections &output_connections() const
	{
		return output_connections_;
	}

	/**
   * @brief Return a list of all Nodes that this Node's inputs are connected to (does not include this Node)
   */
	std::vector<Node *> get_dependencies() const;

	/**
   * @brief Returns a list of Nodes that this Node is dependent on, provided no other Nodes are dependent on them
   * outside of this hierarchy.
   *
   * Similar to GetDependencies(), but excludes any Nodes that are used outside the dependency graph of this Node.
   */
	std::vector<Node *> get_exclusive_dependencies() const;

	/**
   * @brief Retrieve immediate dependencies (only nodes that are directly connected to the inputs of this one)
   */
	std::vector<Node *> get_immediate_dependencies() const;

	struct ShaderRequest {
		ShaderRequest(const std::string &shader_id)
		{
			id = shader_id;
		}

		ShaderRequest(const std::string &shader_id, const std::string &shader_stub)
		{
			id = shader_id;
			stub = shader_stub;
		}

		std::string id;
		std::string stub;
	};

	/**
   * @brief Generate hardware accelerated code for this Node
   */
	virtual ShaderCode get_shader_code(const ShaderRequest &request) const;

	/**
   * @brief If Value() pushes a ShaderJob, this is the function that will process them.
   */
	virtual void process_samples(const NodeValueRow &values,
								const SampleBuffer &input, SampleBuffer &output,
								int index) const;

	/**
   * @brief If Value() pushes a GenerateJob, override this function for the image to create
   *
   * @param frame
   *
   * The destination buffer. It will already be allocated and ready for writing to.
   */
	virtual void generate_frame(FramePtr frame, const GenerateJob &job) const;

	/**
   * @brief Returns whether this node ever receives an input from a particular node instance
   */
	bool inputs_from(Node *n, bool recursively) const;

	/**
   * @brief Returns whether this node ever receives an input from a node with a particular ID
   */
	bool inputs_from(const std::string &id, bool recursively) const;

	/**
   * @brief Find inputs that `output` outputs to in order to arrive at this node
   *
   * Traverse this node's inputs recursively looking for `output`, and return a list of
   * edges that `output` uses to get to `this` node.
   */
	std::vector<NodeInput> find_ways_node_arrives_here(const Node *output) const;

	/**
   * @brief Severs all input and output connections
   */
	void disconnect_all();

	/**
   * @brief Get the human-readable name for any category
   */
	static std::string get_category_name(const CategoryID &c);

	enum TransformTimeDirection {
		k_towards_input,
		k_towards_output
	};

	/**
   * @brief Transforms time from this node through the connections it takes to get to the specified node
   */
	TimeRange transform_time_to(TimeRange time, Node *target,
							  TransformTimeDirection dir, int path_index);

	/**
   * @brief Find nodes of a certain type that this Node takes inputs from
   */
	template <class T> std::vector<T *> find_input_nodes(int maximum = 0) const;

	/**
   * @brief Find nodes of a certain type that this Node takes inputs from
   */
	template <class T>
	static std::vector<T *> find_input_nodes_connected_to_input(const NodeInput &input,
													   int maximum = 0);

	using InvalidateCacheOptions = std::map<std::string, Variant>;

	/**
   * @brief Signal all dependent Nodes that anything cached between start_range and end_range is now invalid and
   *        requires re-rendering
   *
   * Override this if your Node subclass keeps a cache, but call this base function at the end of the subclass function.
   * Default behavior is to relay this signal to all connected outputs, which will need to be done as to not break
   * the DAG. Even if the time needs to be transformed somehow (e.g. converting media time to sequence time), you can
   * call this function with transformed time and relay the signal that way.
   */
	virtual void
	invalidate_cache(const TimeRange &range, const std::string &from,
					int element = -1,
					InvalidateCacheOptions options = InvalidateCacheOptions());

	void invalidate_cache(
		const TimeRange &range, const NodeInput &from,
		const InvalidateCacheOptions &options = InvalidateCacheOptions())
	{
		invalidate_cache(range, from.input(), from.element(), options);
	}

	/**
   * @brief Adjusts time that should be sent to nodes connected to certain inputs.
   *
   * If this node modifies the `time` (i.e. a clip converting sequence time to media time), this function should be
   * overridden to do so. Also make sure to override OutputTimeAdjustment() to provide the inverse function.
   */
	virtual TimeRange input_time_adjustment(const std::string &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const;

	/**
   * @brief The inverse of InputTimeAdjustment()
   */
	virtual TimeRange output_time_adjustment(const std::string &input, int element,
										   const TimeRange &input_time) const;

	/**
   * @brief Copies inputs from from Node to another including connections
   *
   * Nodes must be of the same types (i.e. have the same ID)
   */
	static void copy_inputs(const Node *source, Node *destination,
						   bool include_connections = true,
						   MultiUndoCommand *command = nullptr);

	static void copy_input(const Node *src, Node *dst, const std::string &input,
						  bool include_connections, bool traverse_arrays,
						  MultiUndoCommand *command);

	static void copy_values_of_element(const Node *src, Node *dst,
									const std::string &input, int src_element,
									int dst_element,
									MultiUndoCommand *command = nullptr);
	static void copy_values_of_element(const Node *src, Node *dst,
									const std::string &input, int element,
									MultiUndoCommand *command = nullptr)
	{
		return copy_values_of_element(src, dst, input, element, element, command);
	}

	/**
   * @brief Clones a set of nodes and connects the new ones the way the old ones were
   */
	static std::vector<Node *> copy_dependency_graph(const std::vector<Node *> &nodes,
											   MultiUndoCommand *command);
	static void copy_dependency_graph(const std::vector<Node *> &src,
									const std::vector<Node *> &dst,
									MultiUndoCommand *command);

	static Node *
	copy_node_and_dependency_graph_minus_items(Node *node, MultiUndoCommand *command);

	static Node *copy_node_in_graph(Node *node, MultiUndoCommand *command);

	/**
   * @brief The main processing function
   *
   * The node's main purpose is to take values from inputs to set values in outputs. For whatever subclass node you
   * create, this is where the code for that goes.
   *
   * Note that as a video editor, the node graph has to work across time. Depending on the purpose of your node, it may
   * output different values depending on the time, and even if not, it will likely be receiving different input
   * depending on the time. Most of the difficult work here is handled by NodeInput::get_value() which you should pass
   * the `time` parameter to. It will return its value (at that time, if it's keyframed), or pass the time to a
   * corresponding output if it's connected to one. If your node doesn't directly deal with time, the default behavior
   * of the NodeParam objects will handle everything related to it automatically.
   */
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const;

	bool has_gizmos() const
	{
		return !gizmos_.empty();
	}

	const std::vector<NodeGizmo *> &get_gizmos() const
	{
		return gizmos_;
	}

	virtual Matrix4x4 gizmo_transformation(const NodeValueRow &row,
										   const NodeGlobals &globals) const
	{
		return Matrix4x4();
	}

	virtual void update_gizmo_positions(const NodeValueRow &row,
										const NodeGlobals &globals)
	{
	}

	const std::string &get_label() const;
	void set_label(const std::string &s);

	std::string get_label_and_name() const;
	std::string get_label_or_name() const;

	void invalidate_all(const std::string &input, int element = -1);

	bool has_links() const
	{
		return !links_.empty();
	}

	const std::vector<Node *> &links() const
	{
		return links_;
	}

	static bool link(Node *a, Node *b);
	static bool unlink(Node *a, Node *b);
	static bool are_linked(Node *a, Node *b);

	bool load(XmlStreamReader *reader, SerializedData *data);
	void save(XmlStreamWriter *writer) const;

	virtual bool load_custom(XmlStreamReader *reader, SerializedData *data);
	virtual void save_custom(XmlStreamWriter *writer) const
	{
	}
	virtual void PostLoadEvent(SerializedData *data);

	bool load_input(XmlStreamReader *reader, SerializedData *data);
	void save_input(XmlStreamWriter *writer, const std::string &id) const;

	/**
	 * @brief Maps an input ID read from an old project file to its current ID
	 *
	 * Nodes whose input IDs have been renamed override this so old projects
	 * keep loading. The default implementation returns the ID unchanged.
	 */
	virtual std::string get_input_id_for_legacy_id(const std::string &id) const;

	bool load_immediate(XmlStreamReader *reader, const std::string &input,
					   int element, SerializedData *data);
	void save_immediate(XmlStreamWriter *writer, const std::string &input,
					   int element) const;

	void set_folder(Folder *folder)
	{
		folder_ = folder;
	}

	InputFlags get_input_flags(const std::string &input) const;
	void set_input_flag(const std::string &input, InputFlag f, bool on = true);

	virtual void LoadFinishedEvent()
	{
	}
	virtual void ConnectedToPreviewEvent()
	{
	}

	static void set_value_at_time(const NodeInput &input, const Rational &time,
							   const Variant &value, int track,
							   MultiUndoCommand *command,
							   bool insert_on_all_tracks_if_no_key);

	/**
   * @brief Find path starting at `from` that outputs to arrive at `to`
   */
	static std::list<NodeInput> find_path(Node *from, Node *to, int path_index);

	void array_resize_internal(const std::string &id, int size);

	virtual void AddedToGraphEvent(Project *p)
	{
	}
	virtual void RemovedFromGraphEvent(Project *p)
	{
	}

	static std::string get_connect_command_string(Node *output,
										   const NodeInput &input);
	static std::string get_disconnect_command_string(Node *output,
												  const NodeInput &input);

	static const std::string k_enabled_input;

	/**
	 * @brief Registers a keyframe with this node
	 *
	 * Replaces the QObject childAdded path: inserts the keyframe into its
	 * input immediate (sorted by time) and invalidates the affected range.
	 */
	void add_keyframe(NodeKeyframe *key);

	/**
	 * @brief Unregisters a keyframe from this node
	 *
	 * Replaces the QObject childRemoved path: removes the keyframe from its
	 * input immediate and invalidates the range it affected.
	 */
	void remove_keyframe(NodeKeyframe *key);

	/**
	 * @brief Registers a gizmo with this node (replaces QObject childAdded)
	 */
	void add_gizmo(NodeGizmo *gizmo)
	{
		gizmos_.push_back(gizmo);
	}

	/**
	 * @brief Unregisters a gizmo from this node (replaces QObject childRemoved)
	 */
	void remove_gizmo(NodeGizmo *gizmo)
	{
		auto it = std::find(gizmos_.begin(), gizmos_.end(), gizmo);
		if (it != gizmos_.end()) {
			gizmos_.erase(it);
		}
	}

	/**
	 * @brief Keyframe change notifications (formerly private slots driven by
	 *        NodeKeyframe signals and sender())
	 *
	 * With the signal/slot mechanism removed, NodeKeyframe calls these
	 * directly through its node pointer.
	 */
	void invalidate_from_keyframe_time_change(NodeKeyframe *key);
	void invalidate_from_keyframe_value_change(NodeKeyframe *key);
	void invalidate_from_keyframe_type_changed(NodeKeyframe *key);
	void invalidate_from_keyframe_bezier_in_change(NodeKeyframe *key);
	void invalidate_from_keyframe_bezier_out_change(NodeKeyframe *key);

	OFX::Host::ImageEffect::Instance *getPluginInstance() const
	{
		return plugin_instance_;
	}
	OFX::Host::ImageEffect::ImageEffectPlugin *getPlugin() const
	{
		return plugin_instance_ ? plugin_instance_->getPlugin() : nullptr;
	}

protected:
	// If set, this node owns a plugin instance.
	OFX::Host::ImageEffect::Instance *plugin_instance_ = nullptr;

	void setPluginInstance(OFX::Host::ImageEffect::Instance *instance)
	{
		plugin_instance_ = instance;
	}

	void insert_input(const std::string &id, NodeValue::Type type,
					 const Variant &default_value, InputFlags flags,
					 int index);

	void prepend_input(const std::string &id, NodeValue::Type type,
					  const Variant &default_value,
					  InputFlags flags = InputFlags(k_input_flag_normal))
	{
		insert_input(id, type, default_value, flags, 0);
	}

	void prepend_input(const std::string &id, NodeValue::Type type,
					  InputFlags flags = InputFlags(k_input_flag_normal))
	{
		prepend_input(id, type, Variant(), flags);
	}

	void add_input(const std::string &id, NodeValue::Type type,
				  const Variant &default_value,
				  InputFlags flags = InputFlags(k_input_flag_normal))
	{
		insert_input(id, type, default_value, flags, int(input_ids_.size()));
	}

	void add_input(const std::string &id, NodeValue::Type type,
				  InputFlags flags = InputFlags(k_input_flag_normal))
	{
		add_input(id, type, Variant(), flags);
	}

	void remove_input(const std::string &id);

	void set_combo_box_strings(const std::string &id, const StringList &strings)
	{
		set_input_property(id, "combo_str", strings);
	}

	void send_invalidate_cache(const TimeRange &range,
							 const InvalidateCacheOptions &options);

	enum GizmoScaleHandles {
		k_gizmo_scale_top_left,
		k_gizmo_scale_top_center,
		k_gizmo_scale_top_right,
		k_gizmo_scale_bottom_left,
		k_gizmo_scale_bottom_center,
		k_gizmo_scale_bottom_right,
		k_gizmo_scale_center_left,
		k_gizmo_scale_center_right,
		k_gizmo_scale_count,
	};

	virtual void LinkChangeEvent()
	{
	}

	virtual void InputValueChangedEvent(const std::string &input, int element);

	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output);

	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output);

	virtual void OutputConnectedEvent(const NodeInput &input);

	virtual void OutputDisconnectedEvent(const NodeInput &input);

	void set_effect_input(const std::string &input)
	{
		effect_input_ = input;
	}

	void set_flag(Flag f, bool on = true)
	{
		if (on) {
			flags_ |= f;
		} else {
			flags_ &= ~f;
		}
	}

	template <typename T>
	T *add_draggable_gizmo(const std::vector<NodeKeyframeTrackReference> &inputs =
							 std::vector<NodeKeyframeTrackReference>(),
						 DraggableGizmo::DragValueBehavior behavior =
							 DraggableGizmo::k_delta_from_start)
	{
		T *gizmo = new T(this);
		gizmo->set_drag_value_behavior(behavior);
		for (const NodeKeyframeTrackReference &input : inputs) {
			gizmo->add_input(input);
		}
		add_gizmo(gizmo);
		return gizmo;
	}

	template <typename T>
	T *add_draggable_gizmo(const StringList &inputs,
						 DraggableGizmo::DragValueBehavior behavior =
							 DraggableGizmo::k_delta_from_start)
	{
		std::vector<NodeKeyframeTrackReference> refs(inputs.size());
		for (size_t i = 0; i < refs.size(); i++) {
			refs[i] = NodeInput(this, inputs[i]);
		}
		return add_draggable_gizmo<T>(refs, behavior);
	}

	/**
	 * @brief Gizmo drag callbacks (formerly protected slots connected to
	 *        DraggableGizmo signals)
	 *
	 * Plain virtual functions now; DraggableGizmo invokes them directly.
	 * `modifiers` carries the keyboard modifier flags as an int (formerly
	 * Qt::KeyboardModifiers).
	 */
	friend class DraggableGizmo;

	virtual void gizmo_drag_start(const olive::NodeValueRow &row, double x,
								double y, const olive::core::Rational &time)
	{
	}

	virtual void gizmo_drag_move(double x, double y, int modifiers)
	{
	}

	/**
	 * @brief Gizmo currently issuing a drag callback (replaces
	 *        QObject::sender() in gizmo_drag_start()/gizmo_drag_move())
	 *
	 * DraggableGizmo sets this around its direct invocation of the drag
	 * callbacks. Null outside of a drag callback.
	 */
	NodeGizmo *current_gizmo() const
	{
		return current_gizmo_;
	}

	void set_current_gizmo(NodeGizmo *gizmo)
	{
		current_gizmo_ = gizmo;
	}

private:
	struct Input {
		NodeValue::Type type;
		InputFlags flags;
		SplitValue default_value;
		std::map<std::string, Variant> properties;
		std::string human_name;
		int array_size;
	};

	NodeInputImmediate *create_immediate(const std::string &input);

	int get_internal_input_index(const std::string &input) const
	{
		auto it = std::find(input_ids_.begin(), input_ids_.end(), input);
		if (it == input_ids_.end()) {
			return -1;
		} else {
			return int(it - input_ids_.begin());
		}
	}

	Input *get_internal_input_data(const std::string &input)
	{
		int i = get_internal_input_index(input);

		if (i == -1) {
			return nullptr;
		} else {
			return &input_data_[i];
		}
	}

	const Input *get_internal_input_data(const std::string &input) const
	{
		int i = get_internal_input_index(input);

		if (i == -1) {
			return nullptr;
		} else {
			return &input_data_.at(i);
		}
	}

	void report_invalid_input(const char *attempted_action, const std::string &id,
							int element) const;

	static Node *copy_node_and_dependency_graph_minus_items_internal(
		std::map<Node *, Node *> &created, Node *node, MultiUndoCommand *command);

	/**
   * @brief Immediates aren't deleted, so the actual array size may be larger than ArraySize()
   */
	int get_internal_input_array_size(const std::string &input);

	/**
   * @brief Find nodes of a certain type that this Node takes inputs from
   */
	template <class T>
	static void find_input_nodes_connected_to_input_internal(const NodeInput &input,
													   std::vector<T *> &list,
													   int maximum);

	template <class T>
	static void find_input_node_internal(const Node *n, std::vector<T *> &list,
									  int maximum);

	std::vector<Node *> get_dependencies_internal(bool traverse,
											bool exclusive_only) const;

	void parameter_value_changed(const std::string &input, int element,
							   const olive::core::TimeRange &range);
	void parameter_value_changed(const NodeInput &input,
							   const olive::core::TimeRange &range)
	{
		parameter_value_changed(input.input(), input.element(), range);
	}

	/**
   * @brief Intelligently determine how what time range is affected by a keyframe
   */
	TimeRange get_range_affected_by_keyframe(NodeKeyframe *key) const;

	/**
   * @brief Gets a time range between the previous and next keyframes of index
   */
	TimeRange get_range_around_index(const std::string &input, int index, int track,
								  int element) const;

	void clear_element(const std::string &input, int index);

	/**
   * @brief Custom user label for node
   */
	std::string label_;

	/**
   * @brief -1 if the color should be based on the category, >=0 if the user has set a custom color
   */
	int override_color_;

	/**
   * @brief Nodes that are linked with this one
   */
	std::vector<Node *> links_;

	std::vector<std::string> input_ids_;
	std::vector<Input> input_data_;

	std::map<std::string, NodeInputImmediate *> standard_immediates_;

	std::map<std::string, std::vector<NodeInputImmediate *>> array_immediates_;

	InputConnections input_connections_;

	OutputConnections output_connections_;

	Folder *folder_;

	/**
   * @brief Graph this node belongs to (replaces QObject::parent())
   */
	Project *parent_;

	std::map<InputElementPair, ValueHint> value_hints_;

	PositionMap context_positions_;

	uint64_t flags_;

	std::vector<NodeGizmo *> gizmos_;

	NodeGizmo *current_gizmo_ = nullptr;

	std::string effect_input_;

	OakRenderCache video_cache_ = {};
	OakRenderCache thumbnail_cache_ = {};

	OakRenderCache audio_cache_ = {};
	OakRenderCache waveform_cache_ = {};

	bool caches_enabled_;
};

template <class T>
void Node::find_input_nodes_connected_to_input_internal(const NodeInput &input,
												  std::vector<T *> &list,
												  int maximum)
{
	Node *edge = input.get_connected_output();
	if (!edge) {
		return;
	}

	T *cast_test = dynamic_cast<T *>(edge);

	if (cast_test) {
		list.push_back(cast_test);
		if (maximum != 0 && int(list.size()) == maximum) {
			return;
		}
	}

	find_input_node_internal<T>(edge, list, maximum);
}

template <class T>
std::vector<T *> Node::find_input_nodes_connected_to_input(const NodeInput &input,
												  int maximum)
{
	std::vector<T *> list;

	find_input_nodes_connected_to_input_internal<T>(input, list, maximum);

	return list;
}

template <class T>
void Node::find_input_node_internal(const Node *n, std::vector<T *> &list, int maximum)
{
	for (auto it = n->input_connections_.cbegin();
		 it != n->input_connections_.cend(); it++) {
		find_input_nodes_connected_to_input_internal(it->first, list, maximum);
	}
}

template <class T> std::vector<T *> Node::find_input_nodes(int maximum) const
{
	std::vector<T *> list;

	find_input_node_internal<T>(this, list, maximum);

	return list;
}

}

#endif // OAK_NODE_H
