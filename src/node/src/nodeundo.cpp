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

#include "nodeundo.h"

#include <cstdio>
#include <cstdlib>

namespace olive
{

void NodeSetPositionCommand::redo()
{
	added_ = !context_->context_contains_node(node_);

	if (!added_) {
		old_pos_ = context_->get_node_position_data_in_context(node_);
	}

	context_->set_node_position_in_context(node_, pos_);
}

void NodeSetPositionCommand::undo()
{
	if (added_) {
		context_->remove_node_from_context(node_);
	} else {
		context_->set_node_position_in_context(node_, old_pos_);
	}
}

void NodeRemovePositionFromContextCommand::redo()
{
	contained_ = context_->context_contains_node(node_);

	if (contained_) {
		old_pos_ = context_->get_node_position_data_in_context(node_);
		context_->remove_node_from_context(node_);
	}
}

void NodeRemovePositionFromContextCommand::undo()
{
	if (contained_) {
		context_->set_node_position_in_context(node_, old_pos_);
	}
}

void NodeRemovePositionFromAllContextsCommand::redo()
{
	Project *graph = node_->parent();

	for (Node *context : graph->nodes()) {
		if (context->context_contains_node(node_)) {
			contexts_.insert(
				{ context, context->get_node_position_in_context(node_) });
			context->remove_node_from_context(node_);
		}
	}
}

void NodeRemovePositionFromAllContextsCommand::undo()
{
	for (auto it = contexts_.crbegin(); it != contexts_.crend(); it++) {
		it->first->set_node_position_in_context(node_, it->second);
	}

	contexts_.clear();
}

void NodeSetPositionAndDependenciesRecursivelyCommand::prepare()
{
	move_recursively(
		node_,
		pos_.position - context_->get_node_position_data_in_context(node_).position);
}

void NodeSetPositionAndDependenciesRecursivelyCommand::redo()
{
	for (auto it = commands_.cbegin(); it != commands_.cend(); it++) {
		(*it)->redo_now();
	}
}

void NodeSetPositionAndDependenciesRecursivelyCommand::undo()
{
	for (auto it = commands_.crbegin(); it != commands_.crend(); it++) {
		(*it)->undo_now();
	}
}

void NodeSetPositionAndDependenciesRecursivelyCommand::move_recursively(
	Node *node, const PointF &diff)
{
	Node::Position pos = context_->get_node_position_data_in_context(node);
	pos += diff;
	commands_.push_back(new NodeSetPositionCommand(node, context_, pos));

	for (auto it = node->input_connections().cbegin();
		 it != node->input_connections().cend(); it++) {
		Node *output = it->second;
		if (context_->context_contains_node(output)) {
			move_recursively(output, diff);
		}
	}
}

NodeEdgeAddCommand::NodeEdgeAddCommand(Node *output, const NodeInput &input)
	: output_(output)
	, input_(input)
	, remove_command_(nullptr)
{
}

NodeEdgeAddCommand::~NodeEdgeAddCommand()
{
	delete remove_command_;
}

void NodeEdgeAddCommand::redo()
{
	if (std::getenv("OAK_DEBUG_EDGES")) {
		fprintf(stderr, "EDGE-DEBUG: NodeEdgeAddCommand::redo %p -> %p (%s)\n",
				(void *)output_, (void *)input_.node(), input_.input().c_str());
	}

	if (input_.is_connected()) {
		if (!remove_command_) {
			remove_command_ =
				new NodeEdgeRemoveCommand(input_.get_connected_output(), input_);
		}

		remove_command_->redo_now();
	}

	Node::connect_edge(output_, input_);
}

void NodeEdgeAddCommand::undo()
{
	if (std::getenv("OAK_DEBUG_EDGES")) {
		fprintf(stderr, "EDGE-DEBUG: NodeEdgeAddCommand::undo %p -> %p (%s)\n",
				(void *)output_, (void *)input_.node(), input_.input().c_str());
	}

	Node::disconnect_edge(output_, input_);

	if (remove_command_) {
		remove_command_->undo_now();
	}
}

NodeEdgeRemoveCommand::NodeEdgeRemoveCommand(Node *output,
											 const NodeInput &input)
	: output_(output)
	, input_(input)
{
}

void NodeEdgeRemoveCommand::redo()
{
	if (std::getenv("OAK_DEBUG_EDGES")) {
		fprintf(stderr, "EDGE-DEBUG: NodeEdgeRemoveCommand::redo %p -> %p (%s)\n",
				(void *)output_, (void *)input_.node(), input_.input().c_str());
	}

	Node::disconnect_edge(output_, input_);
}

void NodeEdgeRemoveCommand::undo()
{
	if (std::getenv("OAK_DEBUG_EDGES")) {
		fprintf(stderr, "EDGE-DEBUG: NodeEdgeRemoveCommand::undo %p -> %p (%s)\n",
				(void *)output_, (void *)input_.node(), input_.input().c_str());
	}

	Node::connect_edge(output_, input_);
}

NodeAddCommand::NodeAddCommand(Project *graph, Node *node)
	: graph_(graph)
	, node_(node)
{
	// Ensures that when this command is destroyed, if redo() is never called again, the node will be destroyed too
	if (Project *old_graph = node_->parent()) {
		old_graph->remove_node(node_);
	}
	owned_node_.reset(node_);
}

void NodeAddCommand::redo()
{
	graph_->add_node(owned_node_.release());
}

void NodeAddCommand::undo()
{
	graph_->remove_node(node_);
	owned_node_.reset(node_);
}

void NodeRemoveAndDisconnectCommand::prepare()
{
	command_ = new MultiUndoCommand();

	// If this is a block, remove all links
	if (node_->has_links()) {
		command_->add_child(new NodeUnlinkAllCommand(node_));
	}

	// Disconnect everything
	for (auto it = node_->input_connections().cbegin();
		 it != node_->input_connections().cend(); it++) {
		command_->add_child(new NodeEdgeRemoveCommand(it->second, it->first));
	}

	for (const Node::OutputConnection &conn : node_->output_connections()) {
		command_->add_child(new NodeEdgeRemoveCommand(conn.first, conn.second));
	}

	command_->add_child(new NodeRemovePositionFromAllContextsCommand(node_));
}

void NodeRenameCommand::add_node(Node *node, const std::string &new_name)
{
	nodes_.push_back(node);
	new_labels_.push_back(new_name);
	old_labels_.push_back(node->get_label());
}

void NodeRenameCommand::redo()
{
	for (size_t i = 0; i < nodes_.size(); i++) {
		nodes_.at(i)->set_label(new_labels_.at(i));
	}
}

void NodeRenameCommand::undo()
{
	for (size_t i = 0; i < nodes_.size(); i++) {
		nodes_.at(i)->set_label(old_labels_.at(i));
	}
}

NodeOverrideColorCommand::NodeOverrideColorCommand(Node *node, int index)
	: node_(node)
	, new_index_(index)
{
}

void NodeOverrideColorCommand::redo()
{
	old_index_ = node_->get_override_color();
	node_->set_override_color(new_index_);
}

void NodeOverrideColorCommand::undo()
{
	node_->set_override_color(old_index_);
}

NodeViewDeleteCommand::NodeViewDeleteCommand()
{
}

void NodeViewDeleteCommand::add_node(Node *node, Node *context)
{
	if (contains_node(node, context)) {
		return;
	}

	Node::ContextPair p = { node, context };
	nodes_.push_back(p);

	for (auto it = node->input_connections().cbegin();
		 it != node->input_connections().cend(); it++) {
		if (context->context_contains_node(it->second)) {
			add_edge(it->second, it->first);
		}
	}

	for (auto it = node->output_connections().cbegin();
		 it != node->output_connections().cend(); it++) {
		if (context->context_contains_node(it->second.node())) {
			add_edge(it->first, it->second);
		}
	}
}

void NodeViewDeleteCommand::add_edge(Node *output, const NodeInput &input)
{
	for (const Node::OutputConnection &edge : edges_) {
		if (edge.first == output && edge.second == input) {
			return;
		}
	}

	edges_.push_back({ output, input });
}

bool NodeViewDeleteCommand::contains_node(Node *node, Node *context)
{
	for (const Node::ContextPair &pair : nodes_) {
		if (pair.node == node && pair.context == context) {
			return true;
		}
	}

	return false;
}

void NodeViewDeleteCommand::redo()
{
	for (const Node::OutputConnection &edge : edges_) {
		Node::disconnect_edge(edge.first, edge.second);
	}

	for (const Node::ContextPair &pair : nodes_) {
		RemovedNode rn;

		rn.node = pair.node;
		rn.context = pair.context;
		rn.pos = rn.context->get_node_position_in_context(rn.node);

		rn.context->remove_node_from_context(rn.node);

		// If node is no longer in any contexts and is not connected to anything, remove it
		if (rn.node->parent()->get_number_of_contexts_node_is_in(rn.node, true) ==
				0 &&
			rn.node->input_connections().empty() &&
			rn.node->output_connections().empty()) {
			rn.removed_from_graph = rn.node->parent();
			rn.removed_from_graph->remove_node(rn.node);
			owned_nodes_.emplace_back(rn.node);
		} else {
			rn.removed_from_graph = nullptr;
		}

		removed_nodes_.push_back(rn);
	}
}

void NodeViewDeleteCommand::undo()
{
	for (auto rn = removed_nodes_.crbegin(); rn != removed_nodes_.crend();
		 rn++) {
		if (rn->removed_from_graph) {
			rn->removed_from_graph->add_node(rn->node);
		}

		rn->context->set_node_position_in_context(rn->node, rn->pos);
	}
	removed_nodes_.clear();

	for (auto &node : owned_nodes_) {
		node.release();
	}
	owned_nodes_.clear();

	for (auto edge = edges_.crbegin(); edge != edges_.crend(); edge++) {
		Node::connect_edge(edge->first, edge->second);
	}
}

NodeParamSetKeyframingCommand::NodeParamSetKeyframingCommand(
	const NodeInput &input, bool setting)
	: input_(input)
	, new_setting_(setting)
{
}

void NodeParamSetKeyframingCommand::redo()
{
	old_setting_ = input_.is_keyframing();
	input_.node()->set_input_is_keyframing(input_, new_setting_);
}

void NodeParamSetKeyframingCommand::undo()
{
	input_.node()->set_input_is_keyframing(input_, old_setting_);
}

NodeParamSetKeyframeValueCommand::NodeParamSetKeyframeValueCommand(
	NodeKeyframe *key, const Variant &value)
	: key_(key)
	, old_value_(key_->value())
	, new_value_(value)
{
}

NodeParamSetKeyframeValueCommand::NodeParamSetKeyframeValueCommand(
	NodeKeyframe *key, const Variant &new_value, const Variant &old_value)
	: key_(key)
	, old_value_(old_value)
	, new_value_(new_value)
{
}

void NodeParamSetKeyframeValueCommand::redo()
{
	key_->set_value(new_value_);
}

void NodeParamSetKeyframeValueCommand::undo()
{
	key_->set_value(old_value_);
}

NodeParamInsertKeyframeCommand::NodeParamInsertKeyframeCommand(
	Node *node, NodeKeyframe *keyframe)
	: input_(node)
	, keyframe_(keyframe)
{
	// Take ownership of the keyframe
	undo();
}

void NodeParamInsertKeyframeCommand::redo()
{
	input_->add_keyframe(keyframe_);
	owned_keyframe_.release();
}

void NodeParamInsertKeyframeCommand::undo()
{
	if (Node *parent = keyframe_->parent()) {
		parent->remove_keyframe(keyframe_);
	}
	owned_keyframe_.reset(keyframe_);
}

NodeParamRemoveKeyframeCommand::NodeParamRemoveKeyframeCommand(
	NodeKeyframe *keyframe)
	: input_(keyframe->parent())
	, keyframe_(keyframe)
{
}

void NodeParamRemoveKeyframeCommand::redo()
{
	// Removes from input
	if (Node *parent = keyframe_->parent()) {
		parent->remove_keyframe(keyframe_);
	}
	owned_keyframe_.reset(keyframe_);
}

void NodeParamRemoveKeyframeCommand::undo()
{
	input_->add_keyframe(keyframe_);
	owned_keyframe_.release();
}

NodeParamSetKeyframeTimeCommand::NodeParamSetKeyframeTimeCommand(
	NodeKeyframe *key, const Rational &time)
	: key_(key)
	, old_time_(key->time())
	, new_time_(time)
{
}

NodeParamSetKeyframeTimeCommand::NodeParamSetKeyframeTimeCommand(
	NodeKeyframe *key, const Rational &new_time, const Rational &old_time)
	: key_(key)
	, old_time_(old_time)
	, new_time_(new_time)
{
}

void NodeParamSetKeyframeTimeCommand::redo()
{
	key_->set_time(new_time_);
}

void NodeParamSetKeyframeTimeCommand::undo()
{
	key_->set_time(old_time_);
}

NodeParamSetStandardValueCommand::NodeParamSetStandardValueCommand(
	const NodeKeyframeTrackReference &input, const Variant &value)
	: ref_(input)
	, old_value_(ref_.input().node()->get_standard_value(ref_.input()))
	, new_value_(value)
{
}

NodeParamSetStandardValueCommand::NodeParamSetStandardValueCommand(
	const NodeKeyframeTrackReference &input, const Variant &new_value,
	const Variant &old_value)
	: ref_(input)
	, old_value_(old_value)
	, new_value_(new_value)
{
}

void NodeParamSetStandardValueCommand::redo()
{
	ref_.input().node()->set_split_standard_value_on_track(ref_, new_value_);
}

void NodeParamSetStandardValueCommand::undo()
{
	ref_.input().node()->set_split_standard_value_on_track(ref_, old_value_);
}

NodeParamArrayAppendCommand::NodeParamArrayAppendCommand(Node *node,
														 const std::string &input)
	: node_(node)
	, input_(input)
{
}

void NodeParamArrayAppendCommand::redo()
{
	node_->input_array_append(input_);
}

void NodeParamArrayAppendCommand::undo()
{
	node_->input_array_remove_last(input_);
}

void NodeSetValueHintCommand::redo()
{
	old_hint_ =
		input_.node()->get_value_hint_for_input(input_.input(), input_.element());
	input_.node()->set_value_hint_for_input(input_.input(), new_hint_,
										input_.element());
}

void NodeSetValueHintCommand::undo()
{
	input_.node()->set_value_hint_for_input(input_.input(), old_hint_,
										input_.element());
}

void NodeImmediateRemoveAllKeyframesCommand::prepare()
{
	for (const NodeKeyframeTrack &track : immediate_->keyframe_tracks()) {
		keys_.insert(keys_.end(), track.begin(), track.end());
	}

	if (!keys_.empty()) {
		node_ = keys_.front()->parent();
	}
}

void NodeImmediateRemoveAllKeyframesCommand::redo()
{
	for (auto it = keys_.cbegin(); it != keys_.cend(); it++) {
		node_->remove_keyframe(*it);
		owned_keyframes_.emplace_back(*it);
	}
}

void NodeImmediateRemoveAllKeyframesCommand::undo()
{
	for (auto it = keys_.crbegin(); it != keys_.crend(); it++) {
		node_->add_keyframe(*it);
	}
	for (auto &key : owned_keyframes_) {
		key.release();
	}
	owned_keyframes_.clear();
}

}
