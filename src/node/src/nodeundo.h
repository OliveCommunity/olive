/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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

#ifndef OAK_NODEUNDO_H
#define OAK_NODEUNDO_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "node.h"
#include "project.h"
#include "undocommand.h"

namespace olive
{

class NodeSetPositionCommand : public UndoCommand {
public:
	NodeSetPositionCommand(Node *node, Node *context, const Node::Position &pos)
	{
		node_ = node;
		context_ = context;
		pos_ = pos;
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;
	Node *context_;
	Node::Position pos_;
	Node::Position old_pos_;
	bool added_;
};

class NodeSetPositionAndDependenciesRecursivelyCommand : public UndoCommand {
public:
	NodeSetPositionAndDependenciesRecursivelyCommand(Node *node, Node *context,
													 const Node::Position &pos)
		: node_(node)
		, context_(context)
		, pos_(pos)
	{
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	void move_recursively(Node *node, const PointF &diff);

	Node *node_;
	Node *context_;
	Node::Position pos_;
	std::vector<UndoCommand *> commands_;
};

class NodeRemovePositionFromContextCommand : public UndoCommand {
public:
	NodeRemovePositionFromContextCommand(Node *node, Node *context)
		: node_(node)
		, context_(context)
	{
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	Node *context_;

	Node::Position old_pos_;

	bool contained_;
};

class NodeRemovePositionFromAllContextsCommand : public UndoCommand {
public:
	NodeRemovePositionFromAllContextsCommand(Node *node)
		: node_(node)
	{
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	std::map<Node *, PointF> contexts_;
};

class NodeArrayInsertCommand : public UndoCommand {
public:
	NodeArrayInsertCommand(Node *node, const std::string &input, int index)
		: node_(node)
		, input_(input)
		, index_(index)
	{
	}

protected:
	virtual void redo() override
	{
		node_->input_array_insert(input_, index_);
	}

	virtual void undo() override
	{
		node_->input_array_remove(input_, index_);
	}

private:
	Node *node_;
	std::string input_;
	int index_;
};

class NodeArrayResizeCommand : public UndoCommand {
public:
	NodeArrayResizeCommand(Node *node, const std::string &input, int size)
		: node_(node)
		, input_(input)
		, size_(size)
	{
	}

protected:
	virtual void redo() override
	{
		old_size_ = node_->input_array_size(input_);

		if (old_size_ > size_) {
			// Decreasing in size, disconnect any extraneous edges
			for (int i = size_; i < old_size_; i++) {
				try {
					NodeInput input(node_, input_, i);
					Node *output = node_->input_connections().at(input);

					removed_connections_[input] = output;

					Node::disconnect_edge(output, input);
				} catch (std::out_of_range &) {
				}
			}
		}

		node_->array_resize_internal(input_, size_);
	}

	virtual void undo() override
	{
		for (auto it = removed_connections_.cbegin();
			 it != removed_connections_.cend(); it++) {
			Node::connect_edge(it->second, it->first);
		}
		removed_connections_.clear();

		node_->array_resize_internal(input_, old_size_);
	}

private:
	Node *node_;
	std::string input_;
	int size_;
	int old_size_;

	Node::InputConnections removed_connections_;
};

class NodeArrayRemoveCommand : public UndoCommand {
public:
	NodeArrayRemoveCommand(Node *node, const std::string &input, int index)
		: node_(node)
		, input_(input)
		, index_(index)
	{
	}

protected:
	virtual void redo() override
	{
		// Save immediate data
		if (node_->is_input_keyframable(input_)) {
			is_keyframing_ = node_->is_input_keyframing(input_, index_);
		}
		standard_value_ = node_->get_split_standard_value(input_, index_);
		keyframes_ = node_->get_keyframe_tracks(input_, index_);

		// Take ownership of the keyframes (removes them from the immediate)
		std::vector<NodeKeyframe *> reclaimed;
		node_->get_immediate(input_, index_)->delete_all_keyframes(&reclaimed);
		for (NodeKeyframe *key : reclaimed) {
			owned_keyframes_.emplace_back(key);
		}

		node_->input_array_remove(input_, index_);
	}

	virtual void undo() override
	{
		node_->input_array_insert(input_, index_);

		// Restore keyframes
		for (const NodeKeyframeTrack &track : keyframes_) {
			for (NodeKeyframe *key : track) {
				node_->add_keyframe(key);
			}
		}
		for (auto &key : owned_keyframes_) {
			key.release();
		}
		owned_keyframes_.clear();

		node_->set_split_standard_value(input_, standard_value_, index_);

		if (node_->is_input_keyframable(input_)) {
			node_->set_input_is_keyframing(input_, is_keyframing_, index_);
		}
	}

private:
	Node *node_;
	std::string input_;
	int index_;

