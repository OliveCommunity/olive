#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QPointF>
#include <QThread>

#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/generator/text/textv3.h"
#include "node/inputimmediate.h"
#include "node/keyframe.h"
#include "node/math/math/math.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/folder/folder.h"

class NodeUndoTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	template <typename T> T *add_node()
	{
		T *node = new T();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<olive::Project> project_;
};

TEST_F(NodeUndoTest, AddCommandAddsAndRemovesNodeFromProject)
{
	auto *node = new olive::MathNode(); // Intentionally parentless

	olive::NodeAddCommand cmd(project_.get(), node);

	// The constructor takes ownership of the node without adding it to the graph
	EXPECT_FALSE(project_->nodes().contains(node));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.push_to_thread(QCoreApplication::instance()->thread());

	cmd.redo_now();
	EXPECT_TRUE(project_->nodes().contains(node));
	EXPECT_EQ(node->project(), project_.get());

	cmd.undo_now();
	EXPECT_FALSE(project_->nodes().contains(node));
	EXPECT_EQ(node->project(), nullptr);
	// cmd's destructor disposes of the unparented node
}

TEST_F(NodeUndoTest, RemoveAndDisconnectCommandRestoresGraphState)
{
	auto *src = add_node<olive::SolidGenerator>();
	auto *mid = add_node<olive::MathNode>();
	auto *dst = add_node<olive::MathNode>();
	auto *link_peer = add_node<olive::SolidGenerator>();
	auto *context = add_node<olive::Folder>();

	olive::Node::connect_edge(
		src, olive::NodeInput(mid, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(
		mid, olive::NodeInput(dst, olive::MathNode::k_param_b_in));
	olive::Node::link(mid, link_peer);
	context->set_node_position_in_context(
		mid, olive::Node::Position(QPointF(3.0, 4.0), true));

	olive::NodeRemoveAndDisconnectCommand cmd(mid);
	cmd.redo_now();

	EXPECT_EQ(cmd.get_relevant_project(), project_.get());
	EXPECT_EQ(mid->project(), nullptr);
	EXPECT_FALSE(project_->nodes().contains(mid));
	EXPECT_TRUE(mid->input_connections().empty());
	EXPECT_TRUE(mid->output_connections().empty());
	EXPECT_TRUE(src->output_connections().empty());
	EXPECT_TRUE(dst->input_connections().empty());
	EXPECT_FALSE(context->context_contains_node(mid));
	EXPECT_FALSE(mid->has_links());

	cmd.undo_now();

	EXPECT_EQ(mid->project(), project_.get());
	EXPECT_TRUE(project_->nodes().contains(mid));
	ASSERT_EQ(src->output_connections().size(), 1);
	EXPECT_EQ(src->output_connections().front().second,
			  olive::NodeInput(mid, olive::MathNode::k_param_a_in));
	EXPECT_EQ(dst->input_connections().at(
				  olive::NodeInput(dst, olive::MathNode::k_param_b_in)),
			  mid);
	ASSERT_TRUE(context->context_contains_node(mid));
	EXPECT_EQ(context->get_node_position_in_context(mid), QPointF(3.0, 4.0));
	EXPECT_TRUE(olive::Node::are_linked(mid, link_peer));
}

TEST_F(NodeUndoTest, RemoveWithExclusiveDependenciesRemovesUpstreamChain)
{
	auto *src = add_node<olive::SolidGenerator>();
	auto *dep = add_node<olive::MathNode>();
	auto *node = add_node<olive::MathNode>();

	olive::Node::connect_edge(
		src, olive::NodeInput(dep, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(
		dep, olive::NodeInput(node, olive::MathNode::k_param_a_in));

	olive::NodeRemoveWithExclusiveDependenciesAndDisconnect cmd(node);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();

	// The node and all of its non-item upstream dependencies are removed
	EXPECT_EQ(node->project(), nullptr);
	EXPECT_EQ(dep->project(), nullptr);
	EXPECT_EQ(src->project(), nullptr);
	EXPECT_TRUE(node->input_connections().empty());
	EXPECT_TRUE(dep->input_connections().empty());
	EXPECT_TRUE(src->output_connections().empty());
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.undo_now();

	EXPECT_EQ(node->project(), project_.get());
	EXPECT_EQ(dep->project(), project_.get());
	EXPECT_EQ(src->project(), project_.get());
	EXPECT_EQ(node->input_connections().at(
				  olive::NodeInput(node, olive::MathNode::k_param_a_in)),
			  dep);
	EXPECT_EQ(dep->input_connections().at(
				  olive::NodeInput(dep, olive::MathNode::k_param_a_in)),
			  src);
}

TEST_F(NodeUndoTest, EdgeAddCommandConnectsAndDisconnects)
{
	auto *output = add_node<olive::SolidGenerator>();
	auto *input_node = add_node<olive::MathNode>();
	const olive::NodeInput input(input_node, olive::MathNode::k_param_a_in);

	olive::NodeEdgeAddCommand cmd(output, input);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_TRUE(input.is_connected());
	EXPECT_EQ(input.get_connected_output(), output);
	EXPECT_EQ(output->output_connections().size(), 1);

	cmd.undo_now();
	EXPECT_FALSE(input.is_connected());
	EXPECT_TRUE(output->output_connections().empty());
}

TEST_F(NodeUndoTest, EdgeAddCommandReplacesExistingConnection)
{
	auto *first = add_node<olive::SolidGenerator>();
	auto *second = add_node<olive::MathNode>();
	auto *input_node = add_node<olive::MathNode>();
	const olive::NodeInput input(input_node, olive::MathNode::k_param_a_in);

	olive::Node::connect_edge(first, input);
	ASSERT_EQ(input.get_connected_output(), first);

	olive::NodeEdgeAddCommand cmd(second, input);
	cmd.redo_now();

	// The previous edge must be disconnected before the new one is made
	EXPECT_EQ(input.get_connected_output(), second);
	EXPECT_TRUE(first->output_connections().empty());

	cmd.undo_now();

	// Undoing must restore the connection that was replaced
	EXPECT_EQ(input.get_connected_output(), first);
	EXPECT_TRUE(second->output_connections().empty());
}

TEST_F(NodeUndoTest, EdgeRemoveCommandDisconnectsAndReconnects)
{
	auto *output = add_node<olive::SolidGenerator>();
	auto *input_node = add_node<olive::MathNode>();
	const olive::NodeInput input(input_node, olive::MathNode::k_param_b_in);

	olive::Node::connect_edge(output, input);
	ASSERT_TRUE(input.is_connected());

	olive::NodeEdgeRemoveCommand cmd(output, input);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_FALSE(input.is_connected());
	EXPECT_TRUE(output->output_connections().empty());

	cmd.undo_now();
	EXPECT_TRUE(input.is_connected());
	EXPECT_EQ(input.get_connected_output(), output);
}

TEST_F(NodeUndoTest, SetPositionCommandAddsNodeToContext)
{
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();
	ASSERT_FALSE(context->context_contains_node(node));

	olive::NodeSetPositionCommand cmd(
		node, context, olive::Node::Position(QPointF(10.0, 20.0), true));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_TRUE(context->context_contains_node(node));
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(10.0, 20.0));
	EXPECT_TRUE(context->is_node_expanded_in_context(node));

	cmd.undo_now();
	EXPECT_FALSE(context->context_contains_node(node));
}

TEST_F(NodeUndoTest, SetPositionCommandRestoresPreviousPosition)
{
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();
	context->set_node_position_in_context(
		node, olive::Node::Position(QPointF(1.0, 2.0)));

	olive::NodeSetPositionCommand cmd(
		node, context, olive::Node::Position(QPointF(30.0, 40.0)));

	cmd.redo_now();
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(30.0, 40.0));

	cmd.undo_now();
	ASSERT_TRUE(context->context_contains_node(node));
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(1.0, 2.0));
}

TEST_F(NodeUndoTest, SetPositionAndDependenciesRecursivelyMovesNode)
{
	auto *dep = add_node<olive::MathNode>();
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();

	olive::Node::connect_edge(
		dep, olive::NodeInput(node, olive::MathNode::k_param_a_in));

	context->set_node_position_in_context(
		dep, olive::Node::Position(QPointF(1.0, 1.0)));
	context->set_node_position_in_context(
		node, olive::Node::Position(QPointF(2.0, 3.0)));

	olive::NodeSetPositionAndDependenciesRecursivelyCommand cmd(
		node, context, olive::Node::Position(QPointF(8.0, 9.0)));

	cmd.redo_now();
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(8.0, 9.0));
	// The dependency moves by the same delta as the node
	EXPECT_EQ(context->get_node_position_in_context(dep), QPointF(7.0, 7.0));

	cmd.undo_now();
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(2.0, 3.0));
	EXPECT_EQ(context->get_node_position_in_context(dep), QPointF(1.0, 1.0));
}

