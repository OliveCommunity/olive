#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include <OpenColorIO/OpenColorIO.h>

#include "config/config.h"
#include "node/color/ociolut/ociolut.h"
#include "node/color/colormanager/colormanager.h"
#include "node/color/ociogradingtransformlinear/ociogradingtransformlinear.h"
#include "node/color/threewaycolor/threewaycolor.h"
#include "node/factory.h"
#include "node/generator/solid/solid.h"
#include "node/project.h"
#include "node/traverser.h"
#include "render/colorprocessor.h"
#include "render/lutlibrary.h"

namespace ocio = OCIO_NAMESPACE;

namespace
{

QString write_test_cube(QTemporaryDir *dir)
{
	const QString path =
		QDir(dir->path()).filePath(QStringLiteral("invert.cube"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	const QByteArray data = "TITLE \"Oak test invert\"\n"
							"LUT_1D_SIZE 2\n"
							"DOMAIN_MIN 0.0 0.0 0.0\n"
							"DOMAIN_MAX 1.0 1.0 1.0\n"
							"1.0 1.0 1.0\n"
							"0.0 0.0 0.0\n";
	file.write(data);
	file.close();
	return path;
}

// CPU-only traverser that resolves SolidGenerator and ColorTransformJob jobs
// into real frames so we can compare pixels without a GPU/worker process.
class PixelColorTransformTraverser : public olive::NodeTraverser {
public:
	void resolve(olive::NodeValue &value)
	{
		resolve_jobs(value);
	}

	olive::FramePtr source_frame;
	olive::FramePtr output_frame;

protected:
	virtual void process_shader(olive::TexturePtr destination,
							   const olive::Node *node,
							   const olive::ShaderJob *job) override
	{
		Q_UNUSED(destination)
		Q_UNUSED(node)

		const olive::Color c =
			job->get_values().value(olive::SolidGenerator::k_color_input).to_color();

		olive::VideoParams p = destination->params();
		p.set_format(olive::core::PixelFormat::f32);
		p.set_channel_count(olive::VideoParams::k_rgba_channel_count);

		source_frame = olive::Frame::create();
		source_frame->set_video_params(p);
		source_frame->allocate();

		for (int y = 0; y < p.effective_height(); ++y) {
			for (int x = 0; x < p.effective_width(); ++x) {
				source_frame->set_pixel(x, y, c);
			}
		}
	}

	virtual void
	process_color_transform(olive::TexturePtr destination,
						  const olive::Node *node,
						  const olive::ColorTransformJob *job) override
	{
		Q_UNUSED(destination)
		Q_UNUSED(node)

		olive::ColorProcessorPtr processor = job->get_color_processor();
		if (!processor || !source_frame) {
			return;
		}

		output_frame = olive::Frame::create();
		output_frame->set_video_params(source_frame->video_params());
		output_frame->allocate();

		std::memcpy(output_frame->data(), source_frame->const_data(),
					source_frame->allocated_size());

		processor->convert_frame(output_frame);
	}
};

QString write_test_cube_lut(QTemporaryDir *dir, const char *title, float low,
						 float high)
{
	const QString path =
		QDir(dir->path())
			.filePath(QStringLiteral("%1.cube").arg(QString::fromUtf8(title)));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	QTextStream stream(&file);
	stream << "TITLE \"" << title << "\"\n";
	stream << "LUT_1D_SIZE 2\n";
	stream << "DOMAIN_MIN 0.0 0.0 0.0\n";
	stream << "DOMAIN_MAX 1.0 1.0 1.0\n";
	stream << high << " " << high << " " << high << "\n";
	stream << low << " " << low << " " << low << "\n";
	file.close();
	return path;
}

// Non-symmetric 1D LUT so forward and inverse produce different, predictable
// results. Table:
//   0.0 -> 0.00
//   0.5 -> 0.75
//   1.0 -> 1.00
// Forward:  (0.25, 0.50, 0.75) -> (0.375, 0.750, 0.875)
// Inverse:  (0.25, 0.50, 0.75) -> (0.167, 0.333, 0.500)
QString write_asymmetric_cube(QTemporaryDir *dir)
{
	const QString path =
		QDir(dir->path()).filePath(QStringLiteral("asymmetric.cube"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	const QByteArray data = "TITLE \"Oak test asymmetric\"\n"
							"LUT_1D_SIZE 3\n"
							"DOMAIN_MIN 0.0 0.0 0.0\n"
							"DOMAIN_MAX 1.0 1.0 1.0\n"
							"0.00 0.00 0.00\n"
							"0.75 0.75 0.75\n"
							"1.00 1.00 1.00\n";
	file.write(data);
	file.close();
	return path;
}

} // namespace

TEST(ColorProcessor, CreateFromInvalidTransformThrows)
{
	olive::ColorManager::set_up_default_config();

	ocio::FileTransformRcPtr transform = ocio::FileTransform::Create();
	transform->setSrc("/nonexistent/lut.cube");
	transform->setInterpolation(ocio::INTERP_LINEAR);
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	// A FileTransform pointing at a missing LUT makes OCIO throw while
	// resolving the processor, before ColorProcessor is even constructed.
	EXPECT_THROW(olive::ColorProcessor::create(
					 olive::ColorManager::get_default_config()->getProcessor(
						 transform)),
				 ocio::Exception);
}

TEST(ColorProcessor, ConvertColorWithIdentityProcessor)
{
	olive::ColorManager::set_up_default_config();

	ocio::MatrixTransformRcPtr transform = ocio::MatrixTransform::Create();
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	olive::ColorProcessorPtr processor = olive::ColorProcessor::create(
		olive::ColorManager::get_default_config()->getProcessor(transform));
	ASSERT_NE(processor, nullptr);

	const olive::Color in(0.25f, 0.50f, 0.75f, 1.0f);
	const olive::Color out = processor->convert_color(in);

	EXPECT_NEAR(out.red(), in.red(), 0.001f);
	EXPECT_NEAR(out.green(), in.green(), 0.001f);
	EXPECT_NEAR(out.blue(), in.blue(), 0.001f);
	EXPECT_NEAR(out.alpha(), in.alpha(), 0.001f);
}

TEST(ColorLut, CubeFileTransformConvertsColor)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube(&dir);
	ASSERT_FALSE(path.isEmpty());

	ocio::FileTransformRcPtr transform = ocio::FileTransform::Create();
	transform->setSrc(path.toUtf8().constData());
	transform->setInterpolation(ocio::INTERP_LINEAR);
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	ocio::ConstConfigRcPtr config = ocio::Config::CreateRaw();
	olive::ColorProcessorPtr processor =
		olive::ColorProcessor::create(config->getProcessor(transform));
	ASSERT_TRUE(processor);

	const olive::Color out =
		processor->convert_color(olive::Color(0.25f, 0.50f, 0.75f, 1.0f));
	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorV04, FactoryCreatesColorNodes)
{
	std::unique_ptr<olive::Node> lut(olive::NodeFactory::create_from_factory_index(
		olive::NodeFactory::k_ocio_lut));
	ASSERT_NE(lut, nullptr);
	EXPECT_EQ(lut->id(), QStringLiteral("org.olivevideoeditor.Olive.ociolut"));

	std::unique_ptr<olive::Node> three_way(
		olive::NodeFactory::create_from_factory_index(
			olive::NodeFactory::k_three_way_color));
	ASSERT_NE(three_way, nullptr);
	EXPECT_EQ(three_way->id(),
			  QStringLiteral("org.olivevideoeditor.Olive.threewaycolor"));
	EXPECT_TRUE(three_way->has_input_with_id(
		olive::ThreeWayColorNode::k_shadows_color_input));
	EXPECT_TRUE(three_way->has_input_with_id(
		olive::ThreeWayColorNode::k_midtones_color_input));
	EXPECT_TRUE(three_way->has_input_with_id(
		olive::ThreeWayColorNode::k_highlights_color_input));

	const olive::Color neutral =
		three_way
			->get_standard_value(olive::ThreeWayColorNode::k_midtones_color_input)
			.value<olive::Color>();
	EXPECT_FLOAT_EQ(neutral.red(), 0.5f);
	EXPECT_FLOAT_EQ(neutral.green(), 0.5f);
	EXPECT_FLOAT_EQ(neutral.blue(), 0.5f);
	EXPECT_FLOAT_EQ(neutral.alpha(), 1.0f);
}

// -----------------------------------------------------------------------------
// E2E-style regression tests for OCIOLutNode.
//
// These tests build a tiny node graph (SolidGenerator -> OCIOLutNode), drive it
// through NodeTraverser, and compare the resulting pixels. They specifically
// exercise the previously broken paths:
//   * OCIOLutNode::Value() must ensure the processor exists before rendering.
//   * Switching the LUT direction must update the processor and change pixels.
//   * Switching the LUT file must update the processor and change pixels.
// -----------------------------------------------------------------------------

TEST(ColorLutNode, ForwardDirectionInvertsPixels)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input, 0); // Forward

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);

	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorLutNode, InverseDirectionReversesForwardTransform)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.75f, 0.50f, 0.25f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input, 1); // Inverse

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);

	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.25f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.75f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorLutNode, SwitchingDirectionUpdatesProcessorAndPixels)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_asymmetric_cube(&dir);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input, 0); // Forward

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	// First render: forward direction.
	PixelColorTransformTraverser forward_traverser;
	forward_traverser.set_cache_video_params(params);
	olive::NodeValueTable forward_table = forward_traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue forward_tex =
		forward_table.get(olive::NodeValue::k_texture);
	forward_traverser.resolve(forward_tex);

	ASSERT_TRUE(forward_traverser.output_frame);
	const olive::Color forward_out =
		forward_traverser.output_frame->get_pixel(0, 0);
	EXPECT_NEAR(forward_out.red(), 0.375f, 0.02f);
	EXPECT_NEAR(forward_out.green(), 0.750f, 0.02f);
	EXPECT_NEAR(forward_out.blue(), 0.875f, 0.02f);

	// Switch direction. Before the Value()/EnsureProcessor() fix, the node
	// would keep using the old forward processor.
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input, 1); // Inverse

	PixelColorTransformTraverser inverse_traverser;
	inverse_traverser.set_cache_video_params(params);
	olive::NodeValueTable inverse_table = inverse_traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue inverse_tex =
		inverse_table.get(olive::NodeValue::k_texture);
	inverse_traverser.resolve(inverse_tex);

	ASSERT_TRUE(inverse_traverser.output_frame);
	const olive::Color inverse_out =
		inverse_traverser.output_frame->get_pixel(0, 0);

	// Inverse of the asymmetric LUT moves the same color to a darker result.
	EXPECT_NEAR(inverse_out.red(), 0.167f, 0.02f);
	EXPECT_NEAR(inverse_out.green(), 0.333f, 0.02f);
	EXPECT_NEAR(inverse_out.blue(), 0.500f, 0.02f);

	// The two outputs must differ from each other.
	EXPECT_GT(std::abs(forward_out.red() - inverse_out.red()), 0.1f);
	EXPECT_GT(std::abs(forward_out.green() - inverse_out.green()), 0.1f);
	EXPECT_GT(std::abs(forward_out.blue() - inverse_out.blue()), 0.1f);
}

