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

#include "node/node.h"
#include "node/project.h"
#include "undo/undocommand.h"

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

	virtual Project *get_relevant_project() const override
	{
		return node_->project();
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

	virtual Project *get_relevant_project() const override
	{
		return node_->project();
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	void move_recursively(Node *node, const QPointF &diff);

	Node *node_;
	Node *context_;
	Node::Position pos_;
	QVector<UndoCommand *> commands_;
};

class NodeRemovePositionFromContextCommand : public UndoCommand {
public:
	NodeRemovePositionFromContextCommand(Node *node, Node *context)
		: node_(node)
		, context_(context)
	{
	}

	virtual Project *get_relevant_project() const override
	{
		return node_->project();
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

	virtual Project *get_relevant_project() const override
	{
		return node_->project();
	}

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	std::map<Node *, QPointF> contexts_;
};

class NodeArrayInsertCommand : public UndoCommand {
public:
	NodeArrayInsertCommand(Node *node, const QString &input, int index)
		: node_(node)
		, input_(input)
		, index_(index)
	{
	}

	virtual Project *get_relevant_project() const override;

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
	QString input_;
	int index_;
};

class NodeArrayResizeCommand : public UndoCommand {
public:
	NodeArrayResizeCommand(Node *node, const QString &input, int size)
		: node_(node)
		, input_(input)
		, size_(size)
	{
	}

	virtual Project *get_relevant_project() const override;

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
	QString input_;
	int size_;
	int old_size_;

	Node::InputConnections removed_connections_;
};

class NodeArrayRemoveCommand : public UndoCommand {
public:
	NodeArrayRemoveCommand(Node *node, const QString &input, int index)
		: node_(node)
		, input_(input)
		, index_(index)
	{
	}

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override
	{
		// Save immediate data
		if (node_->is_input_keyframable(input_)) {
			is_keyframing_ = node_->is_input_keyframing(input_, index_);
		}
		standard_value_ = node_->get_split_standard_value(input_, index_);
		keyframes_ = node_->get_keyframe_tracks(input_, index_);
		node_->get_immediate(input_, index_)
			->delete_all_keyframes(&memory_manager_);

		node_->input_array_remove(input_, index_);
	}

	virtual void undo() override
	{
		node_->input_array_insert(input_, index_);

		// Restore keyframes
		foreach (const NodeKeyframeTrack &track, keyframes_) {
			foreach (NodeKeyframe *key, track) {
				key->setParent(node_);
			}
		}
		node_->set_split_standard_value(input_, standard_value_, index_);

		if (node_->is_input_keyframable(input_)) {
			node_->set_input_is_keyframing(input_, is_keyframing_, index_);
		}
	}

private:
	Node *node_;
	QString input_;
	int index_;

	SplitValue standard_value_;
	bool is_keyframing_;
	QVector<NodeKeyframeTrack> keyframes_;
	QObject memory_manager_;
};

/**
 * @brief An undoable command for disconnecting two NodeParams
 *
 * Can be considered a UndoCommand wrapper for NodeParam::DisonnectEdge()/
 */
class NodeEdgeRemoveCommand : public UndoCommand {
public:
	NodeEdgeRemoveCommand(Node *output, const NodeInput &input);

	virtual Project *get_relevant_project() const override;

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

	virtual Project *get_relevant_project() const override;

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

	void push_to_thread(QThread *thread);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	QObject memory_manager_;

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

	virtual Project *get_relevant_project() const override
	{
		return graph_;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override
	{
		command_->redo_now();

		graph_ = node_->parent();
		node_->setParent(&memory_manager_);
	}

	virtual void undo() override
	{
		node_->setParent(graph_);
		graph_ = nullptr;

		command_->undo_now();
	}

private:
	QObject memory_manager_;

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

	virtual Project *get_relevant_project() const override
	{
		if (command_) {
			return static_cast<const NodeRemoveAndDisconnectCommand *>(
					   command_->child(0))
				->get_relevant_project();
		} else {
			return node_->project();
		}
	}

protected:
	virtual void prepare() override
	{
		command_ = new MultiUndoCommand();

		command_->add_child(new NodeRemoveAndDisconnectCommand(node_));

		// Remove exclusive dependencies
		QVector<Node *> deps = node_->get_exclusive_dependencies();
		foreach (Node *d, deps) {
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

	virtual Project *get_relevant_project() const override
	{
		return a_->project();
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

	virtual Project *get_relevant_project() const override
	{
		return node_->project();
	}

protected:
	virtual void redo() override
	{
		unlinked_ = node_->links();

		foreach (Node *link, unlinked_) {
			Node::unlink(node_, link);
		}
	}

	virtual void undo() override
	{
		foreach (Node *link, unlinked_) {
			Node::link(node_, link);
		}

		unlinked_.clear();
	}

private:
	Node *node_;

	QVector<Node *> unlinked_;
};

class NodeLinkManyCommand : public MultiUndoCommand {
public:
	NodeLinkManyCommand(const QVector<Node *> nodes, bool link)
		: nodes_(nodes)
	{
		foreach (Node *a, nodes_) {
			foreach (Node *b, nodes_) {
				if (a != b) {
					add_child(new NodeLinkCommand(a, b, link));
				}
			}
		}
	}

	virtual Project *get_relevant_project() const override
	{
		return nodes_.first()->project();
	}

private:
	QVector<Node *> nodes_;
};

class NodeRenameCommand : public UndoCommand {
public:
	NodeRenameCommand() = default;
	NodeRenameCommand(Node *node, const QString &new_name)
	{
		add_node(node, new_name);
	}

	void add_node(Node *node, const QString &new_name);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	QVector<Node *> nodes_;

	QStringList new_labels_;
	QStringList old_labels_;
};

class NodeOverrideColorCommand : public UndoCommand {
public:
	NodeOverrideColorCommand(Node *node, int index);

	virtual Project *get_relevant_project() const override;

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

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	QVector<Node::ContextPair> nodes_;

	QVector<Node::OutputConnection> edges_;

	struct RemovedNode {
		Node *node;
		Node *context;
		QPointF pos;
		Project *removed_from_graph;
	};

	QVector<RemovedNode> removed_nodes_;

	QObject memory_manager_;
};

class NodeParamSetKeyframingCommand : public UndoCommand {
public:
	NodeParamSetKeyframingCommand(const NodeInput &input, bool setting);

	virtual Project *get_relevant_project() const override;

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

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *input_;

	NodeKeyframe *keyframe_;

	QObject memory_manager_;
};

class NodeParamRemoveKeyframeCommand : public UndoCommand {
public:
	NodeParamRemoveKeyframeCommand(NodeKeyframe *keyframe);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	Node *input_;

	NodeKeyframe *keyframe_;

	QObject memory_manager_;
};

class NodeParamSetKeyframeTimeCommand : public UndoCommand {
public:
	NodeParamSetKeyframeTimeCommand(NodeKeyframe *key, const Rational &time);
	NodeParamSetKeyframeTimeCommand(NodeKeyframe *key, const Rational &new_time,
									const Rational &old_time);

	virtual Project *get_relevant_project() const override;

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
	NodeParamSetKeyframeValueCommand(NodeKeyframe *key, const QVariant &value);
	NodeParamSetKeyframeValueCommand(NodeKeyframe *key,
									 const QVariant &new_value,
									 const QVariant &old_value);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeKeyframe *key_;

	QVariant old_value_;
	QVariant new_value_;
};

class NodeParamSetStandardValueCommand : public UndoCommand {
public:
	NodeParamSetStandardValueCommand(const NodeKeyframeTrackReference &input,
									 const QVariant &value);
	NodeParamSetStandardValueCommand(const NodeKeyframeTrackReference &input,
									 const QVariant &new_value,
									 const QVariant &old_value);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;
	virtual void undo() override;

private:
	NodeKeyframeTrackReference ref_;

	QVariant old_value_;
	QVariant new_value_;
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

	virtual Project *get_relevant_project() const override
	{
		return ref_.node()->project();
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
	NodeParamArrayAppendCommand(Node *node, const QString &input);

	virtual Project *get_relevant_project() const override;

protected:
	virtual void redo() override;

	virtual void undo() override;

private:
	Node *node_;

	QString input_;
};

class NodeSetValueHintCommand : public UndoCommand {
public:
	NodeSetValueHintCommand(const NodeInput &input, const Node::ValueHint &hint)
		: input_(input)
		, new_hint_(hint)
	{
	}

	NodeSetValueHintCommand(Node *node, const QString &input, int element,
							const Node::ValueHint &hint)
		: NodeSetValueHintCommand(NodeInput(node, input, element), hint)
	{
	}

	virtual Project *get_relevant_project() const override
	{
		return input_.node()->project();
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

	virtual Project *get_relevant_project() const override
	{
		return nullptr;
	}

protected:
	virtual void prepare() override;

	virtual void redo() override;

	virtual void undo() override;

private:
	NodeInputImmediate *immediate_;

	Node *node_;

	QObject memory_manager_;

	QVector<NodeKeyframe *> keys_;
};

}

#endif // OAK_NODEUNDO_H
