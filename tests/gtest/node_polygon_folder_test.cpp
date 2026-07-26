#include <gtest/gtest.h>

#include <QBuffer>
#include <QPointF>
#include <QVector2D>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core.h"
#include "codec/frame.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/generator/polygon/polygon.h"
#include "node/generator/shape/generatorwithmerge.h"
#include "node/generator/shape/shapenodebase.h"
#include "node/generator/solid/solid.h"
#include "node/generator/text/textv1.h"
#include "node/generator/text/textv2.h"
#include "node/gizmo/line.h"
#include "node/gizmo/path.h"
#include "node/gizmo/point.h"
#include "node/globals.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/serializer/serializer.h"
#include "node/traverser.h"
#include "olive/core/util/color.h"
#include "render/diskmanager.h"
#include "render/job/generatejob.h"
#include "render/loopmode.h"
#include "render/texture.h"

namespace
{

// Node that pushes a fixed dummy texture, used to feed the base input of
// merge-capable generators without any renderer.
class ConstantTextureNode : public olive::Node {
public:
	ConstantTextureNode() = default;

	NODE_DEFAULT_FUNCTIONS(ConstantTextureNode)

	virtual QString name() const override
	{
		return QStringLiteral("Test Texture");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.constant_texture");
	}

	virtual QVector<CategoryID> category() const override
	{
		return { k_category_generator };
	}

	void set_texture(const olive::TexturePtr &texture)
	{
		texture_ = texture;
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(olive::NodeValue(olive::NodeValue::k_texture, texture_, this));
	}

private:
	olive::TexturePtr texture_;
};

template <typename T> T *add_node(olive::Project *project)
{
	T *node = new T();
	node->setParent(project);
	return node;
}

olive::TimeRange first_frame()
{
	return olive::TimeRange(olive::Rational(0), olive::Rational(1, 30));
}

// A fresh traverser per call: NodeTraverser caches tables per node/range, so
// reusing one would return stale results after changing standard values.
olive::NodeValueTable generate_table(const olive::Node *node,
									const olive::VideoParams &vparams)
{
	olive::NodeTraverser traverser;
	traverser.set_cache_video_params(vparams);
	return traverser.generate_table(node, first_frame());
}

olive::NodeValueRow generate_row(const olive::Node *node)
{
	olive::NodeTraverser traverser;
	return traverser.generate_row(node, first_frame());
}

olive::TexturePtr get_output_texture(const olive::NodeValueTable &table)
{
	return table.get(olive::NodeValue::k_texture).to_texture();
}

// Project save/load touches the DiskManager singleton, which itself touches Core
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

} // namespace

TEST(Folder, MetadataAndChildInputDefinition)
{
	olive::Folder folder;
	EXPECT_EQ(folder.id(), QStringLiteral("org.olivevideoeditor.Olive.folder"));
	EXPECT_EQ(folder.name(), QStringLiteral("Folder"));
	EXPECT_FALSE(folder.description().isEmpty());
	EXPECT_TRUE(folder.category().contains(olive::Node::k_category_project));
	EXPECT_TRUE(folder.is_item());

	// The child input is a non-keyframable array that accepts any node
	EXPECT_TRUE(folder.has_input_with_id(olive::Folder::k_child_input));
	EXPECT_TRUE(folder.input_is_array(olive::Folder::k_child_input));
	EXPECT_EQ(int(folder.get_input_data_type(olive::Folder::k_child_input)),
			  int(olive::NodeValue::k_none));
	EXPECT_FALSE(folder.is_input_keyframable(olive::Folder::k_child_input));

	// Folders provide their own icon; every other data type falls through to
	// the Node base implementation
	EXPECT_TRUE(folder.data(olive::Node::icon).isValid());
	EXPECT_FALSE(folder.data(olive::Node::tooltip).isValid());
}

TEST(Folder, RetranslateSetsChildInputName)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *folder = add_node<olive::Folder>(&project);
	folder->retranslate();

	EXPECT_EQ(folder->get_input_name(olive::Folder::k_child_input),
			  QStringLiteral("Children"));
}