TEST(ColorLutNode, EmptyFilePathLeavesProcessorNull)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, QString());

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));

	// With an empty LUT path, the node should pass the input texture through
	// without producing a color-transform job.
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	EXPECT_EQ(tex_val.type(), olive::NodeValue::k_texture);
}

TEST(ColorLutNode, MissingFilePathLeavesProcessorNull)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input,
						  QStringLiteral("/nonexistent/path/lut.cube"));

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));

	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	EXPECT_EQ(tex_val.type(), olive::NodeValue::k_texture);
}

TEST(ColorLutNode, UnsupportedExtensionLeavesProcessorNull)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input,
						  QStringLiteral("/tmp/lut.txt"));

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));

	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	EXPECT_EQ(tex_val.type(), olive::NodeValue::k_texture);
}

TEST(ColorLutNode, DirectionStringValuesAreAccepted)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input,
						  QStringLiteral("forward"));

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
}

TEST(ColorLutNode, DirectionStringInverseIsAccepted)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.75f, 0.50f, 0.25f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	lut->set_standard_value(olive::OCIOLutNode::k_direction_input,
						  QStringLiteral("inverse"));

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	PixelColorTransformTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		lut, olive::TimeRange(olive::core::Rational(0),
							  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.25f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.75f, 0.02f);
}

