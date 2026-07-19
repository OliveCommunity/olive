#include <gtest/gtest.h>

#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/project.h"

TEST(NodeProject, DefaultsAfterConstruction)
{
	olive::Project project;

	EXPECT_EQ(project.filename(), QString());
	EXPECT_EQ(project.name(), QStringLiteral("(untitled)"));
	EXPECT_EQ(project.pretty_filename(), QStringLiteral("(untitled)"));
	EXPECT_TRUE(project.is_new());
	EXPECT_FALSE(project.is_modified());
	EXPECT_TRUE(project.has_autorecovery_been_saved());
	EXPECT_FALSE(project.GetUuid().isNull());
	EXPECT_NE(project.color_manager(), nullptr);
	EXPECT_EQ(project.root(), nullptr);
	EXPECT_TRUE(project.nodes().isEmpty());
}

TEST(NodeProject, FilenameAndNameUpdate)
{
	olive::Project project;

	const QString filename = QStringLiteral("test_project.ove");
	project.set_filename(filename);
	EXPECT_EQ(project.filename(), filename);
	EXPECT_EQ(project.name(), QStringLiteral("test_project"));
	EXPECT_EQ(project.pretty_filename(), filename);
	EXPECT_FALSE(project.is_new());
}

TEST(NodeProject, ModifiedState)
{
	olive::Project project;

	project.set_modified(true);
	EXPECT_TRUE(project.is_modified());
	EXPECT_FALSE(project.has_autorecovery_been_saved());

	project.set_modified(false);
	EXPECT_FALSE(project.is_modified());
	EXPECT_TRUE(project.has_autorecovery_been_saved());
}

TEST(NodeProject, SettingsRoundTrip)
{
	olive::Project project;

	project.SetSetting(
		olive::Project::kCacheLocationSettingKey,
		QString::number(olive::Project::kCacheStoreAlongsideProject));
	EXPECT_EQ(project.GetCacheLocationSetting(),
			  olive::Project::kCacheStoreAlongsideProject);

	project.SetCustomCachePath(QStringLiteral("/tmp/cache"));
	EXPECT_EQ(project.GetCustomCachePath(), QStringLiteral("/tmp/cache"));

	project.SetColorConfigFilename(QStringLiteral("config.ocio"));
	EXPECT_EQ(project.GetColorConfigFilename(), QStringLiteral("config.ocio"));

	project.SetDefaultInputColorSpace(QStringLiteral("ACEScg"));
	EXPECT_EQ(project.GetDefaultInputColorSpace(), QStringLiteral("ACEScg"));

	project.SetColorReferenceSpace(QStringLiteral("ACES - ACEScg"));
	EXPECT_EQ(project.GetColorReferenceSpace(),
			  QStringLiteral("ACES - ACEScg"));
}

TEST(NodeProject, InitializeCreatesRoot)
{
	olive::Project project;
	EXPECT_EQ(project.root(), nullptr);

	project.Initialize();
	EXPECT_NE(project.root(), nullptr);
	EXPECT_EQ(project.root()->GetLabel(), QStringLiteral("Root"));
}

TEST(NodeProject, UuidCanBeRegenerated)
{
	olive::Project project;
	const QUuid original = project.GetUuid();

	project.RegenerateUuid();
	EXPECT_FALSE(project.GetUuid().isNull());
	EXPECT_NE(project.GetUuid(), original);
}

TEST(NodeProject, GetProjectFromObject)
{
	olive::Project project;
	olive::ColorManager *cm = project.color_manager();
	EXPECT_EQ(olive::Project::GetProjectFromObject(cm), &project);
}

TEST(NodeProject, SaveProducesXml)
{
	olive::Project project;
	project.Initialize();

	QByteArray xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("project"));
	project.Save(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_FALSE(xml.isEmpty());

	// Parse the document and assert the serialized structure instead of
	// relying on substring matching
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	EXPECT_EQ(reader.name(), QStringLiteral("project"));
	EXPECT_EQ(reader.attributes().value(QStringLiteral("version")).toString(),
			  QStringLiteral("1"));

	bool saw_uuid = false;
	QString uuid_text;
	bool saw_nodes = false;
	bool in_nodes = false;
	int node_count = 0;
	QString first_node_id;
	bool in_settings = false;
	bool saw_settings_root = false;

	while (!reader.atEnd()) {
		const QXmlStreamReader::TokenType token = reader.readNext();
		if (token == QXmlStreamReader::StartElement) {
			const QStringView name = reader.name();
			if (name == QStringLiteral("uuid")) {
				saw_uuid = true;
				uuid_text = reader.readElementText();
			} else if (name == QStringLiteral("nodes")) {
				saw_nodes = true;
				in_nodes = true;
			} else if (in_nodes && name == QStringLiteral("node")) {
				++node_count;
				if (first_node_id.isEmpty()) {
					first_node_id =
						reader.attributes().value(QStringLiteral("id")).toString();
				}
			} else if (name == QStringLiteral("settings")) {
				in_settings = true;
			} else if (in_settings && name == QStringLiteral("root")) {
				saw_settings_root = true;
			}
		} else if (token == QXmlStreamReader::EndElement) {
			if (reader.name() == QStringLiteral("nodes")) {
				in_nodes = false;
			} else if (reader.name() == QStringLiteral("settings")) {
				in_settings = false;
			} else if (reader.name() == QStringLiteral("project")) {
				break;
			}
		}
	}

	ASSERT_FALSE(reader.hasError()) << reader.errorString().toStdString();

	// The project uuid round-trips as a valid uuid
	EXPECT_TRUE(saw_uuid);
	EXPECT_EQ(uuid_text, project.GetUuid().toString());
	EXPECT_FALSE(QUuid(uuid_text).isNull());

	// Initialize() created a root folder, so at least one node is serialized
	// and the first one is that root
	EXPECT_TRUE(saw_nodes);
	EXPECT_GE(node_count, 1);
	EXPECT_EQ(first_node_id, project.root()->id());

	// The root pointer is persisted in the settings block
	EXPECT_TRUE(saw_settings_root);
}
