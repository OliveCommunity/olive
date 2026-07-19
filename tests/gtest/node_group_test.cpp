#include <gtest/gtest.h>

#include <memory>

#include <QSignalSpy>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/group/group.h"
#include "node/math/math/math.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/serializeddata.h"
#include "node/value.h"

class NodeGroupTest : public ::testing::Test {
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

	// AddInputPassthrough()/SetOutputPassthrough() assert that the inner node
	// is part of the group's context, so tests always place it there first
	olive::NodeGroup *add_group_with_inner_math(olive::MathNode **math)
	{
		olive::NodeGroup *group = add_node<olive::NodeGroup>();
		*math = add_node<olive::MathNode>();
		group->set_node_position_in_context(*math, olive::Node::Position());
		return group;
	}

	std::unique_ptr<olive::Project> project_;
};

TEST_F(NodeGroupTest, MetadataIsCorrect)
{
	olive::NodeGroup group;

	EXPECT_EQ(group.id(), QStringLiteral("org.olivevideoeditor.Olive.group"));
	EXPECT_EQ(group.name(), QStringLiteral("Group"));
	EXPECT_TRUE(group.category().contains(olive::Node::k_category_unknown));
	EXPECT_FALSE(group.description().isEmpty());
	EXPECT_TRUE(group.get_flags() & olive::Node::k_dont_show_in_create_menu);

	// A fresh group has no passthroughs of either kind
	EXPECT_EQ(group.get_output_passthrough(), nullptr);
	EXPECT_TRUE(group.get_input_passthroughs().isEmpty());
}

TEST_F(NodeGroupTest, AddInputPassthroughRegistersMirroredInput)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	QSignalSpy added_spy(group, &olive::NodeGroup::input_passthrough_added);
	ASSERT_TRUE(added_spy.isValid());

	const olive::NodeInput input(math, olive::MathNode::k_param_a_in);
	const QString id = group->add_input_passthrough(input);

	// The first passthrough of an input reuses the inner input's ID
	EXPECT_EQ(id, olive::MathNode::k_param_a_in);
	EXPECT_TRUE(group->has_input_with_id(id));

	// The group input mirrors the inner input's type, default and flags
	EXPECT_EQ(group->get_input_data_type(id), olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(group->get_default_value(id).toDouble(), 0.0);
	EXPECT_EQ(group->get_input_flags(id).value(),
			  math->get_input_flags(olive::MathNode::k_param_a_in).value());

	// The passthrough is registered for lookup in both directions
	ASSERT_EQ(group->get_input_passthroughs().size(), 1);
	EXPECT_EQ(group->get_input_passthroughs().first().first, id);
	EXPECT_EQ(group->get_input_passthroughs().first().second, input);
	EXPECT_TRUE(group->contains_input_passthrough(input));
	EXPECT_FALSE(group->contains_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_b_in)));
	EXPECT_EQ(group->get_id_of_passthrough(input), id);
	EXPECT_EQ(group->get_input_from_id(id), input);

	// Unknown lookups return empty/invalid results
	EXPECT_TRUE(group->get_id_of_passthrough(
					 olive::NodeInput(math, olive::MathNode::k_param_b_in))
					.isEmpty());
	EXPECT_FALSE(
		group->get_input_from_id(QStringLiteral("no_such_input")).is_valid());

	ASSERT_EQ(added_spy.count(), 1);
	EXPECT_EQ(added_spy.takeFirst().at(1).value<olive::NodeInput>(), input);
}

TEST_F(NodeGroupTest, AddInputPassthroughIsIdempotentForSameInput)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	const olive::NodeInput input(math, olive::MathNode::k_param_a_in);
	const QString first = group->add_input_passthrough(input);

	QSignalSpy added_spy(group, &olive::NodeGroup::input_passthrough_added);
	const QString second = group->add_input_passthrough(input);

	// Passing the same input through twice returns the existing ID
	EXPECT_EQ(first, second);
	EXPECT_EQ(group->get_input_passthroughs().size(), 1);
	EXPECT_EQ(added_spy.count(), 0);
}

