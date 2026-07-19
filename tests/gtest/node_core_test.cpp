#include <gtest/gtest.h>

#include <climits>
#include <memory>

#include <QPointF>
#include <QString>
#include <QVector>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/generator/text/textv3.h"
#include "node/keyframe.h"
#include "node/math/math/math.h"
#include "node/node.h"
#include "node/project.h"
#include "node/project/folder/folder.h"

namespace
{

// Node that records every InvalidateCache() call so the invalidation flow
// through the graph can be asserted without any rendering.
class RecordingNode : public olive::Node {
public:
	RecordingNode()
	{
		add_input(k_test_input, olive::NodeValue::k_float, 0.0);
	}

	NODE_DEFAULT_FUNCTIONS(RecordingNode)

	virtual QString name() const override
	{
		return QStringLiteral("Recording");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.recording");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_math };
	}

	virtual void invalidate_cache(
		const olive::TimeRange &range, const QString &from, int element,
		olive::Node::InvalidateCacheOptions options) override
	{
		invalidations.append({ range, from, element });
		olive::Node::invalidate_cache(range, from, element, options);
	}

	struct Invalidation {
		olive::TimeRange range;
		QString from;
		int element;
	};

	QVector<Invalidation> invalidations;

	static const QString k_test_input;
};

const QString RecordingNode::k_test_input = QStringLiteral("test_in");

} // namespace

class NodeCoreTest : public ::testing::Test {
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

TEST_F(NodeCoreTest, InputArrayResizeEmitsSignals)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	const int base = node->input_array_size(olive::TextGeneratorV3::k_args_input);

	struct ResizeEvent {
		QString input;
		int old_size;
		int new_size;
	};
	QVector<ResizeEvent> resizes;
	QObject::connect(node, &olive::Node::input_array_size_changed,
					 [&resizes](const QString &input, int old_size,
								int new_size) {
						 resizes.append({ input, old_size, new_size });
					 });
	int value_changed = 0;
	QObject::connect(node, &olive::Node::value_changed,
					 [&value_changed](const olive::NodeInput &,
									  const olive::TimeRange &) {
						 ++value_changed;
					 });

	node->input_array_append(olive::TextGeneratorV3::k_args_input);
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 1);
	ASSERT_EQ(resizes.size(), 1);
	EXPECT_EQ(resizes.first().input, olive::TextGeneratorV3::k_args_input);
	EXPECT_EQ(resizes.first().old_size, base);
	EXPECT_EQ(resizes.first().new_size, base + 1);
	EXPECT_EQ(value_changed, 1);

	// Resizing to the current size is a no-op
	node->input_array_resize(olive::TextGeneratorV3::k_args_input, base + 1);
	EXPECT_EQ(resizes.size(), 1);
	EXPECT_EQ(value_changed, 1);

	node->input_array_resize(olive::TextGeneratorV3::k_args_input, base + 3);
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 3);
	ASSERT_EQ(resizes.size(), 2);
	EXPECT_EQ(resizes.at(1).old_size, base + 1);
	EXPECT_EQ(resizes.at(1).new_size, base + 3);

	node->input_array_remove_last(olive::TextGeneratorV3::k_args_input);
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 2);

	node->input_array_prepend(olive::TextGeneratorV3::k_args_input);
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input),
			  base + 3);
}

TEST_F(NodeCoreTest, InputArrayInsertShiftsConnectionsAndValues)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	auto *output = add_node<olive::MathNode>();

	node->input_array_resize(olive::TextGeneratorV3::k_args_input, 2);
	node->set_standard_value(
		olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 0),
		QStringLiteral("zero"));
	node->set_standard_value(
		olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 1),
		QStringLiteral("one"));
	olive::Node::connect_edge(
		output, olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 0));

	node->input_array_insert(olive::TextGeneratorV3::k_args_input, 0);

	ASSERT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 3);

	// The connection moved down along with its element
	EXPECT_EQ(node->get_connected_output(olive::TextGeneratorV3::k_args_input, 0),
			  nullptr);
	EXPECT_EQ(node->get_connected_output(olive::TextGeneratorV3::k_args_input, 1),
			  output);
	ASSERT_EQ(output->output_connections().size(), 1);
	EXPECT_EQ(output->output_connections().front().second,
			  olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 1));

	// Values moved down too; the inserted element holds the default value
	EXPECT_TRUE(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
											0)
					.at(0)
					.toString()
					.isEmpty());
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
										  1)
				  .at(0)
				  .toString(),
			  QStringLiteral("zero"));
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
										  2)
				  .at(0)
				  .toString(),
			  QStringLiteral("one"));
}

TEST_F(NodeCoreTest, InputArrayRemoveShiftsConnectionsAndValues)
{
	auto *node = add_node<olive::TextGeneratorV3>();
	auto *output = add_node<olive::MathNode>();

	node->input_array_resize(olive::TextGeneratorV3::k_args_input, 3);
	node->set_standard_value(
		olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 0),
		QStringLiteral("zero"));
	node->set_standard_value(
		olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 1),
		QStringLiteral("one"));
	node->set_standard_value(
		olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 2),
		QStringLiteral("two"));
	olive::Node::connect_edge(
		output, olive::NodeInput(node, olive::TextGeneratorV3::k_args_input, 1));

	node->input_array_remove(olive::TextGeneratorV3::k_args_input, 0);

	ASSERT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 2);

	// The connection moved up along with its element
	EXPECT_EQ(node->get_connected_output(olive::TextGeneratorV3::k_args_input, 0),
			  output);
	EXPECT_EQ(node->get_connected_output(olive::TextGeneratorV3::k_args_input, 1),
			  nullptr);

	// Values moved up too
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
										  0)
				  .at(0)
				  .toString(),
			  QStringLiteral("one"));
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
										  1)
				  .at(0)
				  .toString(),
			  QStringLiteral("two"));

	// Removing the element an edge points at drops the edge entirely
	node->input_array_remove(olive::TextGeneratorV3::k_args_input, 0);
	EXPECT_EQ(node->input_array_size(olive::TextGeneratorV3::k_args_input), 1);
	EXPECT_EQ(node->get_connected_output(olive::TextGeneratorV3::k_args_input, 0),
			  nullptr);
	EXPECT_TRUE(output->output_connections().empty());
	EXPECT_EQ(node->get_split_standard_value(olive::TextGeneratorV3::k_args_input,
										  0)
				  .at(0)
				  .toString(),
			  QStringLiteral("two"));
}