TEST(Folder, AddChildAppendsAndEmitsSignals)
{
	// Declared before the project so teardown signals never outlive them
	QVector<olive::Node *> inserted_items;
	QVector<int> inserted_indices;
	int insert_ends = 0;

	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *child = add_node<olive::SolidGenerator>(&project);

	QObject::connect(folder, &olive::Folder::begin_insert_item,
					 [&inserted_items, &inserted_indices](olive::Node *n,
														 int index) {
						 inserted_items.append(n);
						 inserted_indices.append(index);
					 });
	QObject::connect(folder, &olive::Folder::end_insert_item,
					 [&insert_ends]() { ++insert_ends; });

	olive::FolderAddChild(folder, child).redo_now();

	ASSERT_EQ(folder->item_child_count(), 1);
	EXPECT_EQ(folder->item_child(0), child);
	EXPECT_EQ(folder->children().first(), child);
	EXPECT_EQ(folder->index_of_child(child), 0);
	EXPECT_EQ(folder->index_of_child_in_array(child), 0);
	EXPECT_EQ(child->folder(), folder);

	// The insert index is always the append position: the internal model only
	// ever appends, sorting is left to a proxy model (see folder.cpp)
	ASSERT_EQ(inserted_items.size(), 1);
	EXPECT_EQ(inserted_items.first(), child);
	EXPECT_EQ(inserted_indices.first(), 0);
	EXPECT_EQ(insert_ends, 1);
}

TEST(Folder, AddChildUndoRemovesChildAndEmitsSignals)
{
	// Declared before the project so teardown signals never outlive them
	QVector<olive::Node *> removed_items;
	QVector<int> removed_indices;
	int remove_ends = 0;

	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *child = add_node<olive::SolidGenerator>(&project);

	QObject::connect(folder, &olive::Folder::begin_remove_item,
					 [&removed_items, &removed_indices](olive::Node *n,
														int index) {
						 removed_items.append(n);
						 removed_indices.append(index);
					 });
	QObject::connect(folder, &olive::Folder::end_remove_item,
					 [&remove_ends]() { ++remove_ends; });

	olive::FolderAddChild add(folder, child);
	add.redo_now();
	ASSERT_EQ(folder->item_child_count(), 1);

	add.undo_now();

	EXPECT_EQ(folder->item_child_count(), 0);
	EXPECT_EQ(folder->index_of_child(child), -1);
	EXPECT_EQ(folder->index_of_child_in_array(child), -1);
	EXPECT_EQ(child->folder(), nullptr);

	ASSERT_EQ(removed_items.size(), 1);
	EXPECT_EQ(removed_items.first(), child);
	EXPECT_EQ(removed_indices.first(), 0);
	EXPECT_EQ(remove_ends, 1);
}

TEST(Folder, RemoveElementCommandRemovesAndRestores)
{
	// Declared before the project so teardown signals never outlive them
	QVector<olive::Node *> removed_items;
	QVector<int> removed_indices;

	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *first = add_node<olive::Folder>(&project);
	auto *second = add_node<olive::Folder>(&project);
	olive::FolderAddChild(folder, first).redo_now();
	olive::FolderAddChild(folder, second).redo_now();
	ASSERT_EQ(folder->item_child_count(), 2);

	QObject::connect(folder, &olive::Folder::begin_remove_item,
					 [&removed_items, &removed_indices](olive::Node *n,
														int index) {
						 removed_items.append(n);
						 removed_indices.append(index);
					 });

	olive::Folder::RemoveElementCommand remove(folder, first);
	remove.redo_now();

	EXPECT_EQ(folder->item_child_count(), 1);
	EXPECT_EQ(folder->item_child(0), second);
	EXPECT_EQ(first->folder(), nullptr);
	// RemoveElementCommand removes the edge and the array element as separate
	// subcommands, each of which fires BeginRemoveItem
	ASSERT_EQ(removed_items.size(), 2);
	EXPECT_EQ(removed_items.first(), first);
	EXPECT_EQ(removed_indices.first(), 0);

	remove.undo_now();

	// The connection is restored at its original array element, but
	// Folder::InputConnectedEvent only ever appends to the internal model, so
	// the restored child lands at the end of the children list
	ASSERT_EQ(folder->item_child_count(), 2);
	EXPECT_EQ(folder->item_child(0), second);
	EXPECT_EQ(folder->item_child(1), first);
	EXPECT_EQ(folder->index_of_child(first), 1);
	EXPECT_EQ(folder->index_of_child_in_array(first), 0);
	EXPECT_EQ(first->folder(), folder);
}

TEST(Folder, RemoveElementCommandIgnoresForeignChild)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *child = add_node<olive::Folder>(&project);
	auto *stranger = add_node<olive::Folder>(&project);
	olive::FolderAddChild(folder, child).redo_now();
	ASSERT_EQ(folder->item_child_count(), 1);

	// A node that was never added has no array element, so the command is a
	// no-op rather than an error
	olive::Folder::RemoveElementCommand remove(folder, stranger);
	remove.redo_now();
	EXPECT_EQ(folder->item_child_count(), 1);
	EXPECT_EQ(folder->item_child(0), child);

	remove.undo_now();
	EXPECT_EQ(folder->item_child_count(), 1);
}