TEST_F(NodeGroupTest, AddInputPassthroughGeneratesUniqueIdForDuplicateInputId)
{
	auto *math_a = add_node<olive::MathNode>();
	auto *math_b = add_node<olive::MathNode>();
	auto *group = add_node<olive::NodeGroup>();
	group->set_node_position_in_context(math_a, olive::Node::Position());
	group->set_node_position_in_context(math_b, olive::Node::Position());

	const QString id_a = group->add_input_passthrough(
		olive::NodeInput(math_a, olive::MathNode::k_param_a_in));
	EXPECT_EQ(id_a, olive::MathNode::k_param_a_in);

	// A second passthrough of the same input ID (on a different node) must
	// not collide with the first; the suffix is derived from the input ID
	const QString id_b = group->add_input_passthrough(
		olive::NodeInput(math_b, olive::MathNode::k_param_a_in));

	EXPECT_NE(id_a, id_b);
	EXPECT_EQ(id_b, QStringLiteral("param_a_in_2"));

	ASSERT_EQ(group->get_input_passthroughs().size(), 2);
	EXPECT_TRUE(group->has_input_with_id(id_b));
	EXPECT_EQ(group->get_input_from_id(id_b),
			  olive::NodeInput(math_b, olive::MathNode::k_param_a_in));
	EXPECT_EQ(group->get_id_of_passthrough(
				  olive::NodeInput(math_a, olive::MathNode::k_param_a_in)),
			  id_a);
}

TEST_F(NodeGroupTest, AddInputPassthroughHonorsForcedIdAndMirrorsFlags)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	// kMethodIn is declared with kInputFlagNotConnectable |
	// kInputFlagNotKeyframable, exercising the flag mirroring
	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_method_in),
		QStringLiteral("forced_method"));

	EXPECT_EQ(id, QStringLiteral("forced_method"));
	EXPECT_TRUE(group->has_input_with_id(id));
	EXPECT_EQ(group->get_input_data_type(id), olive::NodeValue::k_combo);
	EXPECT_EQ(group->get_input_flags(id).value(),
			  math->get_input_flags(olive::MathNode::k_method_in).value());
	EXPECT_FALSE(group->is_input_connectable(id));
	EXPECT_FALSE(group->is_input_keyframable(id));
	EXPECT_EQ(group->get_input_from_id(id),
			  olive::NodeInput(math, olive::MathNode::k_method_in));
}

TEST_F(NodeGroupTest, RemoveInputPassthroughMirrorsInputDeletion)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	const olive::NodeInput input(math, olive::MathNode::k_param_a_in);
	const QString id = group->add_input_passthrough(input);

	QSignalSpy removed_spy(group, &olive::NodeGroup::input_passthrough_removed);
	ASSERT_TRUE(removed_spy.isValid());

	group->remove_input_passthrough(input);

	EXPECT_EQ(removed_spy.count(), 1);
	EXPECT_TRUE(group->get_input_passthroughs().isEmpty());
	EXPECT_FALSE(group->contains_input_passthrough(input));
	EXPECT_FALSE(group->has_input_with_id(id));
	EXPECT_TRUE(group->get_id_of_passthrough(input).isEmpty());
	EXPECT_FALSE(group->get_input_from_id(id).is_valid());
}

TEST_F(NodeGroupTest, RemoveInputPassthroughIgnoresUnknownInput)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	QSignalSpy removed_spy(group, &olive::NodeGroup::input_passthrough_removed);

	// An input that was never passed through must be a harmless no-op
	group->remove_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_b_in));
	group->remove_input_passthrough(olive::NodeInput());

	EXPECT_EQ(removed_spy.count(), 0);
	EXPECT_EQ(group->get_input_passthroughs().size(), 1);
	EXPECT_TRUE(group->has_input_with_id(id));
}

TEST_F(NodeGroupTest, SetOutputPassthroughUpdatesAndEmits)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	ASSERT_EQ(group->get_output_passthrough(), nullptr);

	QSignalSpy output_spy(group, &olive::NodeGroup::output_passthrough_changed);
	ASSERT_TRUE(output_spy.isValid());

	group->set_output_passthrough(math);
	EXPECT_EQ(group->get_output_passthrough(), math);
	EXPECT_EQ(output_spy.count(), 1);

	// Clearing the passthrough is allowed
	group->set_output_passthrough(nullptr);
	EXPECT_EQ(group->get_output_passthrough(), nullptr);
	EXPECT_EQ(output_spy.count(), 2);
}

TEST_F(NodeGroupTest, PassthroughInputAcceptsExternalEdges)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// The mirrored input is a real input on the group and can be connected
	// from nodes outside the group
	auto *external = add_node<olive::MathNode>();
	const olive::NodeInput group_input(group, id);
	EXPECT_FALSE(group_input.is_connected());

	olive::Node::connect_edge(external, group_input);
	EXPECT_TRUE(group_input.is_connected());
	EXPECT_EQ(group_input.get_connected_output(), external);
	EXPECT_EQ(external->output_connections().size(), 1);

	olive::Node::disconnect_edge(external, group_input);
	EXPECT_FALSE(group_input.is_connected());
	EXPECT_TRUE(external->output_connections().empty());
}