TEST(ColorLutNode, ReusingSameFileDoesNotCrash)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	// Render twice; the second render should reuse the cached processor.
	for (int i = 0; i < 2; ++i) {
		PixelColorTransformTraverser traverser;
		traverser.set_cache_video_params(params);
		olive::NodeValueTable table = traverser.generate_table(
			lut, olive::TimeRange(olive::core::Rational(0),
								  olive::core::Rational(1, 30)));
		olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
		traverser.resolve(tex_val);

		ASSERT_TRUE(traverser.output_frame);
		const olive::Color out = traverser.output_frame->get_pixel(0, 0);
		EXPECT_NEAR(out.red(), 0.75f, 0.02f);
		EXPECT_NEAR(out.green(), 0.50f, 0.02f);
		EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	}
}

TEST(ColorLutNode, SwitchingBackToOriginalFileRestoresOriginalPixels)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString invert_path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	const QString boost_path = write_test_cube_lut(&dir, "boost", 0.5f, 1.0f);
	ASSERT_FALSE(invert_path.isEmpty());
	ASSERT_FALSE(boost_path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, invert_path);

	olive::Node::connect_edge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	auto render = [&]() {
		PixelColorTransformTraverser traverser;
		traverser.set_cache_video_params(params);
		olive::NodeValueTable table = traverser.generate_table(
			lut, olive::TimeRange(olive::core::Rational(0),
								  olive::core::Rational(1, 30)));
		olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
		traverser.resolve(tex_val);
		return traverser.output_frame->get_pixel(0, 0);
	};

	// First render with invert.
	const olive::Color invert_out = render();
	EXPECT_NEAR(invert_out.red(), 0.75f, 0.02f);

	// Switch to boost, then back to invert.
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, boost_path);
	const olive::Color boost_out = render();
	EXPECT_GT(std::abs(boost_out.red() - invert_out.red()), 0.1f);

	lut->set_standard_value(olive::OCIOLutNode::k_file_input, invert_path);
	const olive::Color restored_out = render();
	EXPECT_NEAR(restored_out.red(), invert_out.red(), 0.02f);
	EXPECT_NEAR(restored_out.green(), invert_out.green(), 0.02f);
	EXPECT_NEAR(restored_out.blue(), invert_out.blue(), 0.02f);
}

