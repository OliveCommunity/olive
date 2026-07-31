// Engine-side SerializedLayoutInfo XML (de)serialization tests.
//
// These live in their own translation unit because the app layer now carries
// a mirror type (app/common/serializedlayoutinfoapp.h) with the same
// fully-qualified name; including both headers in one TU is a redefinition
// error. The XML methods (to_xml/from_xml) exist only on the engine type.

#include <gtest/gtest.h>

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializedlayoutinfo.h"

using namespace olive;

TEST(SerializedLayoutInfo, XmlRoundTripPreservesEverything)
{
	Project project;
	project.initialize();
	auto *folder = new Folder();
	folder->setParent(&project);
	auto *sequence = new Sequence();
	sequence->setParent(&project);
	auto *viewer = new ViewerOutput();
	viewer->setParent(&project);

	SerializedLayoutInfo info;
	info.open_folders.push_back(folder);
	info.open_sequences.push_back(sequence);
	info.open_viewers.push_back(viewer);
	PanelLayoutInfo data;
	data[QStringLiteral("splitter")] = QStringLiteral("AAA=");
	info.panel_data[QStringLiteral("TimelinePanel")] = data;
	info.state = QByteArray("binary\x01\x02state", 12);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("layout"));
	info.to_xml(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	QHash<quintptr, Node *> node_map;
	node_map.insert(reinterpret_cast<quintptr>(folder), folder);
	node_map.insert(reinterpret_cast<quintptr>(sequence), sequence);
	node_map.insert(reinterpret_cast<quintptr>(viewer), viewer);

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("layout"));

	SerializedLayoutInfo loaded = SerializedLayoutInfo::from_xml(&reader, node_map);

	ASSERT_EQ(loaded.open_folders.size(), 1);
	EXPECT_EQ(loaded.open_folders.front(), folder);
	ASSERT_EQ(loaded.open_sequences.size(), 1);
	EXPECT_EQ(loaded.open_sequences.front(), sequence);

	// Open viewers must survive the round trip too
	ASSERT_EQ(loaded.open_viewers.size(), 1);
	EXPECT_EQ(loaded.open_viewers.front(), viewer);

	EXPECT_EQ(loaded.state, info.state);

	ASSERT_EQ(loaded.panel_data.count(QStringLiteral("TimelinePanel")), 1);
	EXPECT_EQ(loaded.panel_data
				  .at(QStringLiteral("TimelinePanel"))
				  .at(QStringLiteral("splitter")),
			  QStringLiteral("AAA="));

	// No unknown nodes leak into the viewers list: it must contain exactly the
	// viewer that was added, not a duplicate of the sequences list
	EXPECT_NE(loaded.open_viewers.front(),
			  static_cast<ViewerOutput *>(sequence));
}

TEST(SerializedLayoutInfo, FromXmlSkipsUnknownElementsAndNodes)
{
	const QString xml = QStringLiteral(
		"<layout version=\"1\">"
		"<folders><folder>1</folder><unknown><nested/></unknown></folders>"
		"<timeline><sequence>2</sequence></timeline>"
		"<viewers><viewer>3</viewer></viewers>"
		"<mystery><deep><deeper/></deep></mystery>"
		"<state>QUJD</state>"
		"</layout>");

	// No node map entries: pointers resolve to nullptr
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("layout"));

	SerializedLayoutInfo info = SerializedLayoutInfo::from_xml(&reader, {});

	// Unknown pointers resolve to null but are still listed
	ASSERT_EQ(info.open_folders.size(), 1);
	EXPECT_EQ(info.open_folders.front(), nullptr);
	ASSERT_EQ(info.open_sequences.size(), 1);
	EXPECT_EQ(info.open_sequences.front(), nullptr);
	ASSERT_EQ(info.open_viewers.size(), 1);
	EXPECT_EQ(info.open_viewers.front(), nullptr);
	EXPECT_EQ(info.state, QByteArray("ABC"));
	EXPECT_TRUE(info.panel_data.empty());
}
