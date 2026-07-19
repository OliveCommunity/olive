#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "common/qtutils.h"
#include "node/globals.h"
#include "node/math/math/math.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/util/color.h"
#include "olive/core/util/rational.h"
#include "render/job/samplejob.h"

namespace
{

// Minimal node that emits a single configurable value. MathNode's inputs are
// declared as kFloat, so standard values always arrive as kFloat-typed
// NodeValues; feeding vectors, matrices, colors, rationals or sample buffers
// requires a connected node that pushes the real type.
class ConstantValueNode : public olive::Node {
public:
	ConstantValueNode() = default;

	NODE_DEFAULT_FUNCTIONS(ConstantValueNode)

	virtual QString name() const override
	{
		return QStringLiteral("Test Constant");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.constant");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_math };
	}

	void set_output(const olive::NodeValue &value)
	{
		output_ = value;
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(output_);
	}

private:
	olive::NodeValue output_;
};

// Traverser that resolves SampleJobs on the CPU (no audio hardware) so the
// non-static number path of MathNode can be verified end to end.
class SampleResolvingTraverser : public olive::NodeTraverser {
public:
	void resolve(olive::NodeValue &value)
	{
		resolve_jobs(value);
	}

protected:
	virtual olive::core::SampleBuffer
	create_sample_buffer(const olive::core::AudioParams &params,
					   int sample_count) override
	{
		return olive::core::SampleBuffer(params, size_t(sample_count));
	}

	virtual void process_samples(olive::core::SampleBuffer &destination,
								const olive::Node *node,
								const olive::TimeRange &range,
								const olive::SampleJob &job) override
	{
		Q_UNUSED(range)

		for (size_t i = 0; i < destination.sample_count(); i++) {
			node->process_samples(job.get_values(), job.samples(), destination,
								 int(i));
		}
	}
};

olive::MathNode *create_math_node(olive::Project *project)
{
	auto *math = new olive::MathNode();
	math->setParent(project);
	return math;
}

ConstantValueNode *create_constant(olive::Project *project,
								  const olive::NodeValue &value)
{
	auto *node = new ConstantValueNode();
	node->setParent(project);
	node->set_output(value);
	return node;
}

olive::NodeValueTable generate_math_table(olive::MathNode *math)
{
	olive::NodeTraverser traverser;
	return traverser.generate_table(
		math, olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(1, 30)));
}

olive::core::AudioParams test_audio_params()
{
	return olive::core::AudioParams(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::f32_p);
}

// Creates a stereo buffer with the given per-channel samples. Both channels
// must have the same number of samples.
olive::core::SampleBuffer make_sample_buffer(const std::vector<float> &channel0,
										   const std::vector<float> &channel1)
{
	olive::core::SampleBuffer buffer(test_audio_params(), channel0.size());
	for (size_t i = 0; i < channel0.size(); i++) {
		buffer.data(0)[i] = channel0[i];
	}
	for (size_t i = 0; i < channel1.size(); i++) {
		buffer.data(1)[i] = channel1[i];
	}
	return buffer;
}

olive::NodeValue sample_value(const olive::core::SampleBuffer &buffer)
{
	return olive::NodeValue(olive::NodeValue::k_samples,
							QVariant::fromValue(buffer));
}

} // namespace