TEST_F(NodeCoreTest, InputFlagsReflectDeclaration)
{
	auto *math = add_node<olive::MathNode>();
	EXPECT_TRUE(math->is_input_connectable(olive::MathNode::k_param_a_in));
	EXPECT_TRUE(math->is_input_keyframable(olive::MathNode::k_param_a_in));
	EXPECT_FALSE(math->is_input_hidden(olive::MathNode::k_param_a_in));
	EXPECT_FALSE(math->input_is_array(olive::MathNode::k_param_a_in));

	// kMethodIn is declared not-connectable and not-keyframable
	EXPECT_FALSE(math->is_input_connectable(olive::MathNode::k_method_in));
	EXPECT_FALSE(math->is_input_keyframable(olive::MathNode::k_method_in));

	auto *text = add_node<olive::TextGeneratorV3>();
	EXPECT_TRUE(
		text->is_input_hidden(olive::TextGeneratorV3::k_vertical_alignment_input));
	EXPECT_FALSE(text->is_input_connectable(
		olive::TextGeneratorV3::k_vertical_alignment_input));
	EXPECT_TRUE(text->input_is_array(olive::TextGeneratorV3::k_args_input));
	EXPECT_FALSE(text->input_is_array(olive::TextGeneratorV3::k_text_input));
}

TEST_F(NodeCoreTest, SetInputFlagTogglesAndEmits)
{
	auto *node = add_node<olive::MathNode>();

	int emissions = 0;
	QString last_input;
	uint64_t last_flags = 0;
	QObject::connect(node, &olive::Node::input_flags_changed,
					 [&emissions, &last_input,
					  &last_flags](const QString &input,
								   const olive::InputFlags &flags) {
						 ++emissions;
						 last_input = input;
						 last_flags = flags.value();
					 });

	node->set_input_flag(olive::MathNode::k_param_a_in, olive::k_input_flag_hidden);
	EXPECT_TRUE(node->is_input_hidden(olive::MathNode::k_param_a_in));
	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(last_input, olive::MathNode::k_param_a_in);
	EXPECT_TRUE(last_flags & olive::k_input_flag_hidden);

	// Setting another flag preserves the flags already set
	node->set_input_flag(olive::MathNode::k_param_a_in,
					   olive::k_input_flag_not_connectable);
	EXPECT_FALSE(node->is_input_connectable(olive::MathNode::k_param_a_in));
	EXPECT_TRUE(node->is_input_hidden(olive::MathNode::k_param_a_in));
	EXPECT_EQ(emissions, 2);

	node->set_input_flag(olive::MathNode::k_param_a_in, olive::k_input_flag_hidden,
					   false);
	EXPECT_FALSE(node->is_input_hidden(olive::MathNode::k_param_a_in));
	EXPECT_EQ(emissions, 3);
}

TEST_F(NodeCoreTest, SetKeyframingOnNonKeyframableInputIsIgnored)
{
	auto *node = add_node<olive::MathNode>();
	ASSERT_FALSE(node->is_input_keyframable(olive::MathNode::k_method_in));

	node->set_input_is_keyframing(olive::MathNode::k_method_in, true);
	EXPECT_FALSE(node->is_input_keyframing(olive::MathNode::k_method_in));
}

TEST_F(NodeCoreTest, UnknownInputAccessorsFallBackSafely)
{
	olive::MathNode node;
	const QString bogus = QStringLiteral("bogus_in");

	// All of these go through GetInternalInputData(), which must handle the
	// input not existing without crashing
	EXPECT_EQ(node.get_input_flags(bogus).value(),
			  static_cast<uint64_t>(olive::k_input_flag_normal));
	EXPECT_EQ(node.get_input_data_type(bogus), olive::NodeValue::k_none);
	EXPECT_TRUE(node.get_input_name(bogus).isEmpty());
	EXPECT_EQ(node.get_immediate(bogus, -1), nullptr);
	EXPECT_EQ(node.get_connected_output(bogus), nullptr);
	EXPECT_FALSE(node.is_input_keyframing(bogus));
	EXPECT_FALSE(node.has_input_property(bogus, QStringLiteral("x")));
	EXPECT_TRUE(node.get_input_properties(bogus).isEmpty());
	EXPECT_TRUE(node.get_split_standard_value(bogus).isEmpty());
	EXPECT_TRUE(node.get_split_default_value(bogus).isEmpty());
	EXPECT_EQ(node.input_array_size(bogus), 0);
}