TEST_F(NodeUndoTest, RemovePositionFromContextCommandRestoresPosition)
{
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();
	context->set_node_position_in_context(
		node, olive::Node::Position(QPointF(5.0, 6.0)));

	olive::NodeRemovePositionFromContextCommand cmd(node, context);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_FALSE(context->context_contains_node(node));

	cmd.undo_now();
	ASSERT_TRUE(context->context_contains_node(node));
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(5.0, 6.0));
}

TEST_F(NodeUndoTest, RemovePositionFromContextCommandNoOpWhenAbsent)
{
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();

	olive::NodeRemovePositionFromContextCommand cmd(node, context);

	cmd.redo_now();
	EXPECT_FALSE(context->context_contains_node(node));

	cmd.undo_now();
	EXPECT_FALSE(context->context_contains_node(node));
}

TEST_F(NodeUndoTest, RemovePositionFromAllContextsCommandRestoresAll)
{
	auto *node = add_node<olive::MathNode>();
	auto *ctx_a = add_node<olive::Folder>();
	auto *ctx_b = add_node<olive::Folder>();
	ctx_a->set_node_position_in_context(
		node, olive::Node::Position(QPointF(1.0, 1.0)));
	ctx_b->set_node_position_in_context(
		node, olive::Node::Position(QPointF(2.0, 2.0)));

	olive::NodeRemovePositionFromAllContextsCommand cmd(node);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_FALSE(ctx_a->context_contains_node(node));
	EXPECT_FALSE(ctx_b->context_contains_node(node));

	cmd.undo_now();
	EXPECT_EQ(ctx_a->get_node_position_in_context(node), QPointF(1.0, 1.0));
	EXPECT_EQ(ctx_b->get_node_position_in_context(node), QPointF(2.0, 2.0));
}