TEST(MathNode, MetadataIsCorrect)
{
	olive::MathNode unparented;
	EXPECT_EQ(unparented.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.math"));
	EXPECT_FALSE(unparented.description().isEmpty());
	EXPECT_TRUE(
		unparented.category().contains(olive::Node::k_category_math));
	EXPECT_EQ(unparented.get_operation(), olive::MathNode::k_op_add);

	// Without a parent the node is just called "Math"
	EXPECT_EQ(unparented.name(), QStringLiteral("Math"));

	// Parented nodes are named after their operation
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	EXPECT_EQ(math->name(), QStringLiteral("Add"));

	math->set_operation(olive::MathNode::k_op_subtract);
	EXPECT_EQ(math->get_operation(), olive::MathNode::k_op_subtract);
	EXPECT_EQ(math->name(), QStringLiteral("Subtract"));
}

TEST(MathNode, OperationNames)
{
	EXPECT_EQ(olive::MathNodeBase::get_operation_name(olive::MathNode::k_op_add),
			  QStringLiteral("Add"));
	EXPECT_EQ(
		olive::MathNodeBase::get_operation_name(olive::MathNode::k_op_subtract),
		QStringLiteral("Subtract"));
	EXPECT_EQ(
		olive::MathNodeBase::get_operation_name(olive::MathNode::k_op_multiply),
		QStringLiteral("Multiply"));
	EXPECT_EQ(olive::MathNodeBase::get_operation_name(olive::MathNode::k_op_divide),
			  QStringLiteral("Divide"));
	EXPECT_EQ(olive::MathNodeBase::get_operation_name(olive::MathNode::k_op_power),
			  QStringLiteral("Power"));

	// Out-of-range operations produce an empty name
	EXPECT_TRUE(olive::MathNodeBase::get_operation_name(
					static_cast<olive::MathNode::Operation>(-1))
					.isEmpty());
}

TEST(MathNode, RetranslateSetsInputNamesAndComboStrings)
{
	olive::MathNode math;
	math.retranslate();

	EXPECT_EQ(math.get_input_name(olive::MathNode::k_method_in),
			  QStringLiteral("Method"));
	EXPECT_EQ(math.get_input_name(olive::MathNode::k_param_a_in),
			  QStringLiteral("Value"));
	EXPECT_EQ(math.get_input_name(olive::MathNode::k_param_b_in),
			  QStringLiteral("Value"));

	const QStringList operations =
		math.get_input_property(olive::MathNode::k_method_in,
							  QStringLiteral("combo_str"))
			.toStringList();
	ASSERT_EQ(operations.size(), 5);
	EXPECT_EQ(operations.at(0), QStringLiteral("Add"));
	EXPECT_EQ(operations.at(1), QStringLiteral("Subtract"));
	EXPECT_EQ(operations.at(2), QStringLiteral("Multiply"));
	EXPECT_EQ(operations.at(3), QStringLiteral("Divide"));
	// The combo list matches the Operation enum exactly, so kOpPower == 4
	// selects "Power"
	EXPECT_EQ(operations.at(4), QStringLiteral("Power"));
}

TEST(MathNode, AddNumbers)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 3.0);

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), 5.0);
}

TEST(MathNode, SubtractNumbers)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_subtract);
	math->set_standard_value(olive::MathNode::k_param_a_in, 7.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 10.0);

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), -3.0);
}

TEST(MathNode, MultiplyNumbers)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.5);
	math->set_standard_value(olive::MathNode::k_param_b_in, 4.0);

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), 10.0);
}

TEST(MathNode, DivideNumbers)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);
	math->set_standard_value(olive::MathNode::k_param_a_in, 7.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 2.0);

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), 3.5);
}

TEST(MathNode, PowerNumbers)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_power);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 10.0);

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), 1024.0);
}

TEST(MathNode, DivideByZeroProducesInfinity)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);
	math->set_standard_value(olive::MathNode::k_param_a_in, 1.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 0.0);

	olive::NodeValueTable table = generate_math_table(math);
	const double result = table.get(olive::NodeValue::k_float).to_double();
	EXPECT_TRUE(std::isinf(result));
	EXPECT_GT(result, 0.0);

	// 0 / 0 yields NaN
	math->set_standard_value(olive::MathNode::k_param_a_in, 0.0);
	table = generate_math_table(math);
	EXPECT_TRUE(std::isnan(table.get(olive::NodeValue::k_float).to_double()));
}

TEST(MathNode, ChangingOperationChangesResult)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 3.0);

	const olive::MathNode::Operation ops[] = {
		olive::MathNode::k_op_add,	olive::MathNode::k_op_subtract,
		olive::MathNode::k_op_multiply, olive::MathNode::k_op_divide,
		olive::MathNode::k_op_power
	};
	const double expected[] = { 5.0, -1.0, 6.0, 2.0 / 3.0, 8.0 };

	for (int i = 0; i < 5; i++) {
		math->set_operation(ops[i]);
		olive::NodeValueTable table = generate_math_table(math);
		EXPECT_NEAR(table.get(olive::NodeValue::k_float).to_double(), expected[i],
					1e-6)
			<< "Failed at operation index " << i;
	}
}