TEST_F(NodeCoreTest, InputNameTypePropertiesAndDefaults)
{
	auto *node = add_node<olive::MathNode>();

	// Default values round-trip through the split representation
	EXPECT_DOUBLE_EQ(
		node->get_default_value(olive::MathNode::k_param_a_in).toDouble(), 0.0);
	node->set_default_value(olive::MathNode::k_param_a_in, 1.5);
	EXPECT_DOUBLE_EQ(
		node->get_default_value(olive::MathNode::k_param_a_in).toDouble(), 1.5);
	EXPECT_DOUBLE_EQ(node->get_split_default_value(olive::MathNode::k_param_a_in)
						 .at(0)
						 .toDouble(),
					 1.5);

	// Declared data type, keyframe track count and input properties
	EXPECT_EQ(node->get_input_data_type(olive::MathNode::k_param_a_in),
			  olive::NodeValue::k_float);
	EXPECT_EQ(node->get_number_of_keyframe_tracks(olive::MathNode::k_param_a_in), 1);
	EXPECT_EQ(node->get_input_property(olive::MathNode::k_param_a_in,
									 QStringLiteral("decimalplaces"))
				  .toInt(),
			  8);

	auto *solid = add_node<olive::SolidGenerator>();
	EXPECT_EQ(
		solid->get_number_of_keyframe_tracks(olive::SolidGenerator::k_color_input),
		4);

	int name_emissions = 0;
	int type_emissions = 0;
	int property_emissions = 0;
	QObject::connect(node, &olive::Node::input_name_changed,
					 [&name_emissions](const QString &, const QString &) {
						 ++name_emissions;
					 });
	QObject::connect(node, &olive::Node::input_data_type_changed,
					 [&type_emissions](const QString &, olive::NodeValue::Type) {
						 ++type_emissions;
					 });
	QObject::connect(node, &olive::Node::input_property_changed,
					 [&property_emissions](const QString &, const QString &,
										   const QVariant &) {
						 ++property_emissions;
					 });

	node->set_input_name(olive::MathNode::k_param_a_in, QStringLiteral("Custom"));
	EXPECT_EQ(node->get_input_name(olive::MathNode::k_param_a_in),
			  QStringLiteral("Custom"));
	EXPECT_EQ(name_emissions, 1);

	node->set_input_data_type(olive::MathNode::k_param_a_in, olive::NodeValue::k_int);
	EXPECT_EQ(node->get_input_data_type(olive::MathNode::k_param_a_in),
			  olive::NodeValue::k_int);
	EXPECT_EQ(type_emissions, 1);

	node->set_input_property(olive::MathNode::k_param_a_in, QStringLiteral("mykey"),
						   42);
	EXPECT_TRUE(node->has_input_property(olive::MathNode::k_param_a_in,
									   QStringLiteral("mykey")));
	EXPECT_EQ(node->get_input_property(olive::MathNode::k_param_a_in,
									 QStringLiteral("mykey"))
				  .toInt(),
			  42);
	EXPECT_TRUE(node->get_input_properties(olive::MathNode::k_param_a_in)
					.contains(QStringLiteral("decimalplaces")));
	EXPECT_EQ(property_emissions, 1);
}

TEST_F(NodeCoreTest, ContextPositionLifecycleEmitsSignals)
{
	auto *node = add_node<olive::MathNode>();
	auto *context = add_node<olive::Folder>();

	int added_count = 0;
	int removed_count = 0;
	QVector<QPointF> positions;
	QObject::connect(context, &olive::Node::node_added_to_context,
					 [&added_count](olive::Node *) { ++added_count; });
	QObject::connect(context, &olive::Node::node_removed_from_context,
					 [&removed_count](olive::Node *) { ++removed_count; });
	QObject::connect(context, &olive::Node::node_position_in_context_changed,
					 [&positions](olive::Node *, const QPointF &pos) {
						 positions.append(pos);
					 });

	EXPECT_FALSE(context->context_contains_node(node));
	EXPECT_TRUE(context->get_context_positions().isEmpty());

	// First insertion reports that the node was added
	EXPECT_TRUE(context->set_node_position_in_context(
		node, olive::Node::Position(QPointF(3.0, 4.0), true)));
	EXPECT_TRUE(context->context_contains_node(node));
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(3.0, 4.0));
	EXPECT_TRUE(context->is_node_expanded_in_context(node));
	EXPECT_EQ(added_count, 1);
	ASSERT_EQ(positions.size(), 1);
	EXPECT_EQ(positions.first(), QPointF(3.0, 4.0));

	// Updating an existing entry reports no addition and keeps the expanded
	// state
	EXPECT_FALSE(context->set_node_position_in_context(node, QPointF(5.0, 6.0)));
	EXPECT_EQ(context->get_node_position_in_context(node), QPointF(5.0, 6.0));
	EXPECT_TRUE(context->is_node_expanded_in_context(node));
	EXPECT_EQ(added_count, 1);
	ASSERT_EQ(positions.size(), 2);
	EXPECT_EQ(positions.at(1), QPointF(5.0, 6.0));

	context->set_node_expanded_in_context(node, false);
	EXPECT_FALSE(context->is_node_expanded_in_context(node));

	EXPECT_TRUE(context->remove_node_from_context(node));
	EXPECT_FALSE(context->context_contains_node(node));
	EXPECT_EQ(removed_count, 1);

	// Removing a node that is not in the context is a no-op
	EXPECT_FALSE(context->remove_node_from_context(node));
	EXPECT_EQ(removed_count, 1);
}

