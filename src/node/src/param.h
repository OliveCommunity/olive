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

#ifndef OAK_NODEPARAM_H
#define OAK_NODEPARAM_H

#include <map>
#include <string>

#include "value.h"

namespace olive
{

class Node;
class NodeKeyframe;

enum InputFlag : uint64_t {
	/// By default, inputs are keyframable, connectable, and NOT arrays
	k_input_flag_normal = 0x0,
	k_input_flag_array = 0x1,
	k_input_flag_not_keyframable = 0x2,
	k_input_flag_not_connectable = 0x4,
	k_input_flag_hidden = 0x8,
	k_input_flag_ignore_invalidations = 0x10,

	k_input_flag_static = k_input_flag_not_keyframable | k_input_flag_not_connectable
};

class InputFlags {
public:
	explicit InputFlags()
	{
		f_ = k_input_flag_normal;
	}

	explicit InputFlags(uint64_t flags)
	{
		f_ = flags;
	}

	InputFlags operator|(const InputFlags &f) const
	{
		InputFlags i = *this;
		i |= f;
		return i;
	}

	InputFlags operator|(const InputFlag &f) const
	{
		InputFlags i = *this;
		i |= f;
		return i;
	}

	InputFlags operator|(const uint64_t &f) const
	{
		InputFlags i = *this;
		i |= f;
		return i;
	}

	InputFlags &operator|=(const InputFlags &f)
	{
		f_ |= f.f_;
		return *this;
	}

	InputFlags &operator|=(const InputFlag &f)
	{
		f_ |= f;
		return *this;
	}

	InputFlags &operator|=(const uint64_t &f)
	{
		f_ |= f;
		return *this;
	}

	InputFlags operator&(const InputFlags &f) const
	{
		InputFlags i = *this;
		i &= f;
		return i;
	}

	InputFlags operator&(const InputFlag &f) const
	{
		InputFlags i = *this;
		i &= f;
		return i;
	}

	InputFlags operator&(const uint64_t &f) const
	{
		InputFlags i = *this;
		i &= f;
		return i;
	}

	InputFlags &operator&=(const InputFlags &f)
	{
		f_ &= f.f_;
		return *this;
	}

	InputFlags &operator&=(const InputFlag &f)
	{
		f_ &= f;
		return *this;
	}

	InputFlags &operator&=(const uint64_t &f)
	{
		f_ &= f;
		return *this;
	}

	InputFlags operator~() const
	{
		InputFlags i = *this;
		i.f_ = ~i.f_;
		return i;
	}

	inline operator bool() const
	{
		return f_;
	}

	inline const uint64_t &value() const
	{
		return f_;
	}

private:
	uint64_t f_;
};

struct NodeInputPair {
	bool operator==(const NodeInputPair &rhs) const
	{
		return node == rhs.node && input == rhs.input;
	}

	Node *node;
	std::string input;
};

/**
 * @brief Defines a Node input
 */
class NodeInput {
public:
	NodeInput()
	{
		node_ = nullptr;
		element_ = -1;
	}

	NodeInput(Node *n, const std::string &i, int e = -1)
	{
		node_ = n;
		input_ = i;
		element_ = e;
	}

	bool operator==(const NodeInput &rhs) const
	{
		return node_ == rhs.node_ && input_ == rhs.input_ &&
			   element_ == rhs.element_;
	}

	bool operator!=(const NodeInput &rhs) const
	{
		return !(*this == rhs);
	}

	bool operator<(const NodeInput &rhs) const
	{
		if (node_ != rhs.node_) {
			return node_ < rhs.node_;
		}

		if (input_ != rhs.input_) {
			return input_ < rhs.input_;
		}

		return element_ < rhs.element_;
	}

	Node *node() const
	{
		return node_;
	}

	NodeInputPair input_pair() const
	{
		return { node_, input_ };
	}

	const std::string &input() const
	{
		return input_;
	}

	const int &element() const
	{
		return element_;
	}

	void set_node(Node *node)
	{
		node_ = node;
	}

	void set_input(const std::string &input)
	{
		input_ = input;
	}

	void set_element(int e)
	{
		element_ = e;
	}

	std::string name() const;

	bool is_valid() const
	{
		return node_ && !input_.empty() && element_ >= -1;
	}

	bool is_hidden() const;

	bool is_connected() const;

	bool is_keyframing() const;

	bool is_array() const;

	InputFlags get_flags() const;

	std::string get_input_name() const;

	Node *get_connected_output() const;

	NodeValue::Type get_data_type() const;

	Variant get_default_value() const;

	StringList get_combo_box_strings() const;

	Variant get_property(const std::string &key) const;
	std::map<std::string, Variant> get_properties() const;

	Variant get_value_at_time(const Rational &time) const;

	NodeKeyframe *get_keyframe_at_time_on_track(const Rational &time,
										   int track) const;

	Variant get_split_default_value_for_track(int track) const;

	int get_array_size() const;

	void reset()
	{
		*this = NodeInput();
	}

private:
	Node *node_;
	std::string input_;
	int element_;
};

struct InputElementPair {
	std::string input;
	int element;

	bool operator<(const InputElementPair &rhs) const
	{
		if (input != rhs.input) {
			return input < rhs.input;
		}

		return element < rhs.element;
	}

	bool operator==(const InputElementPair &rhs) const
	{
		return input == rhs.input && element == rhs.element;
	}

	bool operator!=(const InputElementPair &rhs) const
	{
		return !(*this == rhs);
	}
};

class NodeKeyframeTrackReference {
public:
	NodeKeyframeTrackReference()
	{
		track_ = -1;
	}

	NodeKeyframeTrackReference(const NodeInput &input, int track = 0)
	{
		input_ = input;
		track_ = track;
	}

	bool operator==(const NodeKeyframeTrackReference &rhs) const
	{
		return input_ == rhs.input_ && track_ == rhs.track_;
	}

	const NodeInput &input() const
	{
		return input_;
	}

	int track() const
	{
		return track_;
	}

	bool is_valid() const
	{
		return input_.is_valid() && track_ >= 0;
	}

	void reset()
	{
		*this = NodeKeyframeTrackReference();
	}

private:
	NodeInput input_;
	int track_;
};

}

#endif // OAK_NODEPARAM_H