TEST(Folder, GetChildWithNameFindsNestedChildren)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *sub = add_node<olive::Folder>(&project);
	sub->set_label(QStringLiteral("Sub"));
	auto *nested = add_node<olive::SolidGenerator>(&project);
	nested->set_label(QStringLiteral("Nested"));

	olive::FolderAddChild(folder, sub).redo_now();
	olive::FolderAddChild(sub, nested).redo_now();

	// Lookup by label recurses into subfolders
	EXPECT_EQ(folder->get_child_with_name(QStringLiteral("Sub")), sub);
	EXPECT_EQ(folder->get_child_with_name(QStringLiteral("Nested")), nested);
	EXPECT_TRUE(folder->child_exists_with_name(QStringLiteral("Nested")));

	EXPECT_EQ(folder->get_child_with_name(QStringLiteral("Missing")), nullptr);
	EXPECT_FALSE(folder->child_exists_with_name(QStringLiteral("Missing")));
}

TEST(Folder, HasChildRecursiveFindsNestedChildren)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *sub = add_node<olive::Folder>(&project);
	auto *nested = add_node<olive::Folder>(&project);
	auto *outsider = add_node<olive::Folder>(&project);

	olive::FolderAddChild(folder, sub).redo_now();
	olive::FolderAddChild(sub, nested).redo_now();

	EXPECT_TRUE(folder->has_child_recursive(sub));
	EXPECT_TRUE(folder->has_child_recursive(nested));
	EXPECT_FALSE(folder->has_child_recursive(outsider));
	EXPECT_FALSE(folder->has_child_recursive(folder));
}

TEST(Folder, ListChildrenOfTypeRecursesIntoSubfolders)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	olive::Folder *folder = project.root();
	auto *sub = add_node<olive::Folder>(&project);
	auto *nested = add_node<olive::Folder>(&project);
	auto *item = add_node<olive::SolidGenerator>(&project);

	olive::FolderAddChild(folder, sub).redo_now();
	olive::FolderAddChild(sub, nested).redo_now();
	olive::FolderAddChild(folder, item).redo_now();

	// Folders are collected recursively while other items are skipped
	const QVector<olive::Folder *> folders =
		folder->list_children_of_type<olive::Folder>();
	ASSERT_EQ(folders.size(), 2);
	EXPECT_TRUE(folders.contains(sub));
	EXPECT_TRUE(folders.contains(nested));
	EXPECT_FALSE(folders.contains(static_cast<olive::Node *>(item)));

	const QVector<olive::SolidGenerator *> solids =
		folder->list_children_of_type<olive::SolidGenerator>();
	ASSERT_EQ(solids.size(), 1);
	EXPECT_EQ(solids.first(), item);
}

TEST(Folder, ChildStructureSurvivesSerializationRoundTrip)
{
	ensure_app_singletons();
	olive::ColorManager::set_up_default_config();
	olive::NodeFactory::initialize();
	// Guard against serializer instances left over by another test
	olive::ProjectSerializer::destroy();
	olive::ProjectSerializer::initialize();

	olive::Project project;
	project.initialize();

	auto *sub = add_node<olive::Folder>(&project);
	sub->set_label(QStringLiteral("Sub"));
	olive::FolderAddChild(project.root(), sub).redo_now();

	auto *nested = add_node<olive::Folder>(&project);
	nested->set_label(QStringLiteral("Nested"));
	olive::FolderAddChild(sub, nested).redo_now();

	olive::ProjectSerializer::SaveData save_data(
		olive::ProjectSerializer::k_project, &project, QString());

	QByteArray xml;
	QBuffer buffer(&xml);
	buffer.open(QIODevice::WriteOnly);
	QXmlStreamWriter writer(&buffer);
	ASSERT_EQ(olive::ProjectSerializer::save(&writer, save_data).code(),
			  olive::ProjectSerializer::k_success);
	buffer.close();

	olive::Project loaded_project;
	QBuffer read_buffer(&xml);
	read_buffer.open(QIODevice::ReadOnly);
	QXmlStreamReader reader(&read_buffer);
	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		&loaded_project, &reader, olive::ProjectSerializer::k_project);
	ASSERT_EQ(result.code(), olive::ProjectSerializer::k_success);

	// The child connections of kChildInput are re-established on load,
	// rebuilding the folder hierarchy
	olive::Folder *loaded_root = loaded_project.root();
	ASSERT_NE(loaded_root, nullptr);
	ASSERT_EQ(loaded_root->item_child_count(), 1);

	olive::Node *loaded_sub = loaded_root->item_child(0);
	EXPECT_EQ(loaded_sub->get_label(), QStringLiteral("Sub"));
	EXPECT_EQ(loaded_sub->folder(), loaded_root);

	auto *loaded_sub_folder = dynamic_cast<olive::Folder *>(loaded_sub);
	ASSERT_NE(loaded_sub_folder, nullptr);
	ASSERT_EQ(loaded_sub_folder->item_child_count(), 1);
	EXPECT_EQ(loaded_sub_folder->item_child(0)->get_label(),
			  QStringLiteral("Nested"));
	EXPECT_EQ(loaded_sub_folder->item_child(0)->folder(), loaded_sub_folder);

	EXPECT_EQ(loaded_root->get_child_with_name(QStringLiteral("Nested")),
			  loaded_sub_folder->item_child(0));
	EXPECT_TRUE(
		loaded_root->has_child_recursive(loaded_sub_folder->item_child(0)));

	olive::ProjectSerializer::destroy();
}

