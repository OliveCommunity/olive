#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include <OpenColorIO/OpenColorIO.h>

#include "node/color/ociolut/ociolut.h"
#include "node/color/threewaycolor/threewaycolor.h"
#include "node/factory.h"
#include "node/generator/solid/solid.h"
#include "node/project.h"
#include "node/traverser.h"
#include "render/colorprocessor.h"

namespace OCIO = OCIO_NAMESPACE;

namespace {

bool IsOakSupportedLutExtension(QString suffix)
{
	if (suffix.startsWith(QLatin1Char('.'))) {
		suffix.remove(0, 1);
	}
	const QString lower = suffix.toLower();
	return lower == QStringLiteral("cube") || lower == QStringLiteral("3dl");
}

QString WriteTestCube(QTemporaryDir *dir)
{
	const QString path = QDir(dir->path()).filePath(QStringLiteral("invert.cube"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	const QByteArray data =
		"TITLE \"Oak test invert\"\n"
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
	void Resolve(olive::NodeValue &value)
	{
		ResolveJobs(value);
	}

	olive::FramePtr source_frame;
	olive::FramePtr output_frame;

protected:
	virtual void ProcessShader(olive::TexturePtr destination,
							   const olive::Node *node,
							   const olive::ShaderJob *job) override
	{
		Q_UNUSED(destination)
		Q_UNUSED(node)

		const olive::Color c =
			job->GetValues().value(olive::SolidGenerator::kColorInput).toColor();

		olive::VideoParams p = destination->params();
		p.set_format(olive::core::PixelFormat::F32);
		p.set_channel_count(olive::VideoParams::kRGBAChannelCount);

		source_frame = olive::Frame::Create();
		source_frame->set_video_params(p);
		source_frame->allocate();

		for (int y = 0; y < p.effective_height(); ++y) {
			for (int x = 0; x < p.effective_width(); ++x) {
				source_frame->set_pixel(x, y, c);
			}
		}
	}

	virtual void ProcessColorTransform(olive::TexturePtr destination,
									   const olive::Node *node,
									   const olive::ColorTransformJob *job) override
	{
		Q_UNUSED(destination)
		Q_UNUSED(node)

		olive::ColorProcessorPtr processor = job->GetColorProcessor();
		if (!processor || !source_frame) {
			return;
		}

		output_frame = olive::Frame::Create();
		output_frame->set_video_params(source_frame->video_params());
		output_frame->allocate();

		std::memcpy(output_frame->data(), source_frame->const_data(),
					source_frame->allocated_size());

		processor->ConvertFrame(output_frame);
	}
};

QString WriteTestCubeLut(QTemporaryDir *dir, const char *title,
						 float low, float high)
{
	const QString path = QDir(dir->path()).filePath(
		QStringLiteral("%1.cube").arg(QString::fromUtf8(title)));
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
QString WriteAsymmetricCube(QTemporaryDir *dir)
{
	const QString path =
		QDir(dir->path()).filePath(QStringLiteral("asymmetric.cube"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	const QByteArray data =
		"TITLE \"Oak test asymmetric\"\n"
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

TEST(ColorLut, OcioSupportsCubeAnd3dlExtensions)
{
	EXPECT_TRUE(IsOakSupportedLutExtension("cube"));
	EXPECT_TRUE(IsOakSupportedLutExtension(".cube"));
	EXPECT_TRUE(IsOakSupportedLutExtension("3dl"));
	EXPECT_TRUE(IsOakSupportedLutExtension(".3dl"));
	EXPECT_FALSE(IsOakSupportedLutExtension("txt"));
}

TEST(ColorLut, CubeFileTransformConvertsColor)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = WriteTestCube(&dir);
	ASSERT_FALSE(path.isEmpty());

	OCIO::FileTransformRcPtr transform = OCIO::FileTransform::Create();
	transform->setSrc(path.toUtf8().constData());
	transform->setInterpolation(OCIO::INTERP_LINEAR);
	transform->setDirection(OCIO::TRANSFORM_DIR_FORWARD);

	OCIO::ConstConfigRcPtr config = OCIO::Config::CreateRaw();
	olive::ColorProcessorPtr processor =
		olive::ColorProcessor::Create(config->getProcessor(transform));
	ASSERT_TRUE(processor);

	const olive::Color out = processor->ConvertColor(olive::Color(0.25f, 0.50f, 0.75f, 1.0f));
	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorV04, FactoryCreatesColorNodes)
{
	std::unique_ptr<olive::Node> lut(
		olive::NodeFactory::CreateFromFactoryIndex(
			olive::NodeFactory::kOCIOLut));
	ASSERT_NE(lut, nullptr);
	EXPECT_EQ(lut->id(), QStringLiteral("org.olivevideoeditor.Olive.ociolut"));

	std::unique_ptr<olive::Node> three_way(
		olive::NodeFactory::CreateFromFactoryIndex(
			olive::NodeFactory::kThreeWayColor));
	ASSERT_NE(three_way, nullptr);
	EXPECT_EQ(three_way->id(),
			  QStringLiteral("org.olivevideoeditor.Olive.threewaycolor"));
	EXPECT_TRUE(three_way->HasInputWithID(
		olive::ThreeWayColorNode::kShadowsColorInput));
	EXPECT_TRUE(three_way->HasInputWithID(
		olive::ThreeWayColorNode::kMidtonesColorInput));
	EXPECT_TRUE(three_way->HasInputWithID(
		olive::ThreeWayColorNode::kHighlightsColorInput));

	const olive::Color neutral =
		three_way->GetStandardValue(
					  olive::ThreeWayColorNode::kMidtonesColorInput)
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
	olive::ColorManager::SetUpDefaultConfig();

	olive::Project project;
	project.Initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = WriteTestCubeLut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->SetStandardValue(
		olive::SolidGenerator::kColorInput,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->SetStandardValue(olive::OCIOLutNode::kFileInput, path);
	lut->SetStandardValue(olive::OCIOLutNode::kDirectionInput, 0); // Forward

	olive::Node::ConnectEdge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::kTextureInput));

	const olive::VideoParams params(
		16, 16, olive::core::PixelFormat::F32,
		olive::VideoParams::kRGBAChannelCount);

	PixelColorTransformTraverser traverser;
	traverser.SetCacheVideoParams(params);

	olive::NodeValueTable table = traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue tex_val = table.Get(olive::NodeValue::kTexture);
	traverser.Resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.75f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.25f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorLutNode, InverseDirectionReversesForwardTransform)
{
	olive::ColorManager::SetUpDefaultConfig();

	olive::Project project;
	project.Initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = WriteTestCubeLut(&dir, "invert", 0.0f, 1.0f);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->SetStandardValue(
		olive::SolidGenerator::kColorInput,
		QVariant::fromValue(olive::Color(0.75f, 0.50f, 0.25f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->SetStandardValue(olive::OCIOLutNode::kFileInput, path);
	lut->SetStandardValue(olive::OCIOLutNode::kDirectionInput, 1); // Inverse

	olive::Node::ConnectEdge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::kTextureInput));

	const olive::VideoParams params(
		16, 16, olive::core::PixelFormat::F32,
		olive::VideoParams::kRGBAChannelCount);

	PixelColorTransformTraverser traverser;
	traverser.SetCacheVideoParams(params);

	olive::NodeValueTable table = traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue tex_val = table.Get(olive::NodeValue::kTexture);
	traverser.Resolve(tex_val);

	ASSERT_TRUE(traverser.output_frame);
	const olive::Color out = traverser.output_frame->get_pixel(0, 0);

	EXPECT_NEAR(out.red(), 0.25f, 0.02f);
	EXPECT_NEAR(out.green(), 0.50f, 0.02f);
	EXPECT_NEAR(out.blue(), 0.75f, 0.02f);
	EXPECT_NEAR(out.alpha(), 1.0f, 0.001f);
}

TEST(ColorLutNode, SwitchingDirectionUpdatesProcessorAndPixels)
{
	olive::ColorManager::SetUpDefaultConfig();

	olive::Project project;
	project.Initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString path = WriteAsymmetricCube(&dir);
	ASSERT_FALSE(path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->SetStandardValue(
		olive::SolidGenerator::kColorInput,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->SetStandardValue(olive::OCIOLutNode::kFileInput, path);
	lut->SetStandardValue(olive::OCIOLutNode::kDirectionInput, 0); // Forward

	olive::Node::ConnectEdge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::kTextureInput));

	const olive::VideoParams params(
		16, 16, olive::core::PixelFormat::F32,
		olive::VideoParams::kRGBAChannelCount);

	// First render: forward direction.
	PixelColorTransformTraverser forward_traverser;
	forward_traverser.SetCacheVideoParams(params);
	olive::NodeValueTable forward_table = forward_traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue forward_tex =
		forward_table.Get(olive::NodeValue::kTexture);
	forward_traverser.Resolve(forward_tex);

	ASSERT_TRUE(forward_traverser.output_frame);
	const olive::Color forward_out =
		forward_traverser.output_frame->get_pixel(0, 0);
	EXPECT_NEAR(forward_out.red(), 0.375f, 0.02f);
	EXPECT_NEAR(forward_out.green(), 0.750f, 0.02f);
	EXPECT_NEAR(forward_out.blue(), 0.875f, 0.02f);

	// Switch direction. Before the Value()/EnsureProcessor() fix, the node
	// would keep using the old forward processor.
	lut->SetStandardValue(olive::OCIOLutNode::kDirectionInput, 1); // Inverse

	PixelColorTransformTraverser inverse_traverser;
	inverse_traverser.SetCacheVideoParams(params);
	olive::NodeValueTable inverse_table = inverse_traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue inverse_tex =
		inverse_table.Get(olive::NodeValue::kTexture);
	inverse_traverser.Resolve(inverse_tex);

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

TEST(ColorLutNode, SwitchingFileUpdatesProcessorAndPixels)
{
	olive::ColorManager::SetUpDefaultConfig();

	olive::Project project;
	project.Initialize();

	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString invert_path = WriteTestCubeLut(
		&dir, "invert", 0.0f, 1.0f);
	const QString boost_path = WriteTestCubeLut(
		&dir, "boost", 0.5f, 1.0f);
	ASSERT_FALSE(invert_path.isEmpty());
	ASSERT_FALSE(boost_path.isEmpty());

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);
	solid->SetStandardValue(
		olive::SolidGenerator::kColorInput,
		QVariant::fromValue(olive::Color(0.25f, 0.50f, 0.75f, 1.0f)));

	auto *lut = new olive::OCIOLutNode();
	lut->setParent(&project);
	lut->SetStandardValue(olive::OCIOLutNode::kFileInput, invert_path);
	lut->SetStandardValue(olive::OCIOLutNode::kDirectionInput, 0); // Forward

	olive::Node::ConnectEdge(
		solid, olive::NodeInput(lut, olive::OCIOLutNode::kTextureInput));

	const olive::VideoParams params(
		16, 16, olive::core::PixelFormat::F32,
		olive::VideoParams::kRGBAChannelCount);

	// Render with invert LUT.
	PixelColorTransformTraverser invert_traverser;
	invert_traverser.SetCacheVideoParams(params);
	olive::NodeValueTable invert_table = invert_traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue invert_tex = invert_table.Get(olive::NodeValue::kTexture);
	invert_traverser.Resolve(invert_tex);

	ASSERT_TRUE(invert_traverser.output_frame);
	const olive::Color invert_out =
		invert_traverser.output_frame->get_pixel(0, 0);
	EXPECT_NEAR(invert_out.red(), 0.75f, 0.02f);

	// Switch to a different LUT file. Before the fix, the node could keep the
	// old invert processor cached and the output would not change.
	lut->SetStandardValue(olive::OCIOLutNode::kFileInput, boost_path);

	PixelColorTransformTraverser boost_traverser;
	boost_traverser.SetCacheVideoParams(params);
	olive::NodeValueTable boost_table = boost_traverser.GenerateTable(
		lut, olive::TimeRange(olive::core::rational(0),
							  olive::core::rational(1, 30)));
	olive::NodeValue boost_tex = boost_table.Get(olive::NodeValue::kTexture);
	boost_traverser.Resolve(boost_tex);

	ASSERT_TRUE(boost_traverser.output_frame);
	const olive::Color boost_out =
		boost_traverser.output_frame->get_pixel(0, 0);

	// boost LUT maps:
	//   0.0 -> 1.0, 1.0 -> 0.5  (linear: f(x) = 1 - 0.5*x)
	// so (0.25, 0.50, 0.75) -> (0.875, 0.750, 0.625).
	EXPECT_NEAR(boost_out.red(), 0.875f, 0.02f);
	EXPECT_NEAR(boost_out.green(), 0.750f, 0.02f);
	EXPECT_NEAR(boost_out.blue(), 0.625f, 0.02f);

	// The two outputs must differ (boost raises all channels vs invert).
	EXPECT_GT(std::abs(boost_out.red() - invert_out.red()), 0.1f);
	EXPECT_GT(std::abs(boost_out.green() - invert_out.green()), 0.1f);
	EXPECT_GT(std::abs(boost_out.blue() - invert_out.blue()), 0.1f);
}