TEST_F(NodeUndoTest, RenameCommandSetsAndRestoresLabels)
{
	auto *a = add_node<olive::MathNode>();
	auto *b = add_node<olive::SolidGenerator>();
	a->set_label(QStringLiteral("old_a"));
	b->set_label(QStringLiteral("old_b"));

	olive::NodeRenameCommand cmd(a, QStringLiteral("new_a"));
	cmd.add_node(b, QStringLiteral("new_b"));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(a->get_label(), QStringLiteral("new_a"));
	EXPECT_EQ(b->get_label(), QStringLiteral("new_b"));

	cmd.undo_now();
	EXPECT_EQ(a->get_label(), QStringLiteral("old_a"));
	EXPECT_EQ(b->get_label(), QStringLiteral("old_b"));
}

TEST_F(NodeUndoTest, RenameCommandEmptyHasNoProject)
{
	olive::NodeRenameCommand cmd;
	EXPECT_EQ(cmd.get_relevant_project(), nullptr);

	// redo/undo on an empty command must be harmless no-ops
	cmd.redo_now();
	cmd.undo_now();
}

TEST_F(NodeUndoTest, OverrideColorCommandSetsAndRestoresColor)
{
	auto *node = add_node<olive::MathNode>();
	ASSERT_EQ(node->get_override_color(), -1);

	olive::NodeOverrideColorCommand cmd(node, 5);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(node->get_override_color(), 5);

	cmd.undo_now();
	EXPECT_EQ(node->get_override_color(), -1);
}

TEST_F(NodeUndoTest, LinkCommandLinksAndUnlinks)
{
	auto *a = add_node<olive::MathNode>();
	auto *b = add_node<olive::SolidGenerator>();

	olive::NodeLinkCommand link_cmd(a, b, true);
	EXPECT_EQ(link_cmd.get_relevant_project(), project_.get());

	link_cmd.redo_now();
	EXPECT_TRUE(olive::Node::are_linked(a, b));

	link_cmd.undo_now();
	EXPECT_FALSE(olive::Node::are_linked(a, b));

	olive::Node::link(a, b);
	olive::NodeLinkCommand unlink_cmd(a, b, false);

	unlink_cmd.redo_now();
	EXPECT_FALSE(olive::Node::are_linked(a, b));

	unlink_cmd.undo_now();
	EXPECT_TRUE(olive::Node::are_linked(a, b));
}