TEST_F(NodeGroupTest, GetInputNameFallsThroughToInnerNode)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	math->retranslate();

	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// Without an override the name comes from the inner node's input
	EXPECT_EQ(group->get_input_name(id), QStringLiteral("Value"));

	// An explicit override takes precedence
	group->set_input_name(id, QStringLiteral("Custom Name"));
	EXPECT_EQ(group->get_input_name(id), QStringLiteral("Custom Name"));

	// Clearing the override restores the fall-through
	group->set_input_name(id, QString());
	EXPECT_EQ(group->get_input_name(id), QStringLiteral("Value"));
}

TEST_F(NodeGroupTest, GetInputNameResolvesThroughNestedGroups)
{
	auto *math = add_node<olive::MathNode>();
	math->retranslate();

	auto *inner = add_node<olive::NodeGroup>();
	inner->set_node_position_in_context(math, olive::Node::Position());
	const QString inner_id = inner->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	auto *outer = add_node<olive::NodeGroup>();
	outer->set_node_position_in_context(inner, olive::Node::Position());
	const QString outer_id = outer->add_input_passthrough(
		olive::NodeInput(inner, inner_id));

	// The outer group asks the inner group, which asks the math node
	EXPECT_EQ(outer->get_input_name(outer_id), QStringLiteral("Value"));
}

TEST_F(NodeGroupTest, ResolveInputUnwrapsNestedGroups)
{
	auto *math = add_node<olive::MathNode>();

	auto *inner = add_node<olive::NodeGroup>();
	inner->set_node_position_in_context(math, olive::Node::Position());
	const QString inner_id = inner->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	auto *outer = add_node<olive::NodeGroup>();
	outer->set_node_position_in_context(inner, olive::Node::Position());
	const QString outer_id = outer->add_input_passthrough(
		olive::NodeInput(inner, inner_id));

	// An input on the outer group resolves to the innermost real input
	const olive::NodeInput resolved = olive::NodeGroup::resolve_input(
		olive::NodeInput(outer, outer_id));
	EXPECT_EQ(resolved.node(), math);
	EXPECT_EQ(resolved.input(), olive::MathNode::k_param_a_in);
	EXPECT_EQ(resolved.element(), -1);

	// Inputs on regular nodes are returned unchanged
	const olive::NodeInput plain(math, olive::MathNode::k_param_b_in);
	EXPECT_EQ(olive::NodeGroup::resolve_input(plain), plain);
}

TEST_F(NodeGroupTest, GetInnerRejectsNonGroupAndUnknownInputs)
{
	auto *math = add_node<olive::MathNode>();

	auto *group = add_node<olive::NodeGroup>();
	group->set_node_position_in_context(math, olive::Node::Position());
	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// A node that is not a group has no inner input
	olive::NodeInput non_group(math, olive::MathNode::k_param_a_in);
	EXPECT_FALSE(olive::NodeGroup::get_inner(&non_group));
	EXPECT_EQ(non_group.node(), math);

	// An input ID that is not a passthrough is left unchanged
	olive::NodeInput unknown(group, QStringLiteral("does_not_exist"));
	EXPECT_FALSE(olive::NodeGroup::get_inner(&unknown));
	EXPECT_EQ(unknown.node(), group);

	// One level of passthrough resolves to the inner node's input
	olive::NodeInput passthrough(group, id);
	EXPECT_TRUE(olive::NodeGroup::get_inner(&passthrough));
	EXPECT_EQ(passthrough.node(), math);
	EXPECT_EQ(passthrough.input(), olive::MathNode::k_param_a_in);
}

TEST_F(NodeGroupTest, RetranslateRetranslatesContextNodes)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	ASSERT_TRUE(math->get_input_name(olive::MathNode::k_param_a_in).isEmpty());

	group->retranslate();

	// The group retranslates itself and every node in its context
	EXPECT_EQ(group->get_input_name(olive::Node::k_enabled_input),
			  QStringLiteral("Enabled"));
	EXPECT_EQ(math->get_input_name(olive::MathNode::k_param_a_in),
			  QStringLiteral("Value"));
}

