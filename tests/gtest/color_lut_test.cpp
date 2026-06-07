#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include <OpenColorIO/OpenColorIO.h>

#include "node/color/ociolut/ociolut.h"
#include "node/color/threewaycolor/threewaycolor.h"
#include "node/factory.h"
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