TEST(PolygonGenerator, GenerateFrameRasterizesDefaultPentagon)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);

	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::u8,
									 olive::VideoParams::k_rgba_channel_count);
	olive::FramePtr frame = olive::Frame::create();
	frame->set_video_params(vparams);
	frame->allocate();

	node->generate_frame(frame, olive::GenerateJob(generate_row(node)));

	auto pixel = [&frame](int x, int y) -> const uchar * {
		return reinterpret_cast<const uchar *>(frame->data()) +
			   y * frame->linesize_bytes() +
			   x * olive::VideoParams::k_rgba_channel_count;
	};

	// The pentagon is filled white: the frame center is well inside it
	const uchar *center = pixel(160, 120);
	EXPECT_EQ(int(center[0]), 255);
	EXPECT_EQ(int(center[1]), 255);
	EXPECT_EQ(int(center[2]), 255);
	EXPECT_EQ(int(center[3]), 255);

	const uchar *lower = pixel(160, 200);
	EXPECT_EQ(int(lower[3]), 255);

	// The corners are outside the pentagon and stay transparent
	for (int y : { 0, 239 }) {
		for (int x : { 0, 319 }) {
			const uchar *corner = pixel(x, y);
			EXPECT_EQ(int(corner[3]), 0) << "corner " << x << ", " << y;
		}
	}
}

TEST(PolygonGenerator, UpdateGizmoPositionsCreatesHandlesForEachPoint)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);

	// Only the path gizmo exists before the first update
	ASSERT_EQ(node->get_gizmos().size(), 1);

	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::f32,
									 olive::VideoParams::k_rgba_channel_count);
	const olive::NodeGlobals globals(vparams, olive::core::AudioParams(),
									 olive::Rational(0),
									 olive::LoopMode::k_loop_mode_off);

	node->update_gizmo_positions(generate_row(node), globals);

	// Path gizmo + one position handle, two bezier handles and two bezier
	// lines per point of the default pentagon
	ASSERT_EQ(node->get_gizmos().size(), 1 + 5 + 10 + 10);

	// Without a base texture the gizmos are anchored at half the sequence
	// resolution on top of each point
	const double expected[5][2] = {
		{ 0, -135 }, { 135, -45 }, { 90, 120 }, { -90, 120 }, { -135, -45 }
	};
	for (int i = 0; i < 5; i++) {
		auto *position =
			static_cast<olive::PointGizmo *>(node->get_gizmos().at(1 + i));
		EXPECT_EQ(position->get_point(),
				  QPointF(expected[i][0] + 160, expected[i][1] + 120))
			<< "Wrong position handle for point " << i;
	}

	// Bezier handles default to the point position (zero control point offsets)
	auto *bezier = static_cast<olive::PointGizmo *>(node->get_gizmos().at(6));
	EXPECT_EQ(int(bezier->get_shape()), int(olive::PointGizmo::k_circle));
	EXPECT_EQ(bezier->get_point(), QPointF(160, -15));

	auto *line = static_cast<olive::LineGizmo *>(node->get_gizmos().at(16));
	EXPECT_EQ(line->get_line(), QLineF(QPointF(160, -15), QPointF(160, -15)));

	auto *path = dynamic_cast<olive::PathGizmo *>(node->get_gizmos().first());
	ASSERT_NE(path, nullptr);
	// moveTo plus one cubic segment (3 elements) per edge of the pentagon
	EXPECT_EQ(path->get_path().elementCount(), 16);

	// Shrinking the point array shrinks the gizmo vectors with it
	node->input_array_resize(olive::PolygonGenerator::k_points_input, 3);
	node->update_gizmo_positions(generate_row(node), globals);
	EXPECT_EQ(node->get_gizmos().size(), 1 + 3 + 6 + 6);

	// With a base texture connected the gizmos anchor at half the texture's
	// virtual resolution instead of the sequence's
	const olive::TexturePtr base = std::make_shared<olive::Texture>(
		olive::VideoParams(64, 48, olive::core::PixelFormat::u8,
						   olive::VideoParams::k_rgba_channel_count));
	olive::NodeValueRow row = generate_row(node);
	row.insert(olive::GeneratorWithMerge::k_base_input,
			   olive::NodeValue(olive::NodeValue::k_texture, base));
	node->update_gizmo_positions(row, globals);

	auto *position =
		static_cast<olive::PointGizmo *>(node->get_gizmos().at(1));
	EXPECT_EQ(position->get_point(), QPointF(32, -111));
}