TEST_F(NodeUndoTest, LinkCommandIgnoresAlreadyLinkedPair)
{
	auto *a = add_node<olive::MathNode>();
	auto *b = add_node<olive::SolidGenerator>();
	olive::Node::link(a, b);

	olive::NodeLinkCommand cmd(a, b, true);
	cmd.redo_now();
	EXPECT_TRUE(olive::Node::are_linked(a, b));

	// Undo must not unlink a pair that redo did not link
	cmd.undo_now();
	EXPECT_TRUE(olive::Node::are_linked(a, b));
}

TEST_F(NodeUndoTest, UnlinkAllCommandRestoresAllLinks)
{
	auto *node = add_node<olive::MathNode>();
	auto *a = add_node<olive::SolidGenerator>();
	auto *b = add_node<olive::SolidGenerator>();
	olive::Node::link(node, a);
	olive::Node::link(node, b);
	ASSERT_EQ(node->links().size(), 2);

	olive::NodeUnlinkAllCommand cmd(node);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_TRUE(node->links().isEmpty());
	EXPECT_FALSE(olive::Node::are_linked(node, a));
	EXPECT_FALSE(olive::Node::are_linked(node, b));

	cmd.undo_now();
	EXPECT_EQ(node->links().size(), 2);
	EXPECT_TRUE(olive::Node::are_linked(node, a));
	EXPECT_TRUE(olive::Node::are_linked(node, b));
}

TEST_F(NodeUndoTest, LinkManyCommandLinksAndUnlinksAllPairs)
{
	auto *a = add_node<olive::MathNode>();
	auto *b = add_node<olive::MathNode>();
	auto *c = add_node<olive::SolidGenerator>();

	olive::NodeLinkManyCommand cmd({ a, b, c }, true);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_TRUE(olive::Node::are_linked(a, b));
	EXPECT_TRUE(olive::Node::are_linked(a, c));
	EXPECT_TRUE(olive::Node::are_linked(b, c));

	cmd.undo_now();
	EXPECT_FALSE(olive::Node::are_linked(a, b));
	EXPECT_FALSE(olive::Node::are_linked(a, c));
	EXPECT_FALSE(olive::Node::are_linked(b, c));
}

TEST_F(NodeUndoTest, ViewDeleteCommandRemovesNodeAndEdges)
{
	auto *context = add_node<olive::Folder>();
	auto *a = add_node<olive::SolidGenerator>();
	auto *b = add_node<olive::MathNode>();

	olive::Node::connect_edge(
		a, olive::NodeInput(b, olive::MathNode::k_param_a_in));
	context->set_node_position_in_context(
		a, olive::Node::Position(QPointF(1.0, 2.0)));
	context->set_node_position_in_context(
		b, olive::Node::Position(QPointF(3.0, 4.0)));

	olive::NodeViewDeleteCommand cmd;
	EXPECT_EQ(cmd.get_relevant_project(), nullptr);

	cmd.add_node(b, context);
	EXPECT_TRUE(cmd.contains_node(b, context));
	EXPECT_FALSE(cmd.contains_node(a, context));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();

	// b was only in this context and became fully disconnected, so it is
	// removed from the graph entirely
	EXPECT_FALSE(context->context_contains_node(b));
	EXPECT_EQ(b->project(), nullptr);
	EXPECT_TRUE(b->input_connections().empty());
	EXPECT_TRUE(a->output_connections().empty());

	cmd.undo_now();

	EXPECT_EQ(b->project(), project_.get());
	ASSERT_TRUE(context->context_contains_node(b));
	EXPECT_EQ(context->get_node_position_in_context(b), QPointF(3.0, 4.0));
	ASSERT_EQ(a->output_connections().size(), 1);
	EXPECT_EQ(a->output_connections().front().second,
			  olive::NodeInput(b, olive::MathNode::k_param_a_in));
}

TEST_F(NodeUndoTest, ViewDeleteCommandKeepsNodeConnectedOutsideContext)
{
	auto *context = add_node<olive::Folder>();
	auto *a = add_node<olive::SolidGenerator>();
	auto *outside = add_node<olive::MathNode>();

	// "outside" is not in the context, so its edge keeps "a" in the graph
	olive::Node::connect_edge(
		a, olive::NodeInput(outside, olive::MathNode::k_param_a_in));
	context->set_node_position_in_context(
		a, olive::Node::Position(QPointF(7.0, 8.0)));

	olive::NodeViewDeleteCommand cmd;
	cmd.add_node(a, context);

	cmd.redo_now();

	EXPECT_FALSE(context->context_contains_node(a));
	EXPECT_EQ(a->project(), project_.get());
	EXPECT_EQ(outside->input_connections().at(
				  olive::NodeInput(outside, olive::MathNode::k_param_a_in)),
			  a);

	cmd.undo_now();

	ASSERT_TRUE(context->context_contains_node(a));
	EXPECT_EQ(context->get_node_position_in_context(a), QPointF(7.0, 8.0));
}