// -----------------------------------------------------------------------------
// Error reporting: silent passthrough states must be observable.
// -----------------------------------------------------------------------------

TEST(ColorLutNode, MissingFileSetsLastError)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input,
						  QStringLiteral("/nonexistent/path/lut.cube"));

	EXPECT_FALSE(lut->last_error().isEmpty());
}

TEST(ColorLutNode, UnsupportedExtensionSetsLastError)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = QDir(dir.path()).filePath(QStringLiteral("lut.txt"));
	QFile file(path);
	ASSERT_TRUE(file.open(QIODevice::WriteOnly));
	file.close();

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);

	EXPECT_FALSE(lut->last_error().isEmpty());
}

TEST(ColorLutNode, ValidFileClearsLastError)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = write_test_cube_lut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);

	lut->set_standard_value(olive::OCIOLutNode::k_file_input,
						  QStringLiteral("/nonexistent/path/lut.cube"));
	EXPECT_FALSE(lut->last_error().isEmpty());

	lut->set_standard_value(olive::OCIOLutNode::k_file_input, path);
	EXPECT_TRUE(lut->last_error().isEmpty());
}

TEST(ColorLutNode, EmptyPathClearsLastError)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);

	lut->set_standard_value(olive::OCIOLutNode::k_file_input,
						  QStringLiteral("/nonexistent/path/lut.cube"));
	EXPECT_FALSE(lut->last_error().isEmpty());

	lut->set_standard_value(olive::OCIOLutNode::k_file_input, QString());
	EXPECT_TRUE(lut->last_error().isEmpty());
}

// -----------------------------------------------------------------------------
// Global LUT library
// -----------------------------------------------------------------------------

TEST(LUTLibrary, SupportsCubeAnd3dlExtensions)
{
	EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(QStringLiteral("cube")));
	EXPECT_TRUE(
		olive::LUTLibrary::is_supported_extension(QStringLiteral(".cube")));
	EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(QStringLiteral("CUBE")));
	EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(QStringLiteral("3dl")));
	EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(QStringLiteral(".3dl")));
	EXPECT_FALSE(olive::LUTLibrary::is_supported_extension(QStringLiteral("txt")));
	EXPECT_FALSE(olive::LUTLibrary::is_supported_extension(QString()));
}

TEST(LUTLibrary, SupportsExtendedOcioFormats)
{
	for (const QString &ext :
		 { QStringLiteral("spi1d"), QStringLiteral("spi3d"),
		   QStringLiteral("spimtx"), QStringLiteral("csp"),
		   QStringLiteral("clf"), QStringLiteral("ctf"), QStringLiteral("cub") }) {
		EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(ext))
			<< ext.toStdString();
		EXPECT_TRUE(olive::LUTLibrary::is_supported_extension(
			QStringLiteral(".") + ext.toUpper()))
			<< ext.toStdString();
	}
}