TEST(PolygonGenerator, DraggingPositionGizmoUpdatesPointTracks)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::PolygonGenerator>(&project);

	const olive::NodeValueRow row = generate_row(node);
	node->update_gizmo_positions(row, olive::NodeGlobals());
	ASSERT_EQ(node->get_gizmos().size(), 26);

	// The first position handle drives the X/Y tracks of the first point
	auto *gizmo = static_cast<olive::PointGizmo *>(node->get_gizmos().at(1));
	gizmo->drag_start(row, 0, 0, olive::Rational(0));
	gizmo->drag_move(10, -20, Qt::NoModifier);

	EXPECT_DOUBLE_EQ(node->get_split_standard_value_on_track(
						 olive::PolygonGenerator::k_points_input, 0, 0)
						 .toDouble(),
					 10.0);
	EXPECT_DOUBLE_EQ(node->get_split_standard_value_on_track(
						 olive::PolygonGenerator::k_points_input, 1, 0)
						 .toDouble(),
					 -155.0);

	// The other points are untouched
	EXPECT_DOUBLE_EQ(node->get_split_standard_value_on_track(
						 olive::PolygonGenerator::k_points_input, 0, 1)
						 .toDouble(),
					 135.0);

	olive::MultiUndoCommand command;
	gizmo->drag_end(&command);
}

TEST(TextGeneratorV2, MetadataIsCorrect)
{
	olive::TextGeneratorV2 node;
	EXPECT_EQ(node.id(), QStringLiteral("org.olivevideoeditor.Olive.text2"));
	EXPECT_EQ(node.name(), QStringLiteral("Text (Legacy)"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));

	// Hidden from the create menu: superseded by TextGeneratorV3
	EXPECT_TRUE(node.get_flags() & olive::Node::k_dont_show_in_create_menu);
}

TEST(TextGeneratorV2, InputDefaults)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);

	EXPECT_EQ(node->get_standard_value(olive::TextGeneratorV2::k_text_input)
				  .toString(),
			  QStringLiteral("Sample Text"));

	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV2::k_html_input)),
			  int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node->get_standard_value(olive::TextGeneratorV2::k_html_input)
					 .toBool());

	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV2::k_v_align_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node->get_standard_value(olive::TextGeneratorV2::k_v_align_input)
				  .toInt(),
			  0);

	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV2::k_font_input)),
			  int(olive::NodeValue::k_font));

	EXPECT_EQ(
		int(node->get_input_data_type(olive::TextGeneratorV2::k_font_size_input)),
		int(olive::NodeValue::k_float));
	EXPECT_DOUBLE_EQ(node->get_standard_value(olive::TextGeneratorV2::k_font_size_input)
						 .toDouble(),
					 72.0);

	// From ShapeNodeBase: white text on a 400x300 box
	const olive::core::Color color =
		node->get_standard_value(olive::ShapeNodeBase::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);
	EXPECT_EQ(node->get_standard_value(olive::ShapeNodeBase::k_size_input)
				  .value<QVector2D>(),
			  QVector2D(400.0f, 300.0f));
}