TEST_F(NodeUndoTest, ParamSetKeyframingCommandTogglesKeyframing)
{
	auto *node = add_node<olive::MathNode>();
	const olive::NodeInput input(node, olive::MathNode::k_param_a_in);
	ASSERT_FALSE(input.is_keyframing());

	olive::NodeParamSetKeyframingCommand cmd(input, true);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_TRUE(input.is_keyframing());

	cmd.undo_now();
	EXPECT_FALSE(input.is_keyframing());
}

TEST_F(NodeUndoTest, ParamInsertKeyframeCommandReparentsKeyframe)
{
	auto *node = add_node<olive::MathNode>();

	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);

	olive::NodeParamInsertKeyframeCommand cmd(node, key);

	// The constructor takes ownership of the keyframe without inserting it
	EXPECT_NE(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.contains(key));

	cmd.undo_now();
	EXPECT_NE(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());
}

TEST_F(NodeUndoTest, ParamRemoveKeyframeCommandRestoresKeyframe)
{
	auto *node = add_node<olive::MathNode>();

	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);
	ASSERT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.contains(key));

	olive::NodeParamRemoveKeyframeCommand cmd(key);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_NE(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());

	cmd.undo_now();
	EXPECT_EQ(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.contains(key));
}

TEST_F(NodeUndoTest, ParamSetKeyframeTimeCommandChangesTime)
{
	auto *node = add_node<olive::MathNode>();
	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);

	olive::NodeParamSetKeyframeTimeCommand cmd(key, olive::Rational(1, 2));
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(key->time(), olive::Rational(1, 2));

	cmd.undo_now();
	EXPECT_EQ(key->time(), olive::Rational(0));
}

TEST_F(NodeUndoTest, ParamSetKeyframeTimeCommandExplicitTimes)
{
	auto *node = add_node<olive::MathNode>();
	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);

	olive::NodeParamSetKeyframeTimeCommand cmd(key, olive::Rational(3, 4),
											   olive::Rational(1, 4));

	cmd.redo_now();
	EXPECT_EQ(key->time(), olive::Rational(3, 4));

	cmd.undo_now();
	EXPECT_EQ(key->time(), olive::Rational(1, 4));
}

TEST_F(NodeUndoTest, ParamSetKeyframeValueCommandChangesValue)
{
	auto *node = add_node<olive::MathNode>();
	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);

	olive::NodeParamSetKeyframeValueCommand cmd(key, 5.0);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 5.0);

	cmd.undo_now();
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 1.0);
}

TEST_F(NodeUndoTest, ParamSetKeyframeValueCommandExplicitValues)
{
	auto *node = add_node<olive::MathNode>();
	auto *key = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										olive::NodeKeyframe::k_linear, 0, -1,
										olive::MathNode::k_param_a_in);
	key->setParent(node);

	olive::NodeParamSetKeyframeValueCommand cmd(key, 7.5, 2.5);

	cmd.redo_now();
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 7.5);

	cmd.undo_now();
	EXPECT_DOUBLE_EQ(key->value().toDouble(), 2.5);
}

TEST_F(NodeUndoTest, ParamSetStandardValueCommandSetsAndRestoresValue)
{
	auto *node = add_node<olive::MathNode>();
	const olive::NodeKeyframeTrackReference ref(
		olive::NodeInput(node, olive::MathNode::k_param_a_in), 0);
	ASSERT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 0.0);

	olive::NodeParamSetStandardValueCommand cmd(ref, 2.5);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 2.5);

	cmd.undo_now();
	EXPECT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 0.0);
}

TEST_F(NodeUndoTest, ParamSetStandardValueCommandExplicitOldValue)
{
	auto *node = add_node<olive::MathNode>();
	node->set_standard_value(olive::MathNode::k_param_a_in, 10.0);
	const olive::NodeKeyframeTrackReference ref(
		olive::NodeInput(node, olive::MathNode::k_param_a_in), 0);

	// Three-argument form with an explicit old value, as used by
	// SpeedDurationDialog
	olive::NodeParamSetStandardValueCommand cmd(ref, 20.0, 10.0);

	cmd.redo_now();
	EXPECT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 20.0);

	cmd.undo_now();
	EXPECT_DOUBLE_EQ(
		node->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 10.0);
}