TEST(MathNode, AddSubtractRationalsPreserveRationalType)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 2))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 4))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_add);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_rational);
	ASSERT_EQ(result.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(result.to_rational(), olive::core::Rational(3, 4));

	math->set_operation(olive::MathNode::k_op_subtract);
	table = generate_math_table(math);
	result = table.get(olive::NodeValue::k_rational);
	ASSERT_EQ(result.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(result.to_rational(), olive::core::Rational(1, 4));
}

TEST(MathNode, MultiplyDivideRationalsPreserveRationalType)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 2))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 4))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_multiply);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_rational);
	ASSERT_EQ(result.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(result.to_rational(), olive::core::Rational(1, 8));

	math->set_operation(olive::MathNode::k_op_divide);
	table = generate_math_table(math);
	result = table.get(olive::NodeValue::k_rational);
	ASSERT_EQ(result.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(result.to_rational(), olive::core::Rational(2));
}

TEST(MathNode, PowerOnRationalsProducesFloat)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_power);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 2))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(2))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// Power is not supported on rationals, so the result falls back to float
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_FLOAT_EQ(result.to_double(), 0.25);
}

TEST(MathNode, RationalDividedByZeroProducesNaN)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 2))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(0))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_rational);
	ASSERT_EQ(result.type(), olive::NodeValue::k_rational);
	EXPECT_TRUE(result.to_rational().isNaN());
}

TEST(MathNode, MixedRationalAndFloatProducesFloat)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_b_in, 0.5);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_rational,
						 QVariant::fromValue(olive::core::Rational(1, 2))));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// Only Rational+Rational preserves the Rational type
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_FLOAT_EQ(result.to_double(), 1.0);
}

TEST(MathNode, IntegerInputsProduceFloatResult)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	ConstantValueNode *a = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_int, int64_t(7)));
	ConstantValueNode *b = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_int, int64_t(6)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_multiply);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_float);
	ASSERT_EQ(result.type(), olive::NodeValue::k_float);
	EXPECT_FLOAT_EQ(result.to_double(), 42.0);

	// Mixed int and float
	olive::Node::disconnect_edge(b,
								olive::NodeInput(math, olive::MathNode::k_param_b_in));
	math->set_standard_value(olive::MathNode::k_param_b_in, 0.5);
	math->set_operation(olive::MathNode::k_op_add);
	table = generate_math_table(math);
	EXPECT_FLOAT_EQ(table.get(olive::NodeValue::k_float).to_double(), 7.5);
}

TEST(MathNode, AddVectorsPromotesToLargerType)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(1.0f, 2.0f)));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec3,
						 QVector3D(10.0f, 20.0f, 30.0f)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec3);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec3);
	const QVector3D vec = result.to_vec3();
	EXPECT_FLOAT_EQ(vec.x(), 11.0f);
	EXPECT_FLOAT_EQ(vec.y(), 22.0f);
	EXPECT_FLOAT_EQ(vec.z(), 30.0f);
}

TEST(MathNode, SubtractVec4)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_subtract);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec4,
						 QVector4D(5.0f, 7.0f, 9.0f, 11.0f)));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec4,
						 QVector4D(1.0f, 2.0f, 3.0f, 4.0f)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec4);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec4);
	const QVector4D vec = result.to_vec4();
	EXPECT_FLOAT_EQ(vec.x(), 4.0f);
	EXPECT_FLOAT_EQ(vec.y(), 5.0f);
	EXPECT_FLOAT_EQ(vec.z(), 6.0f);
	EXPECT_FLOAT_EQ(vec.w(), 7.0f);
}

TEST(MathNode, MultiplyDivideVectorsComponentwise)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(2.0f, 3.0f)));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(4.0f, 5.0f)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_multiply);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec2);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec2);
	EXPECT_FLOAT_EQ(result.to_vec2().x(), 8.0f);
	EXPECT_FLOAT_EQ(result.to_vec2().y(), 15.0f);

	math->set_operation(olive::MathNode::k_op_divide);
	table = generate_math_table(math);
	result = table.get(olive::NodeValue::k_vec2);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec2);
	EXPECT_FLOAT_EQ(result.to_vec2().x(), 0.5f);
	EXPECT_FLOAT_EQ(result.to_vec2().y(), 0.6f);
}