TEST_F(NodeCoreTest, ContextPositionSaveLoadAndOperators)
{
	const olive::Node::Position pos(QPointF(1.5, -2.5), true);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("position"));
	pos.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("position"));

	olive::Node::Position loaded;
	ASSERT_TRUE(loaded.load(&reader));
	EXPECT_EQ(loaded.position, QPointF(1.5, -2.5));
	EXPECT_TRUE(loaded.expanded);

	// A position missing a coordinate fails to load
	QXmlStreamReader bad_reader(
		QStringLiteral("<position><x>1</x></position>"));
	ASSERT_TRUE(bad_reader.readNextStartElement());
	olive::Node::Position incomplete;
	EXPECT_FALSE(incomplete.load(&bad_reader));

	// Position arithmetic only affects the point, not the expanded state
	const olive::Node::Position a(QPointF(1.0, 2.0));
	const olive::Node::Position b(QPointF(3.0, 4.0));
	EXPECT_EQ((a + b).position, QPointF(4.0, 6.0));
	EXPECT_EQ((b - a).position, QPointF(2.0, 2.0));

	olive::Node::Position c(QPointF(1.0, 1.0));
	c += a;
	EXPECT_EQ(c.position, QPointF(2.0, 3.0));
	c -= b;
	EXPECT_EQ(c.position, QPointF(-1.0, -1.0));
}

TEST_F(NodeCoreTest, LinkAndUnlinkLifecycle)
{
	auto *a = add_node<olive::MathNode>();
	auto *b = add_node<olive::SolidGenerator>();

	int a_changes = 0;
	int b_changes = 0;
	QObject::connect(a, &olive::Node::links_changed,
					 [&a_changes]() { ++a_changes; });
	QObject::connect(b, &olive::Node::links_changed,
					 [&b_changes]() { ++b_changes; });

	EXPECT_FALSE(olive::Node::are_linked(a, b));
	EXPECT_FALSE(a->has_links());
	EXPECT_FALSE(b->has_links());

	// Invalid pairs are rejected
	EXPECT_FALSE(olive::Node::link(a, a));
	EXPECT_FALSE(olive::Node::link(a, nullptr));
	EXPECT_FALSE(olive::Node::link(nullptr, b));

	EXPECT_TRUE(olive::Node::link(a, b));
	EXPECT_TRUE(olive::Node::are_linked(a, b));
	EXPECT_TRUE(olive::Node::are_linked(b, a));
	EXPECT_TRUE(a->has_links());
	ASSERT_EQ(a->links().size(), 1);
	EXPECT_EQ(a->links().first(), b);
	EXPECT_EQ(b->links().first(), a);
	EXPECT_EQ(a_changes, 1);
	EXPECT_EQ(b_changes, 1);

	// Linking an already-linked pair does nothing
	EXPECT_FALSE(olive::Node::link(a, b));
	EXPECT_EQ(a_changes, 1);
	EXPECT_EQ(b_changes, 1);

	EXPECT_TRUE(olive::Node::unlink(a, b));
	EXPECT_FALSE(olive::Node::are_linked(a, b));
	EXPECT_FALSE(a->has_links());
	EXPECT_EQ(a_changes, 2);
	EXPECT_EQ(b_changes, 2);

	// Unlinking an unlinked pair does nothing
	EXPECT_FALSE(olive::Node::unlink(a, b));
	EXPECT_EQ(a_changes, 2);
	EXPECT_EQ(b_changes, 2);
}

TEST_F(NodeCoreTest, OverrideColorEmitsOnlyOnChange)
{
	auto *node = add_node<olive::MathNode>();
	ASSERT_EQ(node->get_override_color(), -1);

	int emissions = 0;
	QObject::connect(node, &olive::Node::color_changed,
					 [&emissions]() { ++emissions; });

	node->set_override_color(4);
	EXPECT_EQ(node->get_override_color(), 4);
	EXPECT_EQ(emissions, 1);

	// Setting the same color again is not a change
	node->set_override_color(4);
	EXPECT_EQ(emissions, 1);

	node->set_override_color(-1);
	EXPECT_EQ(node->get_override_color(), -1);
	EXPECT_EQ(emissions, 2);
}

TEST_F(NodeCoreTest, ValueHintAccessorsEmitSignal)
{
	auto *node = add_node<olive::MathNode>();

	// The default hint is empty
	const olive::Node::ValueHint def =
		node->get_value_hint_for_input(olive::MathNode::k_param_a_in);
	EXPECT_TRUE(def.types().isEmpty());
	EXPECT_EQ(def.index(), -1);
	EXPECT_TRUE(def.tag().isEmpty());

	QVector<olive::NodeInput> hinted;
	QObject::connect(node, &olive::Node::input_value_hint_changed,
					 [&hinted](const olive::NodeInput &input) {
						 hinted.append(input);
					 });

	const olive::Node::ValueHint hint({ olive::NodeValue::k_vec2 }, 3,
									  QStringLiteral("tag"));
	node->set_value_hint_for_input(olive::MathNode::k_param_a_in, hint);

	const olive::Node::ValueHint stored =
		node->get_value_hint_for_input(olive::MathNode::k_param_a_in);
	ASSERT_EQ(stored.types().size(), 1);
	EXPECT_EQ(stored.types().first(), olive::NodeValue::k_vec2);
	EXPECT_EQ(stored.index(), 3);
	EXPECT_EQ(stored.tag(), QStringLiteral("tag"));

	ASSERT_EQ(hinted.size(), 1);
	EXPECT_EQ(hinted.first(),
			  olive::NodeInput(node, olive::MathNode::k_param_a_in, -1));
	EXPECT_FALSE(node->get_value_hints().isEmpty());

	// Hints are tracked per element
	node->set_value_hint_for_input(olive::MathNode::k_param_a_in,
							   olive::Node::ValueHint(QStringLiteral("elem")),
							   2);
	EXPECT_EQ(node->get_value_hint_for_input(olive::MathNode::k_param_a_in, 2).tag(),
			  QStringLiteral("elem"));
	EXPECT_EQ(node->get_value_hint_for_input(olive::MathNode::k_param_a_in, 1).index(),
			  -1);
}