TEST(TextGeneratorV2, RetranslateSetsNamesAndComboStrings)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV2::k_text_input),
			  QStringLiteral("Text"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV2::k_html_input),
			  QStringLiteral("Enable HTML"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV2::k_font_input),
			  QStringLiteral("Font"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV2::k_font_size_input),
			  QStringLiteral("Font Size"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV2::k_v_align_input),
			  QStringLiteral("Vertical Align"));

	// Inherited names from ShapeNodeBase and GeneratorWithMerge
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_position_input),
			  QStringLiteral("Position"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_size_input),
			  QStringLiteral("Size"));
	EXPECT_EQ(node->get_input_name(olive::ShapeNodeBase::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node->get_input_name(olive::GeneratorWithMerge::k_base_input),
			  QStringLiteral("Base"));

	const QStringList aligns =
		node->get_combo_box_strings(olive::TextGeneratorV2::k_v_align_input);
	ASSERT_EQ(aligns.size(), 3);
	EXPECT_EQ(aligns.at(0), QStringLiteral("Top"));
	EXPECT_EQ(aligns.at(1), QStringLiteral("Center"));
	EXPECT_EQ(aligns.at(2), QStringLiteral("Bottom"));
}

TEST(TextGeneratorV2, ValuePushesFloatTextureWithGenerateJob)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);

	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::u8,
									 olive::VideoParams::k_rgba_channel_count);
	olive::NodeValueTable table = generate_table(node, vparams);

	// Text always renders to a 32-bit float buffer regardless of sequence depth
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(texture->params().height(), vparams.height());
	EXPECT_EQ(int(texture->params().format()),
			  int(olive::core::PixelFormat::f32));

	auto *job = dynamic_cast<olive::GenerateJob *>(texture->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::TextGeneratorV2::k_text_input).to_string(),
			  QStringLiteral("Sample Text"));
	EXPECT_DOUBLE_EQ(job->get(olive::TextGeneratorV2::k_font_size_input).to_double(),
					 72.0);
	EXPECT_EQ(job->get(olive::TextGeneratorV2::k_v_align_input).to_int(), 0);
	EXPECT_EQ(job->get(olive::ShapeNodeBase::k_size_input).to_vec2(),
			  QVector2D(400.0f, 300.0f));
}

TEST(TextGeneratorV2, EmptyTextPushesNothing)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);
	node->set_standard_value(olive::TextGeneratorV2::k_text_input, QString());

	olive::NodeValueTable table = generate_table(
		node, olive::VideoParams(320, 240, olive::core::PixelFormat::u8,
								 olive::VideoParams::k_rgba_channel_count));

	EXPECT_TRUE(get_output_texture(table) == nullptr);
}

TEST(TextGeneratorV2, ValueIgnoresBaseInput)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);
	auto *constant = add_node<ConstantTextureNode>(&project);

	const olive::TexturePtr base = std::make_shared<olive::Texture>(
		olive::VideoParams(64, 48, olive::core::PixelFormat::u8,
						   olive::VideoParams::k_rgba_channel_count));
	constant->set_texture(base);
	olive::Node::connect_edge(constant,
							 olive::NodeInput(
								 node, olive::GeneratorWithMerge::k_base_input));

	olive::NodeValueTable table = generate_table(
		node, olive::VideoParams(320, 240, olive::core::PixelFormat::f32,
								 olive::VideoParams::k_rgba_channel_count));

	// Unlike TextGeneratorV3, which composites its text over the base input,
	// the legacy V2 node never looks at it: the output is its own generate
	// job at sequence params, not a merge with the base
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), 320);
	EXPECT_EQ(texture->params().height(), 240);
	EXPECT_TRUE(dynamic_cast<olive::GenerateJob *>(texture->job()));
}

TEST(TextGeneratorV2, GenerateFrameWithEmptyTextLeavesFrameTransparent)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);
	node->set_standard_value(olive::TextGeneratorV2::k_text_input, QString());

	// Walk the vertical alignment switch and the HTML branch with empty text:
	// no glyphs are drawn, so the transplant loop writes pure zeros
	olive::NodeValueRow row = generate_row(node);
	for (int valign = 0; valign <= 2; valign++) {
		for (int html = 0; html <= 1; html++) {
			row[olive::TextGeneratorV2::k_v_align_input] =
				olive::NodeValue(olive::NodeValue::k_combo, valign);
			row[olive::TextGeneratorV2::k_html_input] =
				olive::NodeValue(olive::NodeValue::k_boolean, bool(html));

			olive::FramePtr frame = olive::Frame::create();
			frame->set_video_params(
				olive::VideoParams(64, 48, olive::core::PixelFormat::f32,
								   olive::VideoParams::k_rgba_channel_count));
			frame->allocate();

			node->generate_frame(frame, olive::GenerateJob(row));

			const float *data = reinterpret_cast<const float *>(frame->data());
			const int pixel_count =
				frame->linesize_pixels() * frame->height() *
				olive::VideoParams::k_rgba_channel_count;
			float max_abs = 0.0f;
			for (int i = 0; i < pixel_count; i++) {
				max_abs = qMax(max_abs, qAbs(data[i]));
			}
			EXPECT_FLOAT_EQ(max_abs, 0.0f)
				<< "valign " << valign << ", html " << html;
		}
	}
}