TEST(MathNode, VectorPowerReturnsFirstInputUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_power);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(2.0f, 3.0f)));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(4.0f, 5.0f)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// Power is not implemented for vector/vector, the first vector is
	// returned unchanged
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec2);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec2);
	EXPECT_FLOAT_EQ(result.to_vec2().x(), 2.0f);
	EXPECT_FLOAT_EQ(result.to_vec2().y(), 3.0f);
}

TEST(MathNode, MultiplyVectorByNumber)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);
	math->set_standard_value(olive::MathNode::k_param_b_in, 2.0);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec4,
						 QVector4D(1.0f, 2.0f, 3.0f, 4.0f)));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec4);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec4);
	const QVector4D vec = result.to_vec4();
	EXPECT_FLOAT_EQ(vec.x(), 2.0f);
	EXPECT_FLOAT_EQ(vec.y(), 4.0f);
	EXPECT_FLOAT_EQ(vec.z(), 6.0f);
	EXPECT_FLOAT_EQ(vec.w(), 8.0f);
}

TEST(MathNode, DivideVectorByNumber)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);
	math->set_standard_value(olive::MathNode::k_param_b_in, 2.0);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec3,
						 QVector3D(2.0f, 4.0f, 6.0f)));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec3);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec3);
	const QVector3D vec = result.to_vec3();
	EXPECT_FLOAT_EQ(vec.x(), 1.0f);
	EXPECT_FLOAT_EQ(vec.y(), 2.0f);
	EXPECT_FLOAT_EQ(vec.z(), 3.0f);
}

TEST(MathNode, AddVectorAndNumberIsNoOp)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_b_in, 5.0);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec2, QVector2D(1.0f, 2.0f)));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// Only multiply/divide are implemented for vector+number; add returns
	// the vector unchanged
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec2);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec2);
	EXPECT_FLOAT_EQ(result.to_vec2().x(), 1.0f);
	EXPECT_FLOAT_EQ(result.to_vec2().y(), 2.0f);
}

TEST(MathNode, NumberTimesVectorScalesVector)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.0);

	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec4,
						 QVector4D(1.0f, 2.0f, 3.0f, 4.0f)));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// With the number in parameter A and the vector in parameter B, the
	// number is still picked as the number operand and the vector is scaled,
	// mirroring MultiplyVectorByNumber with the operands swapped
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec4);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec4);
	const QVector4D vec = result.to_vec4();
	EXPECT_FLOAT_EQ(vec.x(), 2.0f);
	EXPECT_FLOAT_EQ(vec.y(), 4.0f);
	EXPECT_FLOAT_EQ(vec.z(), 6.0f);
	EXPECT_FLOAT_EQ(vec.w(), 8.0f);
}

TEST(MathNode, MultiplyMatrixByVector)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);

	QMatrix4x4 matrix;
	matrix.scale(2.0f, 3.0f, 4.0f);

	ConstantValueNode *a = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_matrix, matrix));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_vec4,
						 QVector4D(1.0f, 2.0f, 3.0f, 1.0f)));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_vec4);
	ASSERT_EQ(result.type(), olive::NodeValue::k_vec4);
	const QVector4D vec = result.to_vec4();
	EXPECT_FLOAT_EQ(vec.x(), 2.0f);
	EXPECT_FLOAT_EQ(vec.y(), 6.0f);
	EXPECT_FLOAT_EQ(vec.z(), 12.0f);
	EXPECT_FLOAT_EQ(vec.w(), 1.0f);
}