TEST_F(NodeCoreTest, ValueHintSaveLoadRoundTrip)
{
	const olive::Node::ValueHint hint(
		{ olive::NodeValue::k_vec2, olive::NodeValue::k_texture }, 7,
		QStringLiteral("mytag"));

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("hint"));
	hint.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("hint"));

	olive::Node::ValueHint loaded;
	ASSERT_TRUE(loaded.load(&reader));
	ASSERT_EQ(loaded.types().size(), 2);
	EXPECT_EQ(loaded.types().at(0), olive::NodeValue::k_vec2);
	EXPECT_EQ(loaded.types().at(1), olive::NodeValue::k_texture);
	EXPECT_EQ(loaded.index(), 7);
	EXPECT_EQ(loaded.tag(), QStringLiteral("mytag"));

	// A default-constructed hint round-trips to an empty hint
	QString empty_xml;
	QXmlStreamWriter empty_writer(&empty_xml);
	empty_writer.writeStartDocument();
	empty_writer.writeStartElement(QStringLiteral("hint"));
	olive::Node::ValueHint().save(&empty_writer);
	empty_writer.writeEndElement();
	empty_writer.writeEndDocument();

	QXmlStreamReader empty_reader(empty_xml);
	ASSERT_TRUE(empty_reader.readNextStartElement());
	olive::Node::ValueHint empty_loaded;
	ASSERT_TRUE(empty_loaded.load(&empty_reader));
	EXPECT_TRUE(empty_loaded.types().isEmpty());
	EXPECT_EQ(empty_loaded.index(), -1);
	EXPECT_TRUE(empty_loaded.tag().isEmpty());
}

TEST_F(NodeCoreTest, EdgeAndValueChangesPropagateInvalidateCache)
{
	auto *src = add_node<olive::MathNode>();
	auto *dst = add_node<RecordingNode>();
	const olive::NodeInput dst_input(dst, RecordingNode::k_test_input);

	const olive::Rational k_min(INT_MIN);
	const olive::Rational k_max(INT_MAX);

	QVector<olive::NodeInput> value_changed_inputs;
	QObject::connect(src, &olive::Node::value_changed,
					 [&value_changed_inputs](const olive::NodeInput &input,
											 const olive::TimeRange &) {
						 value_changed_inputs.append(input);
					 });

	// Connecting an edge invalidates the destination over the full range
	olive::Node::connect_edge(src, dst_input);
	ASSERT_EQ(dst->invalidations.size(), 1);
	EXPECT_EQ(dst->invalidations.first().from, RecordingNode::k_test_input);
	EXPECT_EQ(dst->invalidations.first().element, -1);
	EXPECT_EQ(dst->invalidations.first().range.in(), k_min);
	EXPECT_EQ(dst->invalidations.first().range.out(), k_max);

	// Changing a value upstream emits ValueChanged and invalidates downstream
	src->set_standard_value(olive::MathNode::k_param_a_in, 2.0);
	ASSERT_EQ(value_changed_inputs.size(), 1);
	EXPECT_EQ(value_changed_inputs.first(),
			  olive::NodeInput(src, olive::MathNode::k_param_a_in));
	ASSERT_EQ(dst->invalidations.size(), 2);
	EXPECT_EQ(dst->invalidations.at(1).range.in(), k_min);
	EXPECT_EQ(dst->invalidations.at(1).range.out(), k_max);

	// Disconnecting the edge invalidates the destination again
	olive::Node::disconnect_edge(src, dst_input);
	ASSERT_EQ(dst->invalidations.size(), 3);
	EXPECT_TRUE(dst->input_connections().empty());
	EXPECT_TRUE(src->output_connections().empty());
}

TEST_F(NodeCoreTest, IgnoreInvalidationsFlagSuppressesInvalidation)
{
	auto *src = add_node<olive::MathNode>();
	auto *dst = add_node<RecordingNode>();
	const olive::NodeInput dst_input(dst, RecordingNode::k_test_input);

	// The flag on the destination input suppresses connect/disconnect
	// invalidation
	dst->set_input_flag(RecordingNode::k_test_input,
					  olive::k_input_flag_ignore_invalidations);
	olive::Node::connect_edge(src, dst_input);
	EXPECT_TRUE(dst->invalidations.isEmpty());

	// The flag on the source input suppresses value-change propagation, but
	// the ValueChanged signal is still emitted
	int value_changed_count = 0;
	QObject::connect(src, &olive::Node::value_changed,
					 [&value_changed_count](const olive::NodeInput &,
											const olive::TimeRange &) {
						 ++value_changed_count;
					 });
	src->set_input_flag(olive::MathNode::k_param_a_in,
					  olive::k_input_flag_ignore_invalidations);
	src->set_standard_value(olive::MathNode::k_param_a_in, 5.0);
	EXPECT_EQ(value_changed_count, 1);
	EXPECT_TRUE(dst->invalidations.isEmpty());

	olive::Node::disconnect_edge(src, dst_input);
	EXPECT_TRUE(dst->invalidations.isEmpty());
}

