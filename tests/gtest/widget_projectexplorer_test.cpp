#include <gtest/gtest.h>

#include <memory>

#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/output/track/track.h"
#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/app.h"
#include "render/diskmanager.h"
#include "undo/undostack.h"
#include "widget/projectexplorer/projectexplorer.h"
#include "widget/projectexplorer/projectviewmodel.h"
#include "widget/projecttoolbar/projecttoolbar.h"

using namespace olive;

namespace
{

// Renames and moves go through the global undo stack hosted by Core
void ensure_app_singletons()
{
	if (!olive::Core::instance()) {
		new olive::Core(); // intentionally leaked
	}
	if (!olive::DiskManager::instance()) {
		olive::DiskManager::create_instance();
	}
}

// Helpers: bridge engine pointers to the oak:: wrapper layer used by the
// app interface
inline oak::Node to_oak(Node *n)
{
	return oak::Node(reinterpret_cast<OakEngineNode *>(n));
}

inline oak::Project to_oak_project(Project *p)
{
	return oak::Project(reinterpret_cast<OakEngineProject *>(p));
}

// The process-wide undo stack previously reached via Core::undo_stack()
inline UndoStack *app_undo_stack()
{
	return static_cast<UndoStack *>(oakengine_app_undo_stack());
}

} // namespace

class ProjectViewModelTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();

		model_.set_project(to_oak_project(project_.get()));
	}

	template <typename T> T *add_item(Folder *parent)
	{
		auto *node = new T();
		node->setParent(project_.get());
		FolderAddChild(parent, node).redo_now();
		return node;
	}

	std::unique_ptr<Project> project_;
	ProjectViewModel model_{ nullptr };
};

TEST_F(ProjectViewModelTest, ModelWithoutProjectIsEmpty)
{
	ProjectViewModel empty(nullptr);
	EXPECT_EQ(empty.rowCount(), 0);
	EXPECT_EQ(empty.columnCount(), 0);
	EXPECT_EQ(empty.project(), nullptr);
}

TEST_F(ProjectViewModelTest, HierarchyIndexesAndParents)
{
	Folder *folder = add_item<Folder>(project_->root());
	Footage *footage = add_item<Footage>(project_->root());
	Sequence *sequence = add_item<Sequence>(project_->root());
	Footage *nested = add_item<Footage>(folder);

	ASSERT_EQ(model_.rowCount(), 3);
	EXPECT_EQ(model_.columnCount(), ProjectViewModel::k_column_count);

	// Root children appear in insertion order
	EXPECT_EQ(model_.index(0, 0).internalPointer(), reinterpret_cast<OakEngineNode *>(folder));
	EXPECT_EQ(model_.index(1, 0).internalPointer(), reinterpret_cast<OakEngineNode *>(footage));
	EXPECT_EQ(model_.index(2, 0).internalPointer(), reinterpret_cast<OakEngineNode *>(sequence));

	// Children of the root report an invalid parent index
	EXPECT_EQ(model_.parent(model_.index(0, 0)), QModelIndex());

	// Nested items resolve to their folder
	EXPECT_EQ(model_.rowCount(model_.index(0, 0)), 1);
	QModelIndex nested_index = model_.index(0, 0, model_.index(0, 0));
	EXPECT_EQ(nested_index.internalPointer(), reinterpret_cast<OakEngineNode *>(nested));
	EXPECT_EQ(model_.parent(nested_index), model_.index(0, 0));

	// CreateIndexFromItem round-trips through the same object
	EXPECT_EQ(model_.create_index_from_item(to_oak(footage)), model_.index(1, 0));
	EXPECT_EQ(model_.create_index_from_item(to_oak(nested)).internalPointer(), reinterpret_cast<OakEngineNode *>(nested));

	// Only folders report children, even when empty
	EXPECT_TRUE(model_.hasChildren(model_.index(0, 0)));
	EXPECT_FALSE(model_.hasChildren(model_.index(1, 0)));
}

TEST_F(ProjectViewModelTest, ItemInsertAndRemoveEmitModelSignals)
{
	QSignalSpy about_to_insert(&model_, &QAbstractItemModel::rowsAboutToBeInserted);
	QSignalSpy inserted(&model_, &QAbstractItemModel::rowsInserted);
	QSignalSpy about_to_remove(&model_, &QAbstractItemModel::rowsAboutToBeRemoved);
	QSignalSpy removed(&model_, &QAbstractItemModel::rowsRemoved);

	Footage *footage = add_item<Footage>(project_->root());
	EXPECT_EQ(about_to_insert.count(), 1);
	EXPECT_EQ(inserted.count(), 1);
	EXPECT_EQ(model_.rowCount(), 1);

	// Deleting the node disconnects the folder edge and removes the row
	delete footage;
	EXPECT_EQ(about_to_remove.count(), 1);
	EXPECT_EQ(removed.count(), 1);
	EXPECT_EQ(model_.rowCount(), 0);
}