TEST(MathNode, MatrixMatrixAddAndMultiply)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	QMatrix4x4 mat_a;
	mat_a.scale(2.0f, 3.0f, 4.0f);
	QMatrix4x4 mat_b;
	mat_b.scale(3.0f, 4.0f, 5.0f);

	ConstantValueNode *a = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_matrix, mat_a));
	ConstantValueNode *b = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_matrix, mat_b));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_multiply);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_matrix);
	ASSERT_EQ(result.type(), olive::NodeValue::k_matrix);
	const QMatrix4x4 product = result.to_matrix();
	EXPECT_FLOAT_EQ(product(0, 0), 6.0f);
	EXPECT_FLOAT_EQ(product(1, 1), 12.0f);
	EXPECT_FLOAT_EQ(product(2, 2), 20.0f);
	EXPECT_FLOAT_EQ(product(3, 3), 1.0f);

	math->set_operation(olive::MathNode::k_op_add);
	table = generate_math_table(math);
	result = table.get(olive::NodeValue::k_matrix);
	ASSERT_EQ(result.type(), olive::NodeValue::k_matrix);
	const QMatrix4x4 sum = result.to_matrix();
	EXPECT_FLOAT_EQ(sum(0, 0), 5.0f);
	EXPECT_FLOAT_EQ(sum(1, 1), 7.0f);
	EXPECT_FLOAT_EQ(sum(2, 2), 9.0f);
	EXPECT_FLOAT_EQ(sum(3, 3), 2.0f);
}

TEST(MathNode, MatrixDivideReturnsFirstInputUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);

	QMatrix4x4 mat_a;
	mat_a.scale(2.0f, 3.0f, 4.0f);
	QMatrix4x4 mat_b;
	mat_b.scale(9.0f, 9.0f, 9.0f);

	ConstantValueNode *a = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_matrix, mat_a));
	ConstantValueNode *b = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_matrix, mat_b));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// Divide is not implemented for matrices, the first matrix is returned
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_matrix);
	ASSERT_EQ(result.type(), olive::NodeValue::k_matrix);
	const QMatrix4x4 out = result.to_matrix();
	EXPECT_FLOAT_EQ(out(0, 0), 2.0f);
	EXPECT_FLOAT_EQ(out(1, 1), 3.0f);
	EXPECT_FLOAT_EQ(out(2, 2), 4.0f);
}

TEST(MathNode, AddAndSubtractColors)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.1f, 0.2f, 0.3f, 0.4f))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.4f, 0.3f, 0.2f, 0.1f))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	math->set_operation(olive::MathNode::k_op_add);
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_color);
	ASSERT_EQ(result.type(), olive::NodeValue::k_color);
	const olive::core::Color sum = result.to_color();
	EXPECT_FLOAT_EQ(sum.red(), 0.5f);
	EXPECT_FLOAT_EQ(sum.green(), 0.5f);
	EXPECT_FLOAT_EQ(sum.blue(), 0.5f);
	EXPECT_FLOAT_EQ(sum.alpha(), 0.5f);

	math->set_operation(olive::MathNode::k_op_subtract);
	table = generate_math_table(math);
	result = table.get(olive::NodeValue::k_color);
	ASSERT_EQ(result.type(), olive::NodeValue::k_color);
	const olive::core::Color diff = result.to_color();
	EXPECT_FLOAT_EQ(diff.red(), -0.3f);
	EXPECT_FLOAT_EQ(diff.green(), -0.1f);
	EXPECT_FLOAT_EQ(diff.blue(), 0.1f);
	EXPECT_FLOAT_EQ(diff.alpha(), 0.3f);
}

TEST(MathNode, MultiplyColorsReturnsFirstInputUnchanged)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.1f, 0.2f, 0.3f, 1.0f))));
	ConstantValueNode *b = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.4f, 0.5f, 0.6f, 1.0f))));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// Only add/subtract are implemented for colors, the first color is
	// returned unchanged
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_color);
	ASSERT_EQ(result.type(), olive::NodeValue::k_color);
	const olive::core::Color out = result.to_color();
	EXPECT_FLOAT_EQ(out.red(), 0.1f);
	EXPECT_FLOAT_EQ(out.green(), 0.2f);
	EXPECT_FLOAT_EQ(out.blue(), 0.3f);
	EXPECT_FLOAT_EQ(out.alpha(), 1.0f);
}