	SplitValue standard_value_;
	bool is_keyframing_;
	std::vector<NodeKeyframeTrack> keyframes_;
	std::vector<std::unique_ptr<NodeKeyframe>> owned_keyframes_;
};

/**
 * @brief An undoable command for disconnecting two NodeParams
 *
 * Can be considered a UndoCommand wrapper for NodeParam::DisonnectEdge()/
 */
class NodeEdgeRemoveCommand : public UndoCommand {
public:
	NodeEdgeRemoveCommand(Node *output, const NodeInput &input);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *output_;
	NodeInput input_;
};

/**
 * @brief An undoable command for connecting two NodeParams together
 *
 * Can be considered a UndoCommand wrapper for NodeParam::ConnectEdge()/
 */
class NodeEdgeAddCommand : public UndoCommand {
public:
	NodeEdgeAddCommand(Node *output, const NodeInput &input);

	virtual ~NodeEdgeAddCommand() override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *output_;
	NodeInput input_;

	NodeEdgeRemoveCommand *remove_command_;
};

class NodeAddCommand : public UndoCommand {
public:
	NodeAddCommand(Project *graph, Node *node);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	std::unique_ptr<Node> owned_node_;

	Project *graph_;
	Node *node_;
};

class NodeRemoveAndDisconnectCommand : public UndoCommand {
public:
	NodeRemoveAndDisconnectCommand(Node *node)
		: node_(node)
		, graph_(nullptr)
		, command_(nullptr)
	{
	}

	virtual ~NodeRemoveAndDisconnectCommand() override
	{
		delete command_;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override
	{
		command_->redo_now();

		graph_ = node_->parent();
		graph_->remove_node(node_);
		owned_node_.reset(node_);
	}

	virtual void undo() override
	{
		graph_->add_node(owned_node_.release());
		graph_ = nullptr;

		command_->undo_now();
	}

private:
	std::unique_ptr<Node> owned_node_;

	Node *node_;
	Project *graph_;

	MultiUndoCommand *command_;
};

class NodeRemoveWithExclusiveDependenciesAndDisconnect : public UndoCommand {
public:
	NodeRemoveWithExclusiveDependenciesAndDisconnect(Node *node)
		: node_(node)
		, command_(nullptr)
	{
	}

	virtual ~NodeRemoveWithExclusiveDependenciesAndDisconnect() override
	{
		delete command_;
	}

protected:
	virtual void prepare() override
	{
		command_ = new MultiUndoCommand();

		command_->add_child(new NodeRemoveAndDisconnectCommand(node_));

		// Remove exclusive dependencies
		std::vector<Node *> deps = node_->get_exclusive_dependencies();
		for (Node *d : deps) {
			command_->add_child(new NodeRemoveAndDisconnectCommand(d));
		}
	}

	virtual void redo() override
	{
		command_->redo_now();
	}

	virtual void undo() override
	{
		command_->undo_now();
	}

private:
	Node *node_;
	MultiUndoCommand *command_;
};

class NodeLinkCommand : public UndoCommand {
public:
	NodeLinkCommand(Node *a, Node *b, bool link)
		: a_(a)
		, b_(b)
		, link_(link)
	{
	}

protected:
	virtual void redo() override
	{
		if (link_) {
			done_ = Node::link(a_, b_);
		} else {
			done_ = Node::unlink(a_, b_);
		}
	}

	virtual void undo() override
	{
		if (done_) {
			if (link_) {
				Node::unlink(a_, b_);
			} else {
				Node::link(a_, b_);
			}
		}
	}

private:
	Node *a_;
	Node *b_;
	bool link_;
	bool done_;
};

class NodeUnlinkAllCommand : public UndoCommand {
public:
	NodeUnlinkAllCommand(Node *node)
		: node_(node)
	{
	}

protected:
	virtual void redo() override
	{
		unlinked_ = node_->links();

		for (Node *link : unlinked_) {
			Node::unlink(node_, link);
		}
	}

	virtual void undo() override
	{
		for (Node *link : unlinked_) {
			Node::link(node_, link);
		}

		unlinked_.clear();
	}

private:
	Node *node_;

	std::vector<Node *> unlinked_;
};

class NodeLinkManyCommand : public MultiUndoCommand {
public:
	NodeLinkManyCommand(const std::vector<Node *> nodes, bool link)
		: nodes_(nodes)
	{
		for (Node *a : nodes_) {
			for (Node *b : nodes_) {
				if (a != b) {
					add_child(new NodeLinkCommand(a, b, link));
				}
			}
		}
	}

private:
	std::vector<Node *> nodes_;
};

class NodeRenameCommand : public UndoCommand {
public:
	NodeRenameCommand() = default;
	NodeRenameCommand(Node *node, const std::string &new_name)
	{
		add_node(node, new_name);
	}

	void add_node(Node *node, const std::string &new_name);

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	std::vector<Node *> nodes_;

	StringList new_labels_;
	StringList old_labels_;
};

class NodeOverrideColorCommand : public UndoCommand {
public:
	NodeOverrideColorCommand(Node *node, int index);

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	int old_index_;