TEST_F(NodeUndoTest, ParamSetSplitStandardValueCommandSetsAndRestoresSplit)
{
	auto *node = add_node<olive::SolidGenerator>();
	const olive::NodeInput input(node, olive::SolidGenerator::k_color_input);

	const olive::SplitValue old_split = node->get_split_standard_value(input);
	ASSERT_EQ(old_split.size(), 4);

	const olive::SplitValue new_split = { 0.25, 0.5, 0.75, 1.0 };

	olive::NodeParamSetSplitStandardValueCommand cmd(input, new_split);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	const olive::SplitValue after = node->get_split_standard_value(input);
	ASSERT_EQ(after.size(), 4);
	EXPECT_DOUBLE_EQ(after.at(0).toDouble(), 0.25);
	EXPECT_DOUBLE_EQ(after.at(1).toDouble(), 0.5);
	EXPECT_DOUBLE_EQ(after.at(2).toDouble(), 0.75);
	EXPECT_DOUBLE_EQ(after.at(3).toDouble(), 1.0);

	cmd.undo_now();
	const olive::SplitValue restored = node->get_split_standard_value(input);
	ASSERT_EQ(restored.size(), 4);
	for (int i = 0; i < restored.size(); ++i) {
		EXPECT_DOUBLE_EQ(restored.at(i).toDouble(),
						 old_split.at(i).toDouble());
	}

	// The three-argument form takes the old value explicitly
	const olive::SplitValue explicit_new = { 1.0, 1.0, 1.0, 1.0 };
	olive::NodeParamSetSplitStandardValueCommand explicit_cmd(input,
															  explicit_new,
															  restored);
	explicit_cmd.redo_now();
	EXPECT_DOUBLE_EQ(node->get_split_standard_value(input).at(0).toDouble(), 1.0);

	explicit_cmd.undo_now();
	EXPECT_DOUBLE_EQ(node->get_split_standard_value(input).at(0).toDouble(),
					 restored.at(0).toDouble());
}

TEST_F(NodeUndoTest, ParamArrayAppendCommandAppendsAndRemoves)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	const int base = node->input_array_size(olive::TextGeneratorV3::k_args_input);

	olive::NodeParamArrayAppendCommand cmd(node,
										   olive::TextGeneratorV3::k_args_input);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 1);

	cmd.undo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), base);
}

TEST_F(NodeUndoTest, ArrayInsertCommandInsertsAndRemovesElement)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	node->input_array_append(olive::TextGeneratorV3::k_args_input);
	const int base = node->input_array_size(olive::TextGeneratorV3::k_args_input);

	olive::NodeArrayInsertCommand cmd(node, olive::TextGeneratorV3::k_args_input,
									  0);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 1);

	cmd.undo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), base);
}

TEST_F(NodeUndoTest, ArrayResizeCommandGrowAndShrink)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	const int base = node->input_array_size(olive::TextGeneratorV3::k_args_input);

	olive::NodeArrayResizeCommand cmd(node, olive::TextGeneratorV3::k_args_input,
									  base + 3);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 3);

	cmd.undo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), base);
}

TEST_F(NodeUndoTest, ArrayResizeCommandShrinkDisconnectsAndRestoresEdges)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	auto *output = add_node<olive::MathNode>();

	node->input_array_resize(olive::TextGeneratorV3::k_args_input, 3);
	const olive::NodeInput connected(node,
									 olive::TextGeneratorV3::k_args_input, 2);
	olive::Node::connect_edge(output, connected);
	ASSERT_TRUE(connected.is_connected());

	olive::NodeArrayResizeCommand cmd(node, olive::TextGeneratorV3::k_args_input,
									  1);
	cmd.redo_now();

	// Shrinking removed elements 1 and 2; the edge into element 2 is dropped
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 1);
	EXPECT_FALSE(connected.is_connected());
	EXPECT_TRUE(output->output_connections().empty());

	cmd.undo_now();

	// Undo restores both the array size and the removed connection
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 3);
	EXPECT_TRUE(connected.is_connected());
	EXPECT_EQ(connected.get_connected_output(), output);
}