TEST(MathNode, MultiplyColorByNumber)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);
	math->set_standard_value(olive::MathNode::k_param_b_in, 4.0);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.25f, 0.5f, 0.75f, 1.0f))));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_color);
	ASSERT_EQ(result.type(), olive::NodeValue::k_color);
	const olive::core::Color out = result.to_color();
	EXPECT_FLOAT_EQ(out.red(), 1.0f);
	EXPECT_FLOAT_EQ(out.green(), 2.0f);
	EXPECT_FLOAT_EQ(out.blue(), 3.0f);
	EXPECT_FLOAT_EQ(out.alpha(), 4.0f);
}

TEST(MathNode, DivideColorByNumberIsNoOp)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);
	math->set_standard_value(olive::MathNode::k_param_b_in, 4.0);

	ConstantValueNode *a = create_constant(
		&project,
		olive::NodeValue(olive::NodeValue::k_color,
						 QVariant::fromValue(
							 olive::core::Color(0.25f, 0.5f, 0.75f, 1.0f))));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// Only multiply is implemented for color+number, the color is returned
	// unchanged
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_color);
	ASSERT_EQ(result.type(), olive::NodeValue::k_color);
	const olive::core::Color out = result.to_color();
	EXPECT_FLOAT_EQ(out.red(), 0.25f);
	EXPECT_FLOAT_EQ(out.green(), 0.5f);
	EXPECT_FLOAT_EQ(out.blue(), 0.75f);
	EXPECT_FLOAT_EQ(out.alpha(), 1.0f);
}

TEST(MathNode, NoPairingForNoneInputLeavesInputsUntouched)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_a_in, 42.0);

	// A kNone value has no valid pairing, so Value() returns without pushing
	// a result; the inputs do not leak into the output table either
	ConstantValueNode *b = create_constant(&project, olive::NodeValue());
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	EXPECT_EQ(table.get(olive::NodeValue::k_float).type(),
			  olive::NodeValue::k_none);
	EXPECT_EQ(table.get(olive::NodeValue::k_vec4).type(),
			  olive::NodeValue::k_none);
}

TEST(MathNode, DisabledNodeDoesNotComputeResult)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_a_in, 2.0);
	math->set_standard_value(olive::MathNode::k_param_b_in, 3.0);
	math->set_standard_value(olive::Node::k_enabled_input, false);

	// The node is bypassed, so the result is one of the inputs rather than
	// their sum (merged input order is unspecified)
	olive::NodeValueTable table = generate_math_table(math);
	const double result = table.get(olive::NodeValue::k_float).to_double();
	EXPECT_TRUE(result == 2.0 || result == 3.0);
}

TEST(MathNode, MultiplySamplesByStaticNumber)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);
	math->set_standard_value(olive::MathNode::k_param_b_in, 2.0);

	ConstantValueNode *a = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
									 { 5.0f, 6.0f, 7.0f, 8.0f })));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(result.type(), olive::NodeValue::k_samples);
	const olive::core::SampleBuffer out = result.to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1) * 2.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5) * 2.0f);
	}
}

TEST(MathNode, AddZeroToSamplesIsNoOpButStillPushesBuffer)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);
	math->set_standard_value(olive::MathNode::k_param_b_in, 0.0);

	ConstantValueNode *a = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
									 { 5.0f, 6.0f, 7.0f, 8.0f })));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	// Adding 0 is a no-op, but the (unmodified) buffer is still pushed
	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(result.type(), olive::NodeValue::k_samples);
	const olive::core::SampleBuffer out = result.to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1));
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5));
	}
}

TEST(MathNode, DivideSamplesByZeroProducesInfinity)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_divide);
	math->set_standard_value(olive::MathNode::k_param_b_in, 0.0);

	ConstantValueNode *a = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, -1.0f, 0.0f, 2.0f },
									 { 1.0f, -1.0f, 0.0f, 2.0f })));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int channel = 0; channel < 2; channel++) {
		EXPECT_TRUE(std::isinf(out.data(channel)[0]));
		EXPECT_TRUE(std::isinf(out.data(channel)[1]));
		EXPECT_TRUE(std::isnan(out.data(channel)[2]));
		EXPECT_TRUE(std::isinf(out.data(channel)[3]));
	}
}

