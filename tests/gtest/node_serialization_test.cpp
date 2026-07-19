#include <gtest/gtest.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/node.h"
#include "node/serializeddata.h"
#include "node/splitvalue.h"
#include "node/value.h"
#include "render/diskmanager.h"

namespace
{
class TestNode final : public olive::Node {
public:
	TestNode()
	{
		add_input(QStringLiteral("Value"), olive::NodeValue::k_float);
		olive::SplitValue value;
		value.append(3.5);
		set_split_standard_value(QStringLiteral("Value"), value, -1);
	}

	TestNode *copy() const override
	{
		return new TestNode();
	}

	QString name() const override
	{
		return QStringLiteral("TestNode");
	}

	QString id() const override
	{
		return QStringLiteral("org.olivevideoeditor.TestNode");
	}

	QVector<CategoryID> category() const override
	{
		return { k_category_unknown };
	}

	QString description() const override
	{
		return QStringLiteral("Test node for serialization");
	}

	void value(const olive::NodeValueRow &, const olive::NodeGlobals &,
			   olive::NodeValueTable *) const override
	{
	}
};
}

TEST(NodeSerialization, SaveAndLoadInput)
{
	const bool created_disk_manager =
		(olive::DiskManager::instance() == nullptr);
	if (created_disk_manager) {
		olive::DiskManager::create_instance();
	}

	TestNode node;
	node.set_label(QStringLiteral("MyNode"));
	node.set_override_color(2);

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("node"));
	node.save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();
	buffer.close();

	TestNode loaded;
	olive::SerializedData data;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	EXPECT_TRUE(reader.readNextStartElement());
	EXPECT_EQ(reader.name().toString(), QStringLiteral("node"));
	EXPECT_TRUE(loaded.load(&reader, &data));

	EXPECT_EQ(loaded.get_label(), QStringLiteral("MyNode"));
	EXPECT_EQ(loaded.get_override_color(), 2);
	EXPECT_DOUBLE_EQ(loaded.get_split_standard_value(QStringLiteral("Value"), -1)
						 .first()
						 .toDouble(),
					 3.5);

	if (created_disk_manager) {
		olive::DiskManager::destroy_instance();
	}
}