TEST_F(NodeGroupTest, SaveCustomWritesInputPassthroughs)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	const QString id = group->add_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in),
		QStringLiteral("pt_float"));
	group->set_input_name(id, QStringLiteral("Custom Name"));
	group->set_default_value(id, 2.5);
	group->set_input_flag(id, olive::k_input_flag_hidden);
	group->set_input_property(id, QStringLiteral("mykey"),
							QStringLiteral("myvalue"));
	group->set_output_passthrough(math);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	group->save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	const QString ptr = QString::number(reinterpret_cast<quintptr>(math));

	EXPECT_TRUE(xml.contains(QStringLiteral("<inputpassthroughs>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<node>%1</node>").arg(ptr)));
	EXPECT_TRUE(xml.contains(
		QStringLiteral("<input>%1</input>").arg(olive::MathNode::k_param_a_in)));
	EXPECT_TRUE(xml.contains(QStringLiteral("<element>-1</element>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<id>pt_float</id>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<name>Custom Name</name>")));
	// 8 == kInputFlagHidden; only flags differing from the inner input are
	// saved, and the inner input has kInputFlagNormal
	EXPECT_TRUE(xml.contains(QStringLiteral("<flags>8</flags>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<type>float</type>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<default>2.5</default>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<key>mykey</key>")));
	EXPECT_TRUE(xml.contains(QStringLiteral("<value>myvalue</value>")));
	EXPECT_TRUE(xml.contains(
		QStringLiteral("<outputpassthrough>%1</outputpassthrough>")
			.arg(ptr)));
}

TEST_F(NodeGroupTest, LoadCustomCollectsGroupLinks)
{
	auto *group = add_node<olive::NodeGroup>();

	const QString xml = QStringLiteral(
		"<custom>"
		"<inputpassthroughs>"
		"<inputpassthrough>"
		"<node>12345</node>"
		"<input>param_a_in</input>"
		"<element>-1</element>"
		"<id>pt_a</id>"
		"<name>Custom A</name>"
		"<flags>8</flags>"
		"<type>float</type>"
		"<default>2.5</default>"
		"<properties>"
		"<property>"
		"<key>mykey</key>"
		"<value>myvalue</value>"
		"</property>"
		"</properties>"
		"</inputpassthrough>"
		"</inputpassthroughs>"
		"<outputpassthrough>12345</outputpassthrough>"
		"</custom>");

	olive::SerializedData data;
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));
	EXPECT_TRUE(group->load_custom(&reader, &data));

	// Loading only records the links; resolution happens in PostLoadEvent
	ASSERT_EQ(data.group_input_links.size(), 1);
	const olive::SerializedData::GroupLink &link =
		data.group_input_links.first();
	EXPECT_EQ(link.group, group);
	EXPECT_EQ(link.passthrough_id, QStringLiteral("pt_a"));
	EXPECT_EQ(link.input_node, static_cast<quintptr>(12345));
	EXPECT_EQ(link.input_id, QStringLiteral("param_a_in"));
	EXPECT_EQ(link.input_element, -1);
	EXPECT_EQ(link.custom_name, QStringLiteral("Custom A"));
	EXPECT_EQ(link.custom_flags.value(), uint64_t(8));
	EXPECT_EQ(link.data_type, olive::NodeValue::k_float);
	EXPECT_DOUBLE_EQ(link.default_val.toDouble(), 2.5);
	EXPECT_EQ(link.custom_properties.value(QStringLiteral("mykey"))
				  .toString(),
			  QStringLiteral("myvalue"));

	ASSERT_EQ(data.group_output_links.size(), 1);
	EXPECT_EQ(data.group_output_links.value(group),
			  static_cast<quintptr>(12345));

	// Nothing has been applied to the group yet
	EXPECT_TRUE(group->get_input_passthroughs().isEmpty());
	EXPECT_EQ(group->get_output_passthrough(), nullptr);
}

TEST_F(NodeGroupTest, PostLoadEventRecreatesPassthroughs)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);

	const QString xml = QStringLiteral(
		"<custom>"
		"<inputpassthroughs>"
		"<inputpassthrough>"
		"<node>12345</node>"
		"<input>param_a_in</input>"
		"<element>-1</element>"
		"<id>pt_a</id>"
		"<name>Custom A</name>"
		"<flags>8</flags>"
		"<type>float</type>"
		"<default>2.5</default>"
		"<properties>"
		"<property>"
		"<key>mykey</key>"
		"<value>myvalue</value>"
		"</property>"
		"</properties>"
		"</inputpassthrough>"
		"</inputpassthroughs>"
		"<outputpassthrough>12345</outputpassthrough>"
		"</custom>");

	olive::SerializedData data;
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_TRUE(group->load_custom(&reader, &data));

	// Point the serialized node references at the real inner node
	data.node_ptrs.insert(static_cast<quintptr>(12345), math);
	group->PostLoadEvent(&data);

	// The passthrough input is recreated with all its serialized overrides
	ASSERT_TRUE(group->has_input_with_id(QStringLiteral("pt_a")));
	EXPECT_TRUE(group->contains_input_passthrough(
		olive::NodeInput(math, olive::MathNode::k_param_a_in)));
	EXPECT_EQ(group->get_input_data_type(QStringLiteral("pt_a")),
			  olive::NodeValue::k_float);
	EXPECT_EQ(group->get_input_name(QStringLiteral("pt_a")),
			  QStringLiteral("Custom A"));
	EXPECT_TRUE(group->is_input_hidden(QStringLiteral("pt_a")));
	EXPECT_DOUBLE_EQ(
		group->get_default_value(QStringLiteral("pt_a")).toDouble(), 2.5);
	EXPECT_EQ(group
				  ->get_input_property(QStringLiteral("pt_a"),
									 QStringLiteral("mykey"))
				  .toString(),
			  QStringLiteral("myvalue"));

	EXPECT_EQ(group->get_output_passthrough(), math);
}