TEST_F(NodeCoreTest, InvalidateAllRelaysThroughGraph)
{
	auto *src = add_node<olive::MathNode>();
	auto *mid = add_node<olive::MathNode>();
	auto *dst = add_node<RecordingNode>();
	olive::Node::connect_edge(
		src, olive::NodeInput(mid, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(
		mid, olive::NodeInput(dst, RecordingNode::k_test_input));
	dst->invalidations.clear();

	const olive::Rational k_min(INT_MIN);
	const olive::Rational k_max(INT_MAX);

	// InvalidateAll propagates the full time range across multiple hops
	src->invalidate_all(olive::MathNode::k_param_a_in);
	ASSERT_EQ(dst->invalidations.size(), 1);
	EXPECT_EQ(dst->invalidations.first().from, RecordingNode::k_test_input);
	EXPECT_EQ(dst->invalidations.first().element, -1);
	EXPECT_EQ(dst->invalidations.first().range.in(), k_min);
	EXPECT_EQ(dst->invalidations.first().range.out(), k_max);

	// A zero-length range touches no caches but is still relayed
	src->invalidate_cache(
		olive::TimeRange(olive::Rational(3), olive::Rational(3)),
		olive::MathNode::k_param_a_in, -1);
	ASSERT_EQ(dst->invalidations.size(), 2);
	EXPECT_EQ(dst->invalidations.at(1).range.in(), olive::Rational(3));
	EXPECT_EQ(dst->invalidations.at(1).range.out(), olive::Rational(3));

	// Disabled caches skip cache invalidation, but propagation continues
	EXPECT_TRUE(src->are_caches_enabled());
	src->set_caches_enabled(false);
	EXPECT_FALSE(src->are_caches_enabled());
	src->invalidate_all(olive::MathNode::k_param_a_in);
	ASSERT_EQ(dst->invalidations.size(), 3);
	EXPECT_EQ(dst->invalidations.at(2).range.in(), k_min);
}

TEST_F(NodeCoreTest, KeyframeAddAndRemovalEmitSignals)
{
	auto *node = add_node<olive::MathNode>();

	const olive::Rational k_min(INT_MIN);
	const olive::Rational k_max(INT_MAX);

	int enable_changed = 0;
	bool last_enabled = false;
	QObject::connect(node, &olive::Node::keyframe_enable_changed,
					 [&enable_changed, &last_enabled](const olive::NodeInput &,
													 bool enabled) {
						 ++enable_changed;
						 last_enabled = enabled;
					 });
	int added = 0;
	int removed = 0;
	QObject::connect(node, &olive::Node::keyframe_added,
					 [&added](olive::NodeKeyframe *) { ++added; });
	QObject::connect(node, &olive::Node::keyframe_removed,
					 [&removed](olive::NodeKeyframe *) { ++removed; });
	QVector<olive::TimeRange> changed_ranges;
	QObject::connect(node, &olive::Node::value_changed,
					 [&changed_ranges](const olive::NodeInput &,
									   const olive::TimeRange &range) {
						 changed_ranges.append(range);
					 });

	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);
	EXPECT_EQ(enable_changed, 1);
	EXPECT_TRUE(last_enabled);

	// The first keyframe on a track invalidates the whole range
	auto *first = new olive::NodeKeyframe(
		olive::Rational(5), 1.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	first->setParent(node);
	EXPECT_EQ(added, 1);
	ASSERT_EQ(changed_ranges.size(), 1);
	EXPECT_EQ(changed_ranges.first().in(), k_min);
	EXPECT_EQ(changed_ranges.first().out(), k_max);

	// A later keyframe only invalidates from the previous keyframe onward
	auto *second = new olive::NodeKeyframe(
		olive::Rational(10), 2.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	second->setParent(node);
	EXPECT_EQ(added, 2);
	ASSERT_EQ(changed_ranges.size(), 2);
	EXPECT_EQ(changed_ranges.at(1).in(), olive::Rational(5));
	EXPECT_EQ(changed_ranges.at(1).out(), k_max);

	// Removing the later keyframe invalidates from the remaining one onward
	second->setParent(nullptr);
	EXPECT_EQ(removed, 1);
	ASSERT_EQ(changed_ranges.size(), 3);
	EXPECT_EQ(changed_ranges.at(2).in(), olive::Rational(5));
	EXPECT_EQ(changed_ranges.at(2).out(), k_max);
	delete second;

	// Removing the last keyframe invalidates everything again
	first->setParent(nullptr);
	EXPECT_EQ(removed, 2);
	ASSERT_EQ(changed_ranges.size(), 4);
	EXPECT_EQ(changed_ranges.at(3).in(), k_min);
	EXPECT_EQ(changed_ranges.at(3).out(), k_max);
	delete first;

	EXPECT_TRUE(node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1)
					.at(0)
					.isEmpty());
}

TEST_F(NodeCoreTest, KeyframeTimeChangeResortsTrackAndEmits)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	auto *first = new olive::NodeKeyframe(
		olive::Rational(0), 0.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	first->setParent(node);
	auto *second = new olive::NodeKeyframe(
		olive::Rational(10), 10.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	second->setParent(node);

	const QVector<olive::NodeKeyframeTrack> &tracks =
		node->get_keyframe_tracks(olive::MathNode::k_param_a_in, -1);
	ASSERT_EQ(tracks.at(0).size(), 2);
	EXPECT_EQ(tracks.at(0).first(), first);
	EXPECT_EQ(node->get_earliest_keyframe(olive::MathNode::k_param_a_in), first);
	EXPECT_EQ(node->get_latest_keyframe(olive::MathNode::k_param_a_in), second);

	int time_changed = 0;
	olive::NodeKeyframe *last_changed = nullptr;
	QObject::connect(node, &olive::Node::keyframe_time_changed,
					 [&time_changed,
					  &last_changed](olive::NodeKeyframe *key) {
						 ++time_changed;
						 last_changed = key;
					 });

	// Moving the first keyframe past the second resorts the track
	first->set_time(olive::Rational(20));
	EXPECT_EQ(time_changed, 1);
	EXPECT_EQ(last_changed, first);
	ASSERT_EQ(tracks.at(0).size(), 2);
	EXPECT_EQ(tracks.at(0).first(), second);
	EXPECT_EQ(tracks.at(0).last(), first);

	EXPECT_EQ(node->get_earliest_keyframe(olive::MathNode::k_param_a_in), second);
	EXPECT_EQ(node->get_latest_keyframe(olive::MathNode::k_param_a_in), first);
	EXPECT_EQ(node->get_closest_keyframe_before_time(olive::MathNode::k_param_a_in,
												 olive::Rational(15)),
			  second);
	EXPECT_EQ(node->get_closest_keyframe_after_time(olive::MathNode::k_param_a_in,
												olive::Rational(15)),
			  first);
	EXPECT_TRUE(node->has_keyframe_at_time(olive::MathNode::k_param_a_in,
										olive::Rational(20)));
	EXPECT_EQ(node->get_keyframe_at_time_on_track(olive::MathNode::k_param_a_in,
											 olive::Rational(10), 0),
			  second);
	EXPECT_EQ(node->get_keyframes_at_time(olive::MathNode::k_param_a_in,
									   olive::Rational(20))
				  .size(),
			  1);
}

TEST_F(NodeCoreTest, GetValueAtTimeUsesStandardValueWhenStatic)
{
	auto *node = add_node<olive::MathNode>();
	node->set_standard_value(olive::MathNode::k_param_a_in, 2.5);

	// A static input returns its standard value at any time
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(-100))
						 .toDouble(),
					 2.5);
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(0))
			.toDouble(),
		2.5);
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(100))
						 .toDouble(),
					 2.5);

	// Same result through the NodeInput convenience overload
	EXPECT_DOUBLE_EQ(node->get_value_at_time(
						 olive::NodeInput(node, olive::MathNode::k_param_a_in),
						 olive::Rational(3))
						 .toDouble(),
					 2.5);
}