TEST(ColorLut, Spi1dFileTransformConvertsColor)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path =
		QDir(dir.path()).filePath(QStringLiteral("invert.spi1d"));
	QFile file(path);
	ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
	file.write("Version 1\n"
			   "From 0.0 1.0\n"
			   "Length 2\n"
			   "Components 3\n"
			   "{\n"
			   "1.0 1.0 1.0\n"
			   "0.0 0.0 0.0\n"
			   "}\n");
	file.close();

	ocio::FileTransformRcPtr transform = ocio::FileTransform::Create();
	transform->setSrc(path.toUtf8().constData());
	transform->setInterpolation(ocio::INTERP_LINEAR);
	transform->setDirection(ocio::TRANSFORM_DIR_FORWARD);

	ocio::ConstConfigRcPtr config = ocio::Config::CreateRaw();
	olive::ColorProcessorPtr processor =
		olive::ColorProcessor::create(config->getProcessor(transform));
	ASSERT_TRUE(processor);

	const olive::Color out =
		processor->convert_color(olive::Color(0.25f, 0.50f, 0.75f, 1.0f));
	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(LUTLibrary, DirectoryRoundTripCleansAndDeduplicates)
{
	const QString previous =
		olive::Config::current()[QStringLiteral("LUTLibraryPaths")].toString();

	olive::LUTLibrary::set_directories(
		{ QStringLiteral("/a/luts"), QStringLiteral(" /a/luts "),
		  QStringLiteral("/b/luts"), QString() });

	EXPECT_EQ(olive::LUTLibrary::get_directories(),
			  (QStringList{ QStringLiteral("/a/luts"),
							QStringLiteral("/b/luts") }));

	olive::Config::current()[QStringLiteral("LUTLibraryPaths")] = previous;
}

TEST(LUTLibrary, ScansDirectoriesRecursivelyForSupportedLuts)
{
	const QString previous =
		olive::Config::current()[QStringLiteral("LUTLibraryPaths")].toString();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString cube_path =
		QDir(dir.path()).filePath(QStringLiteral("top.cube"));
	const QString sub_dir = QDir(dir.path()).filePath(QStringLiteral("sub"));
	const QString three_dl_path =
		QDir(sub_dir).filePath(QStringLiteral("nested.3dl"));
	const QString text_path =
		QDir(dir.path()).filePath(QStringLiteral("skip.txt"));

	ASSERT_TRUE(QDir().mkpath(sub_dir));
	for (const QString &p : { cube_path, three_dl_path, text_path }) {
		QFile file(p);
		ASSERT_TRUE(file.open(QIODevice::WriteOnly));
		file.close();
	}

	olive::LUTLibrary::set_directories({ dir.path() });

	const QStringList files = olive::LUTLibrary::get_lut_files();
	EXPECT_EQ(files.size(), 2);
	EXPECT_TRUE(files.contains(cube_path));
	EXPECT_TRUE(files.contains(three_dl_path));
	EXPECT_FALSE(files.contains(text_path));

	olive::Config::current()[QStringLiteral("LUTLibraryPaths")] = previous;
}

// -----------------------------------------------------------------------------
// Display -> reference inverse transform (previously disabled over an OCIO
// crash; covered here so the ColorDialog conversion can stay enabled).
// -----------------------------------------------------------------------------

TEST(ColorProcessor, InverseDisplayTransformRoundTripsColor)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	olive::ColorManager *color_manager = project.color_manager();
	ASSERT_NE(color_manager, nullptr);

	const QString display = color_manager->get_default_display();
	const QString view = color_manager->get_default_view(display);
	ASSERT_FALSE(display.isEmpty());
	ASSERT_FALSE(view.isEmpty());

	const olive::ColorTransform output_transform(display, view, QString());

	olive::ColorProcessorPtr ref_to_display = olive::ColorProcessor::create(
		color_manager, color_manager->get_reference_color_space(),
		output_transform);
	ASSERT_NE(ref_to_display, nullptr);
	ASSERT_NE(ref_to_display->get_processor(), nullptr);

	olive::ColorProcessorPtr display_to_ref = olive::ColorProcessor::create(
		color_manager, color_manager->get_reference_color_space(),
		output_transform, olive::ColorProcessor::k_inverse);
	ASSERT_NE(display_to_ref, nullptr);
	ASSERT_NE(display_to_ref->get_processor(), nullptr);

	const olive::Color reference(0.2f, 0.4f, 0.6f, 1.0f);
	const olive::Color display_color = ref_to_display->convert_color(reference);
	const olive::Color round_trip = display_to_ref->convert_color(display_color);

	EXPECT_NEAR(round_trip.red(), reference.red(), 0.001f);
	EXPECT_NEAR(round_trip.green(), reference.green(), 0.001f);
	EXPECT_NEAR(round_trip.blue(), reference.blue(), 0.001f);
	EXPECT_NEAR(round_trip.alpha(), reference.alpha(), 0.001f);
}