TEST_F(ProjectViewModelTest, DataColumnsAndHeader)
{
	Folder *folder = add_item<Folder>(project_->root());
	folder->set_label(QStringLiteral("Media"));

	QModelIndex name_index = model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_name);
	EXPECT_EQ(model_.data(name_index, Qt::DisplayRole).toString(),
			  QStringLiteral("Media"));
	EXPECT_EQ(model_.data(name_index, Qt::EditRole).toString(),
			  QStringLiteral("Media"));
	EXPECT_EQ(model_.data(name_index, ProjectViewModel::k_inner_text_role).toString(),
			  QStringLiteral("Media"));

	// A folder carries no duration/rate/timestamps
	EXPECT_FALSE(model_.data(model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_duration),
							 Qt::DisplayRole)
					 .isValid());
	EXPECT_FALSE(model_.data(model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_rate),
							 Qt::DisplayRole)
					 .isValid());

	// EditRole is only served for the name column
	EXPECT_FALSE(model_.data(model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_duration),
							 Qt::EditRole)
					 .isValid());

	EXPECT_EQ(model_.headerData(ProjectViewModel::k_name, Qt::Horizontal).toString(),
			  QStringLiteral("Name"));
	EXPECT_EQ(model_.headerData(ProjectViewModel::k_duration, Qt::Horizontal).toString(),
			  QStringLiteral("Duration"));
	EXPECT_EQ(model_.headerData(ProjectViewModel::k_rate, Qt::Horizontal).toString(),
			  QStringLiteral("Rate"));
	EXPECT_EQ(model_.headerData(ProjectViewModel::k_last_modified, Qt::Horizontal).toString(),
			  QStringLiteral("Modified"));
	EXPECT_EQ(model_.headerData(ProjectViewModel::k_created_time, Qt::Horizontal).toString(),
			  QStringLiteral("Created"));
}

TEST_F(ProjectViewModelTest, FlagsMarkNameEditableAndFoldersDroppable)
{
	Folder *folder = add_item<Folder>(project_->root());
	Footage *footage = add_item<Footage>(project_->root());

	const Qt::ItemFlags folder_name_flags =
		model_.flags(model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_name));
	EXPECT_TRUE(folder_name_flags & Qt::ItemIsEditable);
	EXPECT_TRUE(folder_name_flags & Qt::ItemIsDragEnabled);
	EXPECT_TRUE(folder_name_flags & Qt::ItemIsDropEnabled);

	// Non-name columns are not editable, non-folders do not accept drops
	const Qt::ItemFlags footage_duration_flags =
		model_.flags(model_.create_index_from_item(to_oak(footage), ProjectViewModel::k_duration));
	EXPECT_FALSE(footage_duration_flags & Qt::ItemIsEditable);
	EXPECT_FALSE(footage_duration_flags & Qt::ItemIsDropEnabled);

	// The background accepts external file drops
	EXPECT_EQ(model_.flags(QModelIndex()), Qt::ItemIsDropEnabled);
}

TEST_F(ProjectViewModelTest, SetDataRenamesItemThroughUndoStack)
{
	Folder *folder = add_item<Folder>(project_->root());
	folder->set_label(QStringLiteral("Before"));

	QSignalSpy data_changed(&model_, &QAbstractItemModel::dataChanged);

	QModelIndex name_index = model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_name);
	EXPECT_TRUE(model_.setData(name_index, QStringLiteral("After"), Qt::EditRole));
	EXPECT_EQ(folder->get_label(), QStringLiteral("After"));
	EXPECT_GE(data_changed.count(), 1);

	// The rename is a regular undo command
	app_undo_stack()->undo();
	EXPECT_EQ(folder->get_label(), QStringLiteral("Before"));
	app_undo_stack()->clear();

	// Empty names and other columns are rejected
	EXPECT_FALSE(model_.setData(name_index, QString(), Qt::EditRole));
	EXPECT_FALSE(model_.setData(model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_rate),
								QStringLiteral("After"), Qt::EditRole));
	EXPECT_EQ(folder->get_label(), QStringLiteral("Before"));
}