TEST_F(NodeCoreTest, GetValueAtTimeInterpolatesLinearKeyframes)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	auto *first = new olive::NodeKeyframe(
		olive::Rational(0), 0.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	first->setParent(node);
	auto *second = new olive::NodeKeyframe(
		olive::Rational(10), 10.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	second->setParent(node);

	// Outside the keyed range the nearest keyframe value holds
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(-5))
						 .toDouble(),
					 0.0);
	EXPECT_DOUBLE_EQ(node->get_value_at_time(olive::MathNode::k_param_a_in,
										  olive::Rational(15))
						 .toDouble(),
					 10.0);

	// Exactly on a keyframe the value is exact
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(0))
			.toDouble(),
		0.0);
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(10))
			.toDouble(),
		10.0);

	// Between two linear keys the value interpolates linearly
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(5))
			.toDouble(),
		5.0);

	const olive::SplitValue split =
		node->get_split_value_at_time(olive::MathNode::k_param_a_in,
								  olive::Rational(5));
	ASSERT_EQ(split.size(), 1);
	EXPECT_DOUBLE_EQ(split.at(0).toDouble(), 5.0);
}

TEST_F(NodeCoreTest, GetValueAtTimeRespectsHoldKeyframes)
{
	auto *node = add_node<olive::MathNode>();
	node->set_input_is_keyframing(olive::MathNode::k_param_a_in, true);

	auto *hold = new olive::NodeKeyframe(
		olive::Rational(0), 1.0, olive::NodeKeyframe::k_hold, 0, -1,
		olive::MathNode::k_param_a_in);
	hold->setParent(node);
	auto *linear = new olive::NodeKeyframe(
		olive::Rational(10), 3.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_a_in);
	linear->setParent(node);

	// A hold keyframe keeps its value until the next keyframe's time
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(0))
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(5))
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(9))
			.toDouble(),
		1.0);
	EXPECT_DOUBLE_EQ(
		node->get_value_at_time(olive::MathNode::k_param_a_in, olive::Rational(10))
			.toDouble(),
		3.0);
}

TEST_F(NodeCoreTest, CopyInputsCopiesValuesKeyframesLabelAndColor)
{
	auto *src = add_node<olive::MathNode>();
	auto *dst = add_node<olive::MathNode>();

	src->set_standard_value(olive::MathNode::k_param_a_in, 3.5);
	src->set_operation(olive::MathNode::k_op_multiply);
	src->set_label(QStringLiteral("source label"));
	src->set_override_color(2);
	src->set_value_hint_for_input(
		olive::MathNode::k_param_a_in,
		olive::Node::ValueHint({ olive::NodeValue::k_vec2 }, 1,
							   QStringLiteral("hint")));

	src->set_input_is_keyframing(olive::MathNode::k_param_b_in, true);
	auto *key = new olive::NodeKeyframe(
		olive::Rational(4), 8.0, olive::NodeKeyframe::k_linear, 0, -1,
		olive::MathNode::k_param_b_in);
	key->setParent(src);

	olive::Node::copy_inputs(src, dst, false);

	EXPECT_DOUBLE_EQ(
		dst->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 3.5);
	EXPECT_EQ(dst->get_operation(), olive::MathNode::k_op_multiply);
	EXPECT_EQ(dst->get_label(), QStringLiteral("source label"));
	EXPECT_EQ(dst->get_override_color(), 2);
	EXPECT_EQ(dst->get_value_hint_for_input(olive::MathNode::k_param_a_in).tag(),
			  QStringLiteral("hint"));
	EXPECT_TRUE(dst->is_input_keyframing(olive::MathNode::k_param_b_in));

	const QVector<olive::NodeKeyframeTrack> &tracks =
		dst->get_keyframe_tracks(olive::MathNode::k_param_b_in, -1);
	ASSERT_EQ(tracks.at(0).size(), 1);
	EXPECT_EQ(tracks.at(0).first()->time(), olive::Rational(4));
	EXPECT_DOUBLE_EQ(tracks.at(0).first()->value().toDouble(), 8.0);
	// The copied keyframe belongs to the destination, not the source
	EXPECT_EQ(tracks.at(0).first()->parent(), dst);
	EXPECT_NE(tracks.at(0).first(), key);

	// Copied values are independent of the source
	src->set_standard_value(olive::MathNode::k_param_a_in, 9.0);
	EXPECT_DOUBLE_EQ(
		dst->get_standard_value(olive::MathNode::k_param_a_in).toDouble(), 3.5);
}