	int new_index_;
};

class NodeViewDeleteCommand : public UndoCommand {
public:
	NodeViewDeleteCommand();

	void add_node(Node *node, Node *context);

	void add_edge(Node *output, const NodeInput &input);

	bool contains_node(Node *node, Node *context);

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	std::vector<Node::ContextPair> nodes_;

	std::vector<Node::OutputConnection> edges_;

	struct RemovedNode {
		Node *node;
		Node *context;
		PointF pos;
		Project *removed_from_graph;
	};

	std::vector<RemovedNode> removed_nodes_;

	std::vector<std::unique_ptr<Node>> owned_nodes_;
};

class NodeParamSetKeyframingCommand : public UndoCommand {
public:
	NodeParamSetKeyframingCommand(const NodeInput &input, bool setting);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeInput input_;
	bool new_setting_;
	bool old_setting_;
};

class NodeParamInsertKeyframeCommand : public UndoCommand {
public:
	NodeParamInsertKeyframeCommand(Node *node, NodeKeyframe *keyframe);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *input_;

	NodeKeyframe *keyframe_;

	std::unique_ptr<NodeKeyframe> owned_keyframe_;
};

class NodeParamRemoveKeyframeCommand : public UndoCommand {
public:
	NodeParamRemoveKeyframeCommand(NodeKeyframe *keyframe);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *input_;

	NodeKeyframe *keyframe_;

	std::unique_ptr<NodeKeyframe> owned_keyframe_;
};

class NodeParamSetKeyframeTimeCommand : public UndoCommand {
public:
	NodeParamSetKeyframeTimeCommand(NodeKeyframe *key, const Rational &time);
	NodeParamSetKeyframeTimeCommand(NodeKeyframe *key, const Rational &new_time,
									const Rational &old_time);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeKeyframe *key_;

	Rational old_time_;
	Rational new_time_;
};

class NodeParamSetKeyframeValueCommand : public UndoCommand {
public:
	NodeParamSetKeyframeValueCommand(NodeKeyframe *key, const Variant &value);
	NodeParamSetKeyframeValueCommand(NodeKeyframe *key,
									 const Variant &new_value,
									 const Variant &old_value);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeKeyframe *key_;

	Variant old_value_;
	Variant new_value_;
};

class NodeParamSetStandardValueCommand : public UndoCommand {
public:
	NodeParamSetStandardValueCommand(const NodeKeyframeTrackReference &input,
									 const Variant &value);
	NodeParamSetStandardValueCommand(const NodeKeyframeTrackReference &input,
									 const Variant &new_value,
									 const Variant &old_value);

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeKeyframeTrackReference ref_;

	Variant old_value_;
	Variant new_value_;
};

class NodeParamSetSplitStandardValueCommand : public UndoCommand {
public:
	NodeParamSetSplitStandardValueCommand(const NodeInput &input,
										  const SplitValue &new_value,
										  const SplitValue &old_value)
		: ref_(input)
		, old_value_(old_value)
		, new_value_(new_value)
	{
	}

	NodeParamSetSplitStandardValueCommand(const NodeInput &input,
										  const SplitValue &value)
		: NodeParamSetSplitStandardValueCommand(
			  input, value, input.node()->get_split_standard_value(input.input()))
	{
	}

protected:
	virtual void redo() override
	{
		ref_.node()->set_split_standard_value(ref_.input(), new_value_,
										   ref_.element());
	}

	virtual void undo() override
	{
		ref_.node()->set_split_standard_value(ref_.input(), old_value_,
										   ref_.element());
	}

private:
	NodeInput ref_;

	SplitValue old_value_;
	SplitValue new_value_;
};

class NodeParamArrayAppendCommand : public UndoCommand {
public:
	NodeParamArrayAppendCommand(Node *node, const std::string &input);

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	std::string input_;
};

class NodeSetValueHintCommand : public UndoCommand {
public:
	NodeSetValueHintCommand(const NodeInput &input, const Node::ValueHint &hint)
		: input_(input)
		, new_hint_(hint)
	{
	}

	NodeSetValueHintCommand(Node *node, const std::string &input, int element,
							const Node::ValueHint &hint)
		: NodeSetValueHintCommand(NodeInput(node, input, element), hint)
	{
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	NodeInput input_;

	Node::ValueHint new_hint_;
	Node::ValueHint old_hint_;
};

class NodeImmediateRemoveAllKeyframesCommand : public UndoCommand {
public:
	NodeImmediateRemoveAllKeyframesCommand(NodeInputImmediate *immediate)
		: immediate_(immediate)
		, node_(nullptr)
	{
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	NodeInputImmediate *immediate_;

	Node *node_;

	std::vector<std::unique_ptr<NodeKeyframe>> owned_keyframes_;

	std::vector<NodeKeyframe *> keys_;
};

}

#endif // OAK_NODEUNDO_H
