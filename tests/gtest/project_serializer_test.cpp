#include <gtest/gtest.h>

#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/factory.h"
#include "node/project.h"
#include "node/input/time/timeinput.h"
#include "node/project/serializer/serializer.h"
#include "node/color/colormanager/colormanager.h"
#include "render/diskmanager.h"

TEST(ProjectSerializer, SaveLoadProjectRoundTrip)
{
	const bool created_disk_manager =
		(olive::DiskManager::instance() == nullptr);
	if (created_disk_manager) {
		olive::DiskManager::create_instance();
	}

	olive::ColorManager::set_up_default_config();
	olive::NodeFactory::initialize();
	olive::ProjectSerializer::initialize();

	olive::Project project;
	project.initialize();

	auto *node = new olive::TimeInput();
	node->set_label(QStringLiteral("TimeInput"));
	node->setParent(&project);

	olive::ProjectSerializer::SaveData save_data(
		olive::ProjectSerializer::k_project, &project, QString());

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	olive::ProjectSerializer::Result save_result =
		olive::ProjectSerializer::save(&writer, save_data);
	EXPECT_EQ(save_result.code(), olive::ProjectSerializer::k_success);
	buffer.close();

	olive::Project loaded_project;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		&loaded_project, &reader, olive::ProjectSerializer::k_project);
	EXPECT_EQ(result.code(), olive::ProjectSerializer::k_success);
	EXPECT_FALSE(loaded_project.nodes().isEmpty());
	ASSERT_TRUE(result.get_load_data().node_ptrs.contains(
		reinterpret_cast<quintptr>(node)));
	EXPECT_TRUE(
		loaded_project.nodes().contains(result.get_load_data().node_ptrs.value(
			reinterpret_cast<quintptr>(node))));

	olive::ProjectSerializer::destroy();
	if (created_disk_manager) {
		olive::DiskManager::destroy_instance();
	}
}