TEST_F(NodeCoreTest, CopyInputsCopiesConnectionsWhenRequested)
{
	auto *output = add_node<olive::SolidGenerator>();
	auto *src = add_node<olive::MathNode>();
	auto *dst = add_node<olive::MathNode>();
	olive::Node::connect_edge(
		output, olive::NodeInput(src, olive::MathNode::k_param_a_in));

	// Without connections requested, the destination stays unconnected
	olive::Node::copy_inputs(src, dst, false);
	EXPECT_EQ(dst->get_connected_output(olive::MathNode::k_param_a_in), nullptr);

	// With connections requested, the destination connects to the same output
	olive::Node::copy_inputs(src, dst, true);
	EXPECT_EQ(dst->get_connected_output(olive::MathNode::k_param_a_in), output);
}

TEST_F(NodeCoreTest, CopyInputsCopiesArrayElements)
{
	auto *src = add_node<olive::TextGeneratorV3>();
	auto *dst = add_node<olive::TextGeneratorV3>();

	src->input_array_resize(olive::TextGeneratorV3::k_args_input, 2);
	src->set_standard_value(
		olive::NodeInput(src, olive::TextGeneratorV3::k_args_input, 0),
		QStringLiteral("first"));
	src->set_standard_value(
		olive::NodeInput(src, olive::TextGeneratorV3::k_args_input, 1),
		QStringLiteral("second"));
	src->set_input_is_keyframing(olive::TextGeneratorV3::k_args_input, true, 1);
	auto *key = new olive::NodeKeyframe(
		olive::Rational(2), QStringLiteral("keyed"), olive::NodeKeyframe::k_linear,
		0, 1, olive::TextGeneratorV3::k_args_input);
	key->setParent(src);

	olive::Node::copy_inputs(src, dst, false);

	EXPECT_EQ(dst->input_array_size(olive::TextGeneratorV3::k_args_input), 2);
	EXPECT_EQ(dst->get_split_standard_value(olive::TextGeneratorV3::k_args_input, 0)
				  .at(0)
				  .toString(),
			  QStringLiteral("first"));
	EXPECT_EQ(dst->get_split_standard_value(olive::TextGeneratorV3::k_args_input, 1)
				  .at(0)
				  .toString(),
			  QStringLiteral("second"));
	EXPECT_TRUE(dst->is_input_keyframing(olive::TextGeneratorV3::k_args_input, 1));
	EXPECT_FALSE(dst->is_input_keyframing(olive::TextGeneratorV3::k_args_input, 0));
	ASSERT_EQ(dst->get_keyframe_tracks(olive::TextGeneratorV3::k_args_input, 1)
				  .at(0)
				  .size(),
			  1);
	EXPECT_EQ(dst->get_keyframe_tracks(olive::TextGeneratorV3::k_args_input, 1)
				  .at(0)
				  .first()
				  ->value()
				  .toString(),
			  QStringLiteral("keyed"));
}

TEST_F(NodeCoreTest, CopyDependencyGraphClonesAndReconnects)
{
	auto *solid = add_node<olive::SolidGenerator>();
	auto *math = add_node<olive::MathNode>();
	math->set_standard_value(olive::MathNode::k_param_a_in, 6.0);
	olive::Node::connect_edge(
		solid, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	math->set_value_hint_for_input(olive::MathNode::k_param_a_in,
							   olive::Node::ValueHint(QStringLiteral("tagged")));

	const QVector<olive::Node *> copies =
		olive::Node::copy_dependency_graph({ solid, math }, nullptr);
	ASSERT_EQ(copies.size(), 2);

	olive::Node *solid_copy = copies.at(0);
	olive::Node *math_copy = copies.at(1);
	EXPECT_NE(solid_copy, solid);
	EXPECT_NE(math_copy, math);
	EXPECT_EQ(solid_copy->id(), solid->id());
	EXPECT_EQ(math_copy->id(), math->id());
	EXPECT_EQ(solid_copy->project(), project_.get());
	EXPECT_EQ(math_copy->project(), project_.get());

	// The clone is wired to the cloned upstream node, not the original
	EXPECT_EQ(math_copy->get_connected_output(olive::MathNode::k_param_a_in),
			  solid_copy);
	EXPECT_EQ(math->get_connected_output(olive::MathNode::k_param_a_in), solid);

	// ...and carries the source's values and hints
	EXPECT_DOUBLE_EQ(
		math_copy->get_standard_value(olive::MathNode::k_param_a_in).toDouble(),
		6.0);
	EXPECT_EQ(math_copy->get_value_hint_for_input(olive::MathNode::k_param_a_in).tag(),
			  QStringLiteral("tagged"));
}