TEST_F(NodeGroupTest, SaveLoadRoundTripPreservesPassthroughs)
{
	olive::MathNode *math_a;
	olive::NodeGroup *group_a = add_group_with_inner_math(&math_a);

	const QString id = group_a->add_input_passthrough(
		olive::NodeInput(math_a, olive::MathNode::k_param_a_in),
		QStringLiteral("pt_roundtrip"));
	group_a->set_input_name(id, QStringLiteral("Original Name"));
	group_a->set_output_passthrough(math_a);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	group_a->save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	// Load into a fresh group whose inner node replaces the original one
	olive::MathNode *math_b;
	olive::NodeGroup *group_b = add_group_with_inner_math(&math_b);

	olive::SerializedData data;
	data.node_ptrs.insert(reinterpret_cast<quintptr>(math_a), math_b);

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));
	ASSERT_TRUE(group_b->load_custom(&reader, &data));
	group_b->PostLoadEvent(&data);

	ASSERT_EQ(group_b->get_input_passthroughs().size(), 1);
	EXPECT_EQ(group_b->get_input_passthroughs().first().first, id);
	EXPECT_EQ(group_b->get_input_passthroughs().first().second.node(), math_b);
	EXPECT_EQ(group_b->get_input_passthroughs().first().second.input(),
			  olive::MathNode::k_param_a_in);
	EXPECT_EQ(group_b->get_input_name(id), QStringLiteral("Original Name"));
	EXPECT_EQ(group_b->get_output_passthrough(), math_b);
}

TEST_F(NodeGroupTest, AddInputPassthroughCommandAddsAndRemoves)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	const olive::NodeInput input(math, olive::MathNode::k_param_a_in);

	olive::NodeGroupAddInputPassthrough cmd(group, input);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	ASSERT_EQ(group->get_input_passthroughs().size(), 1);
	EXPECT_TRUE(group->contains_input_passthrough(input));

	cmd.undo_now();
	EXPECT_TRUE(group->get_input_passthroughs().isEmpty());
	EXPECT_FALSE(group->has_input_with_id(olive::MathNode::k_param_a_in));
}

TEST_F(NodeGroupTest, AddInputPassthroughCommandNoOpWhenAlreadyPresent)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	const olive::NodeInput input(math, olive::MathNode::k_param_a_in);
	group->add_input_passthrough(input);
	ASSERT_EQ(group->get_input_passthroughs().size(), 1);

	olive::NodeGroupAddInputPassthrough cmd(group, input);

	// Redo must not add a duplicate when the passthrough already exists
	cmd.redo_now();
	EXPECT_EQ(group->get_input_passthroughs().size(), 1);

	// And undo must not remove the pre-existing passthrough
	cmd.undo_now();
	EXPECT_EQ(group->get_input_passthroughs().size(), 1);
	EXPECT_TRUE(group->contains_input_passthrough(input));
}

TEST_F(NodeGroupTest, SetOutputPassthroughCommandRestoresPreviousOutput)
{
	olive::MathNode *math;
	olive::NodeGroup *group = add_group_with_inner_math(&math);
	auto *other = add_node<olive::MathNode>();
	group->set_node_position_in_context(other, olive::Node::Position());

	olive::NodeGroupSetOutputPassthrough cmd(group, math);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(group->get_output_passthrough(), math);

	// Replacing the output passthrough restores the previous one on undo
	olive::NodeGroupSetOutputPassthrough replace_cmd(group, other);
	replace_cmd.redo_now();
	EXPECT_EQ(group->get_output_passthrough(), other);
	replace_cmd.undo_now();
	EXPECT_EQ(group->get_output_passthrough(), math);

	cmd.undo_now();
	EXPECT_EQ(group->get_output_passthrough(), nullptr);
}