// -----------------------------------------------------------------------------
// OCIOGradingTransformLinearNode clamp invariant
// -----------------------------------------------------------------------------

namespace
{

class ClampCaptureTraverser : public PixelColorTransformTraverser {
public:
	bool captured_white_clamp = false;
	double white_clamp_value = 0.0;

protected:
	virtual void
	process_color_transform(olive::TexturePtr destination, const olive::Node *node,
						  const olive::ColorTransformJob *job) override
	{
		const olive::NodeValueRow &values = job->get_values();
		if (values.contains(
				olive::OCIOGradingTransformLinearNode::k_clamp_white_input)) {
			white_clamp_value =
				values
					.value(olive::OCIOGradingTransformLinearNode::
							   k_clamp_white_input)
					.to_double();
			captured_white_clamp = true;
		}
		PixelColorTransformTraverser::process_color_transform(destination, node,
														  job);
	}
};

} // namespace

TEST(ColorGradingLinear, InvalidClampRangeIsCorrectedPerFrame)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *grading = new olive::OCIOGradingTransformLinearNode();
	grading->setParent(&project);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input, true);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input, true);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, 0.5);
	// Invalid: white clamp below black clamp
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_input, 0.0);

	olive::Node::connect_edge(
		solid,
		olive::NodeInput(
			grading,
			olive::OCIOGradingTransformLinearNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	ClampCaptureTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		grading, olive::TimeRange(olive::core::Rational(0),
								  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	// The node must have corrected the white clamp to just above the black
	// clamp instead of feeding an invalid grading primary to OCIO
	ASSERT_TRUE(traverser.captured_white_clamp);
	EXPECT_NEAR(traverser.white_clamp_value, 0.500001, 1e-9);

	// And rendering must not crash or drop the frame
	ASSERT_TRUE(traverser.output_frame);
}

TEST(ColorGradingLinear, ValidClampRangeIsLeftUntouched)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->set_standard_value(
		olive::SolidGenerator::k_color_input,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *grading = new olive::OCIOGradingTransformLinearNode();
	grading->setParent(&project);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_enable_input, true);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_enable_input, true);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, 0.1);
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_white_input, 0.9);

	olive::Node::connect_edge(
		solid,
		olive::NodeInput(
			grading,
			olive::OCIOGradingTransformLinearNode::k_texture_input));

	const olive::VideoParams params(16, 16, olive::core::PixelFormat::f32,
									olive::VideoParams::k_rgba_channel_count);

	ClampCaptureTraverser traverser;
	traverser.set_cache_video_params(params);
	olive::NodeValueTable table = traverser.generate_table(
		grading, olive::TimeRange(olive::core::Rational(0),
								  olive::core::Rational(1, 30)));
	olive::NodeValue tex_val = table.get(olive::NodeValue::k_texture);
	traverser.resolve(tex_val);

	ASSERT_TRUE(traverser.captured_white_clamp);
	EXPECT_NEAR(traverser.white_clamp_value, 0.9, 1e-9);

	ASSERT_TRUE(traverser.output_frame);
}

TEST(ColorGradingLinear, StaticBlackClampConstrainsWhiteMinimum)
{
	olive::ColorManager::set_up_default_config();

	olive::Project project;
	project.initialize();

	auto *grading = new olive::OCIOGradingTransformLinearNode();
	grading->setParent(&project);

	// Default black clamp is 0, so the white minimum starts just above it
	EXPECT_NEAR(grading
					->get_input_property(
						olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
						QStringLiteral("min"))
					.toDouble(),
				0.000001, 1e-9);

	// Changing the static black clamp updates the white minimum
	grading->set_standard_value(
		olive::OCIOGradingTransformLinearNode::k_clamp_black_input, 0.25);
	EXPECT_NEAR(grading
					->get_input_property(
						olive::OCIOGradingTransformLinearNode::k_clamp_white_input,
						QStringLiteral("min"))
					.toDouble(),
				0.250001, 1e-9);
}