TEST(MathNode, PowerSamplesByStaticNumber)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_power);
	math->set_standard_value(olive::MathNode::k_param_b_in, 2.0);

	ConstantValueNode *a = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
									 { 5.0f, 6.0f, 7.0f, 8.0f })));
	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));

	olive::NodeValueTable table = generate_math_table(math);
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], std::pow(float(i + 1), 2.0f));
		EXPECT_FLOAT_EQ(out.data(1)[i], std::pow(float(i + 5), 2.0f));
	}
}

TEST(MathNode, AddSampleBuffersMixesAndKeepsRemainder)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_add);

	ConstantValueNode *a = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
									 { 5.0f, 6.0f, 7.0f, 8.0f })));
	ConstantValueNode *b = create_constant(
		&project,
		sample_value(make_sample_buffer({ 10.0f, 20.0f }, { 50.0f, 60.0f })));

	olive::Node::connect_edge(a, olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(b, olive::NodeInput(math, olive::MathNode::k_param_b_in));

	// The output is as long as the longer buffer: overlapping samples are
	// mixed, the remainder is copied from the longer buffer
	olive::NodeValueTable table = generate_math_table(math);
	const olive::core::SampleBuffer out =
		table.get(olive::NodeValue::k_samples).to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	EXPECT_FLOAT_EQ(out.data(0)[0], 11.0f);
	EXPECT_FLOAT_EQ(out.data(0)[1], 22.0f);
	EXPECT_FLOAT_EQ(out.data(0)[2], 3.0f);
	EXPECT_FLOAT_EQ(out.data(0)[3], 4.0f);
	EXPECT_FLOAT_EQ(out.data(1)[0], 55.0f);
	EXPECT_FLOAT_EQ(out.data(1)[1], 66.0f);
	EXPECT_FLOAT_EQ(out.data(1)[2], 7.0f);
	EXPECT_FLOAT_EQ(out.data(1)[3], 8.0f);
}

TEST(MathNode, ConnectedNumberProducesSampleJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::MathNode *math = create_math_node(&project);
	math->set_operation(olive::MathNode::k_op_multiply);

	// A connected (non-static) number produces a SampleJob instead of
	// processing the buffer immediately
	ConstantValueNode *number = create_constant(
		&project, olive::NodeValue(olive::NodeValue::k_float, 3.0));
	ConstantValueNode *samples = create_constant(
		&project,
		sample_value(make_sample_buffer({ 1.0f, 2.0f, 3.0f, 4.0f },
									 { 5.0f, 6.0f, 7.0f, 8.0f })));

	olive::Node::connect_edge(number,
							 olive::NodeInput(math, olive::MathNode::k_param_a_in));
	olive::Node::connect_edge(samples,
							 olive::NodeInput(math, olive::MathNode::k_param_b_in));

	olive::NodeValueTable table = generate_math_table(math);
	olive::NodeValue result = table.get(olive::NodeValue::k_samples);
	ASSERT_EQ(result.type(), olive::NodeValue::k_samples);
	ASSERT_TRUE(result.canConvert<olive::SampleJob>());

	// Resolve the job on the CPU and verify the processed samples
	SampleResolvingTraverser resolver;
	resolver.resolve(result);

	const olive::core::SampleBuffer out = result.to_samples();
	ASSERT_TRUE(out.is_allocated());
	ASSERT_EQ(out.sample_count(), 4u);
	for (int i = 0; i < 4; i++) {
		EXPECT_FLOAT_EQ(out.data(0)[i], float(i + 1) * 3.0f);
		EXPECT_FLOAT_EQ(out.data(1)[i], float(i + 5) * 3.0f);
	}
}

TEST(MathNode, ProcessSamplesAppliesOperationPerSample)
{
	olive::MathNode math;
	math.set_operation(olive::MathNode::k_op_multiply);

	olive::NodeValueRow row;
	row.insert(olive::MathNode::k_param_a_in,
			   olive::NodeValue(olive::NodeValue::k_float, 3.0));

	olive::core::SampleBuffer input(test_audio_params(), 2);
	olive::core::SampleBuffer output(test_audio_params(), 2);
	input.data(0)[0] = 1.5f;
	input.data(0)[1] = -2.0f;
	input.data(1)[0] = 0.25f;
	input.data(1)[1] = 8.0f;

	math.process_samples(row, input, output, 0);
	math.process_samples(row, input, output, 1);

	EXPECT_FLOAT_EQ(output.data(0)[0], 4.5f);
	EXPECT_FLOAT_EQ(output.data(0)[1], -6.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 0.75f);
	EXPECT_FLOAT_EQ(output.data(1)[1], 24.0f);
}