TEST(TextGeneratorV2, GenerateFrameRasterizesTextInColor)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV2>(&project);
	node->set_standard_value(
		olive::ShapeNodeBase::k_color_input,
		QVariant::fromValue(olive::core::Color(1.0f, 0.0f, 0.0f, 1.0f)));

	olive::FramePtr frame = olive::Frame::create();
	frame->set_video_params(
		olive::VideoParams(320, 240, olive::core::PixelFormat::f32,
						   olive::VideoParams::k_rgba_channel_count));
	frame->allocate();

	node->generate_frame(frame, olive::GenerateJob(generate_row(node)));

	// The alpha mask of the rendered glyphs is tinted by the color input:
	// red premultiplied text has red == alpha and zero green/blue everywhere
	const float *data = reinterpret_cast<const float *>(frame->data());
	bool any_alpha = false;
	bool any_green_or_blue = false;
	bool red_matches_alpha = true;
	for (int y = 0; y < frame->height(); y++) {
		for (int x = 0; x < frame->width(); x++) {
			const float *px = data +
							  (y * frame->linesize_pixels() + x) *
								  olive::VideoParams::k_rgba_channel_count;
			any_alpha |= px[3] > 0.0f;
			any_green_or_blue |= (px[1] != 0.0f || px[2] != 0.0f);
			red_matches_alpha &= (px[0] == px[3]);
		}
	}
	EXPECT_TRUE(any_alpha);
	EXPECT_FALSE(any_green_or_blue);
	EXPECT_TRUE(red_matches_alpha);
}

TEST(TextGeneratorV1, MetadataIsCorrect)
{
	olive::TextGeneratorV1 node;
	EXPECT_EQ(node.id(),
			  QStringLiteral("org.olivevideoeditor.Olive.textgenerator"));
	EXPECT_EQ(node.name(), QStringLiteral("Text (Legacy)"));
	EXPECT_FALSE(node.description().isEmpty());
	EXPECT_TRUE(node.category().contains(olive::Node::k_category_generator));

	// Hidden from the create menu: superseded by TextGeneratorV3
	EXPECT_TRUE(node.get_flags() & olive::Node::k_dont_show_in_create_menu);
}

TEST(TextGeneratorV1, InputDefaults)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);

	EXPECT_EQ(node->get_standard_value(olive::TextGeneratorV1::k_text_input)
				  .toString(),
			  QStringLiteral("Sample Text"));

	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV1::k_html_input)),
			  int(olive::NodeValue::k_boolean));
	EXPECT_FALSE(node->get_standard_value(olive::TextGeneratorV1::k_html_input)
					 .toBool());

	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV1::k_color_input)),
			  int(olive::NodeValue::k_color));
	const olive::core::Color color =
		node->get_standard_value(olive::TextGeneratorV1::k_color_input)
			.value<olive::core::Color>();
	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_FLOAT_EQ(color.green(), 1.0f);
	EXPECT_FLOAT_EQ(color.blue(), 1.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 1.0f);

	// Unlike V2, V1 defaults to centered vertical alignment
	EXPECT_EQ(int(node->get_input_data_type(olive::TextGeneratorV1::k_v_align_input)),
			  int(olive::NodeValue::k_combo));
	EXPECT_EQ(node->get_standard_value(olive::TextGeneratorV1::k_v_align_input)
				  .toInt(),
			  1);

	EXPECT_EQ(
		int(node->get_input_data_type(olive::TextGeneratorV1::k_font_size_input)),
		int(olive::NodeValue::k_float));
	EXPECT_DOUBLE_EQ(node->get_standard_value(olive::TextGeneratorV1::k_font_size_input)
						 .toDouble(),
					 72.0);
}

TEST(TextGeneratorV1, RetranslateSetsNamesAndComboStrings)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);
	node->retranslate();

	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_text_input),
			  QStringLiteral("Text"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_html_input),
			  QStringLiteral("Enable HTML"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_font_input),
			  QStringLiteral("Font"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_font_size_input),
			  QStringLiteral("Font Size"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_color_input),
			  QStringLiteral("Color"));
	EXPECT_EQ(node->get_input_name(olive::TextGeneratorV1::k_v_align_input),
			  QStringLiteral("Vertical Align"));

	const QStringList aligns =
		node->get_combo_box_strings(olive::TextGeneratorV1::k_v_align_input);
	ASSERT_EQ(aligns.size(), 3);
	EXPECT_EQ(aligns.at(0), QStringLiteral("Top"));
	EXPECT_EQ(aligns.at(1), QStringLiteral("Center"));
	EXPECT_EQ(aligns.at(2), QStringLiteral("Bottom"));
}