TEST_F(NodeUndoTest, ArrayRemoveCommandPreservesKeyframesAndValues)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	node->input_array_resize(olive::TextGeneratorV3::k_args_input, 2);

	const olive::NodeInput element(node, olive::TextGeneratorV3::k_args_input,
								   1);
	node->set_standard_value(element, QStringLiteral("hello"));
	node->set_input_is_keyframing(element, true);

	auto *key = new olive::NodeKeyframe(
		olive::Rational(0), QStringLiteral("key"), olive::NodeKeyframe::k_linear,
		0, 1, olive::TextGeneratorV3::k_args_input);
	key->setParent(node);
	ASSERT_TRUE(node->get_keyframe_tracks(olive::TextGeneratorV3::k_args_input, 1)
					.at(0)
					.contains(key));

	olive::NodeArrayRemoveCommand cmd(node, olive::TextGeneratorV3::k_args_input,
									  1);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 1);
	EXPECT_NE(key->parent(), node);

	cmd.undo_now();
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 2);
	EXPECT_EQ(key->parent(), node);
	EXPECT_TRUE(node->get_keyframe_tracks(olive::TextGeneratorV3::k_args_input, 1)
					.at(0)
					.contains(key));
	EXPECT_TRUE(node->is_input_keyframing(olive::TextGeneratorV3::k_args_input, 1));
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input, 1)
				  .at(0)
				  .toString(),
			  QStringLiteral("hello"));
}

TEST_F(NodeUndoTest, SetValueHintCommandSetsAndRestoresHint)
{
	auto *node = add_node<olive::MathNode>();

	const olive::Node::ValueHint old_hint =
		node->get_value_hint_for_input(olive::MathNode::k_param_a_in, -1);

	const olive::Node::ValueHint new_hint({ olive::NodeValue::k_vec2 }, 3,
										  QStringLiteral("tag"));
	olive::NodeSetValueHintCommand cmd(node, olive::MathNode::k_param_a_in, -1,
									   new_hint);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	const olive::Node::ValueHint after =
		node->get_value_hint_for_input(olive::MathNode::k_param_a_in, -1);
	ASSERT_EQ(after.types().size(), 1);
	EXPECT_EQ(after.types().first(), olive::NodeValue::k_vec2);
	EXPECT_EQ(after.index(), 3);
	EXPECT_EQ(after.tag(), QStringLiteral("tag"));

	cmd.undo_now();
	const olive::Node::ValueHint restored =
		node->get_value_hint_for_input(olive::MathNode::k_param_a_in, -1);
	EXPECT_EQ(restored.types().size(), old_hint.types().size());
	EXPECT_EQ(restored.index(), old_hint.index());
	EXPECT_EQ(restored.tag(), old_hint.tag());
}

TEST_F(NodeUndoTest, ImmediateRemoveAllKeyframesCommandRemovesKeys)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	auto *key_a = new olive::NodeKeyframe(olive::Rational(0), 1.0,
										  olive::NodeKeyframe::k_linear, 0, -1,
										  olive::MathNode::k_param_a_in);
	key_a->setParent(node);
	auto *key_b = new olive::NodeKeyframe(olive::Rational(1), 2.0,
										  olive::NodeKeyframe::k_linear, 0, -1,
										  olive::MathNode::k_param_a_in);
	key_b->setParent(node);

	olive::NodeInputImmediate *immediate =
		node->get_immediate(olive::MathNode::k_param_a_in, -1);
	ASSERT_NE(immediate, nullptr);
	ASSERT_EQ(immediate->keyframe_tracks().at(0).size(), 2);

	olive::NodeImmediateRemoveAllKeyframesCommand cmd(immediate);
	EXPECT_EQ(cmd.get_relevant_project(), nullptr);

	cmd.redo_now();
	EXPECT_TRUE(immediate->keyframe_tracks().at(0).isEmpty());
	EXPECT_NE(key_a->parent(), node);
	EXPECT_NE(key_b->parent(), node);

	cmd.undo_now();
	// Undo restores the keyframes to the node and its tracks
	ASSERT_EQ(immediate->keyframe_tracks().at(0).size(), 2);
	EXPECT_EQ(immediate->keyframe_tracks().at(0).at(0), key_a);
	EXPECT_EQ(immediate->keyframe_tracks().at(0).at(1), key_b);
	EXPECT_EQ(key_a->parent(), node);
	EXPECT_EQ(key_b->parent(), node);
}