TEST(MathNode, ProcessSamplesUsesSecondParamWhenFirstMissing)
{
	olive::MathNode math;
	math.set_operation(olive::MathNode::k_op_subtract);

	olive::NodeValueRow row;
	row.insert(olive::MathNode::k_param_b_in,
			   olive::NodeValue(olive::NodeValue::k_float, 4.0));

	olive::core::SampleBuffer input(test_audio_params(), 1);
	olive::core::SampleBuffer output(test_audio_params(), 1);
	input.data(0)[0] = 10.0f;
	input.data(1)[0] = 1.0f;

	math.process_samples(row, input, output, 0);

	EXPECT_FLOAT_EQ(output.data(0)[0], 6.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], -3.0f);
}

TEST(MathNode, ProcessSamplesWithoutNumberLeavesOutputUntouched)
{
	olive::MathNode math;
	math.set_operation(olive::MathNode::k_op_multiply);

	// Neither parameter carries a number: output must not be written
	olive::NodeValueRow row;

	olive::core::SampleBuffer input(test_audio_params(), 1);
	olive::core::SampleBuffer output(test_audio_params(), 1);
	input.data(0)[0] = 10.0f;
	output.data(0)[0] = 123.0f;
	output.data(1)[0] = 45.0f;

	math.process_samples(row, input, output, 0);

	EXPECT_FLOAT_EQ(output.data(0)[0], 123.0f);
	EXPECT_FLOAT_EQ(output.data(1)[0], 45.0f);
}

TEST(MathNode, ShaderCodeForNumberAdd)
{
	olive::MathNode math;

	const olive::ShaderCode code =
		math.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("0.0.2.2")));

	EXPECT_TRUE(code.vert_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform float param_a_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform float param_b_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("vec4 c = param_a_in + param_b_in;")));
}

TEST(MathNode, ShaderCodeForTextureNumberPower)
{
	olive::MathNode math;

	// kOpPower / kPairTextureNumber / kTexture / kFloat
	const olive::ShaderCode code =
		math.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("4.8.10.2")));

	EXPECT_TRUE(code.vert_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform sampler2D param_a_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform float param_b_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("pow(texture(param_a_in, ove_texcoord), vec4(param_b_in))")));
}

TEST(MathNode, ShaderCodeForColorPairUsesVec4Uniforms)
{
	olive::MathNode math;

	// kOpAdd / kPairColorColor / kColor / kColor
	const olive::ShaderCode code =
		math.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("0.7.5.5")));

	EXPECT_TRUE(code.vert_code().isEmpty());
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform vec4 param_a_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("uniform vec4 param_b_in;")));
	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("vec4 c = param_a_in + param_b_in;")));
}

TEST(MathNode, ShaderCodeForTextureMatrixMultiplyHasVertexShader)
{
	olive::MathNode math;

	// kOpMultiply / kPairTextureMatrix / kTexture / kMatrix
	olive::ShaderCode code =
		math.get_shader_code(olive::Node::ShaderRequest(QStringLiteral("2.10.10.6")));

	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("texture(param_a_in, ove_texcoord)")));
	EXPECT_TRUE(code.vert_code().contains(
		QStringLiteral("uniform mat4 param_b_in;")));
	EXPECT_TRUE(code.vert_code().contains(
		QStringLiteral("gl_Position = param_b_in * a_position;")));

	// Reversed operand order swaps the roles of the parameters
	code = math.get_shader_code(
		olive::Node::ShaderRequest(QStringLiteral("2.10.6.10")));

	EXPECT_TRUE(code.frag_code().contains(
		QStringLiteral("texture(param_b_in, ove_texcoord)")));
	EXPECT_TRUE(code.vert_code().contains(
		QStringLiteral("uniform mat4 param_a_in;")));
	EXPECT_TRUE(code.vert_code().contains(
		QStringLiteral("gl_Position = param_a_in * a_position;")));
}