TEST(TextGeneratorV1, ValuePushesTextureAtSequenceParams)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);

	const olive::VideoParams vparams(320, 240, olive::core::PixelFormat::u8,
									 olive::VideoParams::k_rgba_channel_count);
	olive::NodeValueTable table = generate_table(node, vparams);

	// Unlike V2, V1 keeps the sequence pixel format for its output
	olive::TexturePtr texture = get_output_texture(table);
	ASSERT_TRUE(texture);
	ASSERT_TRUE(texture->is_job());
	EXPECT_EQ(texture->params().width(), vparams.width());
	EXPECT_EQ(texture->params().height(), vparams.height());
	EXPECT_EQ(int(texture->params().format()),
			  int(olive::core::PixelFormat::u8));

	auto *job = dynamic_cast<olive::GenerateJob *>(texture->job());
	ASSERT_TRUE(job);
	EXPECT_EQ(job->get(olive::TextGeneratorV1::k_text_input).to_string(),
			  QStringLiteral("Sample Text"));
	EXPECT_DOUBLE_EQ(job->get(olive::TextGeneratorV1::k_font_size_input).to_double(),
					 72.0);
}

TEST(TextGeneratorV1, EmptyTextPushesNothing)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);
	node->set_standard_value(olive::TextGeneratorV1::k_text_input, QString());

	olive::NodeValueTable table = generate_table(
		node, olive::VideoParams(320, 240, olive::core::PixelFormat::u8,
								 olive::VideoParams::k_rgba_channel_count));

	EXPECT_TRUE(get_output_texture(table) == nullptr);
}

TEST(TextGeneratorV1, GenerateFrameWithEmptyTextLeavesFrameBlack)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);
	node->set_standard_value(olive::TextGeneratorV1::k_text_input, QString());

	// Walk the vertical alignment switch and the HTML branch with empty text:
	// no glyphs are drawn, so every pixel is set to transparent black
	olive::NodeValueRow row = generate_row(node);
	for (int valign = 0; valign <= 2; valign++) {
		for (int html = 0; html <= 1; html++) {
			row[olive::TextGeneratorV1::k_v_align_input] =
				olive::NodeValue(olive::NodeValue::k_combo, valign);
			row[olive::TextGeneratorV1::k_html_input] =
				olive::NodeValue(olive::NodeValue::k_boolean, bool(html));

			olive::FramePtr frame = olive::Frame::create();
			frame->set_video_params(
				olive::VideoParams(64, 48, olive::core::PixelFormat::f32,
								   olive::VideoParams::k_rgba_channel_count));
			frame->allocate();

			node->generate_frame(frame, olive::GenerateJob(row));

			const float *data = reinterpret_cast<const float *>(frame->data());
			const int pixel_count =
				frame->linesize_pixels() * frame->height() *
				olive::VideoParams::k_rgba_channel_count;
			float max_abs = 0.0f;
			for (int i = 0; i < pixel_count; i++) {
				max_abs = qMax(max_abs, qAbs(data[i]));
			}
			EXPECT_FLOAT_EQ(max_abs, 0.0f)
				<< "valign " << valign << ", html " << html;
		}
	}
}

TEST(TextGeneratorV1, GenerateFrameRasterizesTextPixels)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = add_node<olive::TextGeneratorV1>(&project);

	olive::FramePtr frame = olive::Frame::create();
	frame->set_video_params(
		olive::VideoParams(320, 240, olive::core::PixelFormat::f32,
						   olive::VideoParams::k_rgba_channel_count));
	frame->allocate();

	node->generate_frame(frame, olive::GenerateJob(generate_row(node)));

	// The default white text is written premultiplied: any covered pixel has
	// all channels equal to its alpha
	const float *data = reinterpret_cast<const float *>(frame->data());
	bool any_alpha = false;
	bool channels_match_alpha = true;
	for (int y = 0; y < frame->height(); y++) {
		for (int x = 0; x < frame->width(); x++) {
			const float *px = data +
							  (y * frame->linesize_pixels() + x) *
								  olive::VideoParams::k_rgba_channel_count;
			any_alpha |= px[3] > 0.0f;
			channels_match_alpha &=
				(px[0] == px[3] && px[1] == px[3] && px[2] == px[3]);
		}
	}
	EXPECT_TRUE(any_alpha);
	EXPECT_TRUE(channels_match_alpha);
}