TEST_F(ProjectViewModelTest, MimeDataEncodesEachRowOnce)
{
	EXPECT_EQ(model_.mimeTypes(),
			  (QStringList{ Project::k_item_mime_type, QStringLiteral("text/uri-list") }));

	Footage *footage = add_item<Footage>(project_->root());
	Folder *folder = add_item<Folder>(project_->root());

	// Passing every column of two rows must still encode only two items
	QModelIndexList indexes{ model_.create_index_from_item(to_oak(footage), ProjectViewModel::k_name),
							 model_.create_index_from_item(to_oak(footage), ProjectViewModel::k_duration),
							 model_.create_index_from_item(to_oak(folder), ProjectViewModel::k_name) };
	std::unique_ptr<QMimeData> mime(model_.mimeData(indexes));
	ASSERT_NE(mime, nullptr);
	ASSERT_TRUE(mime->hasFormat(Project::k_item_mime_type));

	QByteArray encoded = mime->data(Project::k_item_mime_type);
	QDataStream stream(&encoded, QIODevice::ReadOnly);

	// Wire format: stream count (qint64: the writer streams a qsizetype),
	// (type, index) pairs, then the node pointer
	auto read_item = [&stream]() -> Node * {
		qint64 stream_count = 0;
		stream >> stream_count;
		for (qint64 i = 0; i < stream_count; i++) {
			int type, index;
			stream >> type >> index;
		}
		quintptr ptr = 0;
		stream >> ptr;
		return reinterpret_cast<Node *>(ptr);
	};

	EXPECT_EQ(read_item(), footage);
	EXPECT_EQ(read_item(), folder);

	EXPECT_TRUE(stream.atEnd());

	// An empty selection produces no mime data
	EXPECT_EQ(model_.mimeData(QModelIndexList()), nullptr);
}

// Regression note: the app's mimeData() writer streams the enabled-stream
// count as a qsizetype (qint64, 8 bytes); dropMimeData() used to read it
// back as int (4 bytes), desynchronizing the stream. Fixed app-side.
TEST_F(ProjectViewModelTest, DropMimeDataMovesItemIntoFolder)
{
	Folder *folder = add_item<Folder>(project_->root());
	Footage *footage = add_item<Footage>(project_->root());
	ASSERT_EQ(model_.rowCount(), 2);

	std::unique_ptr<QMimeData> mime(
		model_.mimeData({ model_.create_index_from_item(to_oak(footage)) }));
	ASSERT_NE(mime, nullptr);

	EXPECT_TRUE(model_.dropMimeData(mime.get(), Qt::CopyAction, -1, -1,
									model_.create_index_from_item(to_oak(folder))));
	EXPECT_EQ(footage->folder(), folder);
	EXPECT_EQ(model_.rowCount(), 1);
	EXPECT_EQ(model_.rowCount(model_.create_index_from_item(to_oak(folder))), 1);

	// The move is undoable
	app_undo_stack()->undo();
	EXPECT_EQ(footage->folder(), project_->root());
	EXPECT_EQ(model_.rowCount(), 2);
	app_undo_stack()->clear();
}

// DISABLED: same app-side dropMimeData stream-count desync as above
TEST_F(ProjectViewModelTest, DropRejectsNonFolderAndSelfNesting)
{
	Folder *folder = add_item<Folder>(project_->root());
	Folder *subfolder = add_item<Folder>(folder);
	Footage *footage = add_item<Footage>(project_->root());

	// Cannot drop onto a non-folder item
	std::unique_ptr<QMimeData> footage_mime(
		model_.mimeData({ model_.create_index_from_item(to_oak(footage)) }));
	EXPECT_FALSE(model_.dropMimeData(footage_mime.get(), Qt::CopyAction, -1, -1,
									 model_.create_index_from_item(to_oak(footage))));
	EXPECT_EQ(footage->folder(), project_->root());

	// Dropping a folder into its own descendant is skipped as a no-op
	std::unique_ptr<QMimeData> folder_mime(
		model_.mimeData({ model_.create_index_from_item(to_oak(folder)) }));
	EXPECT_TRUE(model_.dropMimeData(folder_mime.get(), Qt::CopyAction, -1, -1,
									model_.create_index_from_item(to_oak(subfolder))));
	EXPECT_EQ(folder->folder(), project_->root());

	// Dropping onto the background moves items to the root
	std::unique_ptr<QMimeData> sub_mime(
		model_.mimeData({ model_.create_index_from_item(to_oak(subfolder)) }));
	EXPECT_TRUE(model_.dropMimeData(sub_mime.get(), Qt::CopyAction, -1, -1,
									QModelIndex()));
	EXPECT_EQ(subfolder->folder(), project_->root());
	app_undo_stack()->clear();
}

class ProjectExplorerTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	template <typename T> T *add_item(Folder *parent)
	{
		auto *node = new T();
		node->setParent(project_.get());
		FolderAddChild(parent, node).redo_now();
		return node;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(ProjectExplorerTest, SetProjectAndSwitchViewType)
{
	ProjectExplorer explorer(nullptr);
	EXPECT_EQ(explorer.project(), nullptr);

	explorer.set_project(to_oak_project(project_.get()));
	EXPECT_EQ(explorer.project(), to_oak_project(project_.get()));

	// Tree view is the default
	EXPECT_EQ(explorer.view_type(), ProjectToolbar::tree_view);

	explorer.set_view_type(ProjectToolbar::list_view);
	EXPECT_EQ(explorer.view_type(), ProjectToolbar::list_view);

	explorer.set_view_type(ProjectToolbar::icon_view);
	EXPECT_EQ(explorer.view_type(), ProjectToolbar::icon_view);
}

TEST_F(ProjectExplorerTest, GetSelectedFolderFallsBackToRoot)
{
	Folder *folder = add_item<Folder>(project_->root());
	Footage *footage = add_item<Footage>(project_->root());

	ProjectExplorer explorer(nullptr);
	explorer.set_project(to_oak_project(project_.get()));

	// No selection: heuristic returns the project root
	EXPECT_EQ(explorer.get_selected_folder(), to_oak(project_->root()));

	// A selected folder is returned directly
	EXPECT_TRUE(explorer.select_item(to_oak(folder)));
	EXPECT_EQ(explorer.get_selected_folder(), to_oak(folder));

	// A selected non-folder resolves to its parent folder
	EXPECT_TRUE(explorer.select_item(to_oak(footage)));
	EXPECT_EQ(explorer.get_selected_folder(), to_oak(project_->root()));
}

TEST_F(ProjectExplorerTest, SelectItemUpdatesSelectedItems)
{
	Footage *footage = add_item<Footage>(project_->root());

	ProjectExplorer explorer(nullptr);
	explorer.set_project(to_oak_project(project_.get()));

	EXPECT_TRUE(explorer.selected_items().isEmpty());

	EXPECT_TRUE(explorer.select_item(to_oak(footage)));
	EXPECT_EQ(explorer.selected_items().size(), 1);
	EXPECT_EQ(explorer.selected_items().first(), to_oak(footage));

	explorer.deselect_all();
	EXPECT_TRUE(explorer.selected_items().isEmpty());
}

class ProjectToolbarTest : public ::testing::Test {
};

TEST_F(ProjectToolbarTest, ActionButtonsEmitSignals)
{
	ProjectToolbar toolbar(nullptr);

	// Buttons in creation order: new, open, save, tree, list, icon
	const QList<QPushButton *> buttons = toolbar.findChildren<QPushButton *>();
	ASSERT_EQ(buttons.size(), 6);

	QSignalSpy new_spy(&toolbar, &ProjectToolbar::new_clicked);
	QSignalSpy open_spy(&toolbar, &ProjectToolbar::open_clicked);
	QSignalSpy save_spy(&toolbar, &ProjectToolbar::save_clicked);

	buttons.at(0)->click();
	EXPECT_EQ(new_spy.count(), 1);

	buttons.at(1)->click();
	EXPECT_EQ(open_spy.count(), 1);

	buttons.at(2)->click();
	EXPECT_EQ(save_spy.count(), 1);
}

TEST_F(ProjectToolbarTest, SearchFieldForwardsTextChanges)
{
	ProjectToolbar toolbar(nullptr);

	auto *search = toolbar.findChild<QLineEdit *>();
	ASSERT_NE(search, nullptr);

	QSignalSpy search_spy(&toolbar, &ProjectToolbar::search_changed);

	search->setText(QStringLiteral("media"));
	ASSERT_EQ(search_spy.count(), 1);
	EXPECT_EQ(search_spy.first().first().toString(), QStringLiteral("media"));
}

TEST_F(ProjectToolbarTest, ViewButtonsAreExclusiveAndEmitViewChanged)
{
	ProjectToolbar toolbar(nullptr);
	const QList<QPushButton *> buttons = toolbar.findChildren<QPushButton *>();
	ASSERT_EQ(buttons.size(), 6);

	QPushButton *tree_button = buttons.at(3);
	QPushButton *list_button = buttons.at(4);
	QPushButton *icon_button = buttons.at(5);

	ProjectToolbar::ViewType received = ProjectToolbar::tree_view;
	int emissions = 0;
	QObject::connect(&toolbar, &ProjectToolbar::view_changed,
					 [&received, &emissions](ProjectToolbar::ViewType type) {
						 received = type;
						 ++emissions;
					 });

	list_button->click();
	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(received, ProjectToolbar::list_view);
	EXPECT_TRUE(list_button->isChecked());
	EXPECT_FALSE(tree_button->isChecked());
	EXPECT_FALSE(icon_button->isChecked());

	icon_button->click();
	EXPECT_EQ(emissions, 2);
	EXPECT_EQ(received, ProjectToolbar::icon_view);
	EXPECT_TRUE(icon_button->isChecked());
	EXPECT_FALSE(list_button->isChecked());

	// SetView only checks the button; it does not re-emit ViewChanged
	toolbar.set_view(ProjectToolbar::tree_view);
	EXPECT_EQ(emissions, 2);
	EXPECT_TRUE(tree_button->isChecked());
	EXPECT_FALSE(icon_button->isChecked());
}
