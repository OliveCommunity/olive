#include <gtest/gtest.h>

#include <memory>

#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QStyleOptionViewItem>
#include <QTest>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/folder/folder.h"
#include "oakutil/define.h"
#include "render/diskmanager.h"
#include "widget/projectexplorer/projectexplorericonview.h"
#include "widget/projectexplorer/projectexplorericonviewitemdelegate.h"
#include "widget/projectexplorer/projectexplorerlistview.h"
#include "widget/projectexplorer/projectexplorerlistviewbase.h"
#include "widget/projectexplorer/projectexplorerlistviewitemdelegate.h"
#include "widget/projectexplorer/projectexplorernavigation.h"
#include "widget/projectexplorer/projectexplorertreeview.h"

using namespace olive;

namespace
{

// Undo commands that rename/move items go through app-wide singletons
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

TEST(ProjectExplorerNavigation, DefaultsMatchDocumentedState)
{
	ProjectExplorerNavigation nav(nullptr);

	auto *dir_up = nav.findChild<QPushButton *>();
	auto *label = nav.findChild<QLabel *>();
	auto *slider = nav.findChild<QSlider *>();
	ASSERT_NE(dir_up, nullptr);
	ASSERT_NE(label, nullptr);
	ASSERT_NE(slider, nullptr);

	// Root folder assumption: no parent to go up to, no folder name
	EXPECT_FALSE(dir_up->isEnabled());
	EXPECT_TRUE(label->text().isEmpty());

	// Slider spans the project icon size constants, defaulting to the default
	EXPECT_EQ(slider->minimum(), k_project_icon_size_minimum);
	EXPECT_EQ(slider->maximum(), k_project_icon_size_maximum);
	EXPECT_EQ(slider->value(), k_project_icon_size_default);
	EXPECT_EQ(slider->orientation(), Qt::Horizontal);
}

TEST(ProjectExplorerNavigation, SetTextUpdatesLabel)
{
	ProjectExplorerNavigation nav(nullptr);
	auto *label = nav.findChild<QLabel *>();
	ASSERT_NE(label, nullptr);

	nav.set_text(QStringLiteral("Media"));
	EXPECT_EQ(label->text(), QStringLiteral("Media"));

	nav.set_text(QString());
	EXPECT_TRUE(label->text().isEmpty());
}

TEST(ProjectExplorerNavigation, DirUpButtonOnlyEmitsWhenEnabled)
{
	ProjectExplorerNavigation nav(nullptr);
	auto *dir_up = nav.findChild<QPushButton *>();
	ASSERT_NE(dir_up, nullptr);

	QSignalSpy spy(&nav, &ProjectExplorerNavigation::directory_up_clicked);

	// Disabled button swallows clicks
	dir_up->click();
	EXPECT_EQ(spy.count(), 0);

	nav.set_dir_up_enabled(true);
	EXPECT_TRUE(dir_up->isEnabled());

	dir_up->click();
	EXPECT_EQ(spy.count(), 1);

	// Disabling again silences it
	nav.set_dir_up_enabled(false);
	dir_up->click();
	EXPECT_EQ(spy.count(), 1);
}

TEST(ProjectExplorerNavigation, SizeSliderEmitsSizeChanged)
{
	ProjectExplorerNavigation nav(nullptr);
	auto *slider = nav.findChild<QSlider *>();
	ASSERT_NE(slider, nullptr);

	QSignalSpy spy(&nav, &ProjectExplorerNavigation::size_changed);

	slider->setValue(k_project_icon_size_minimum);
	ASSERT_EQ(spy.count(), 1);
	EXPECT_EQ(spy.first().first().toInt(), k_project_icon_size_minimum);

	// The setter is implemented as a plain slider setValue, so despite the
	// header comment claiming otherwise it does forward through the signal
	nav.set_size_value(k_project_icon_size_maximum);
	EXPECT_EQ(slider->value(), k_project_icon_size_maximum);
	ASSERT_EQ(spy.count(), 2);
	EXPECT_EQ(spy.at(1).first().toInt(), k_project_icon_size_maximum);

	// Setting the same value again is a no-op, as QSlider only emits on change
	nav.set_size_value(k_project_icon_size_maximum);
	EXPECT_EQ(spy.count(), 2);
}

TEST(ProjectExplorerListViewBase, ConstructionDefaults)
{
	ProjectExplorerListViewBase view(nullptr);

	EXPECT_EQ(view.movement(), QListView::Free);
	EXPECT_EQ(view.selectionMode(), QAbstractItemView::ExtendedSelection);
	EXPECT_EQ(view.resizeMode(), QListView::Adjust);
	EXPECT_EQ(view.contextMenuPolicy(), Qt::CustomContextMenu);
}

TEST(ProjectExplorerListViewBase, DoubleClickEmptyAreaEmitsSignal)
{
	QStringListModel model({ QStringLiteral("One"), QStringLiteral("Two"),
							 QStringLiteral("Three") });

	ProjectExplorerListViewBase view(nullptr);
	view.setModel(&model);
	view.resize(400, 300);
	view.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));

	const QModelIndex first = model.index(0, 0);
	const QRect item_rect = view.visualRect(first);
	if (!item_rect.isValid()) {
		GTEST_SKIP() << "View did not lay out items under the offscreen platform";
	}

	int hits = 0;
	QObject::connect(&view,
					 &ProjectExplorerListViewBase::double_clicked_empty_area,
					 [&hits] { ++hits; });

	// Double clicking an item is not an empty-area click
	QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
					   item_rect.center());
	EXPECT_EQ(hits, 0);

	// Double clicking below the last item is
	const QPoint empty(view.viewport()->width() - 4,
					   view.viewport()->height() - 4);
	if (view.indexAt(empty).isValid()) {
		GTEST_SKIP() << "Could not find an item-free spot in the viewport";
	}
	QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, empty);
	EXPECT_EQ(hits, 1);
}

TEST(ProjectExplorerListViewBase, CtrlClickExtendsSelection)
{
	QStringListModel model({ QStringLiteral("One"), QStringLiteral("Two"),
							 QStringLiteral("Three") });

	ProjectExplorerListViewBase view(nullptr);
	view.setModel(&model);
	view.resize(400, 300);
	view.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));

	const QModelIndex first = model.index(0, 0);
	const QModelIndex third = model.index(2, 0);
	const QRect first_rect = view.visualRect(first);
	const QRect third_rect = view.visualRect(third);
	if (!first_rect.isValid() || !third_rect.isValid()) {
		GTEST_SKIP() << "View did not lay out items under the offscreen platform";
	}

	// Plain click selects exactly one item and makes it current
	QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
					  first_rect.center());
	EXPECT_EQ(view.selectionModel()->currentIndex(), first);
	EXPECT_EQ(view.selectionModel()->selectedIndexes().size(), 1);

	// ExtendedSelection: Ctrl+click adds to the selection instead of replacing
	QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier,
					  third_rect.center());
	const QModelIndexList selected = view.selectionModel()->selectedIndexes();
	EXPECT_EQ(selected.size(), 2);
	EXPECT_TRUE(selected.contains(first));
	EXPECT_TRUE(selected.contains(third));
}

TEST(ProjectExplorerListView, UsesListModeAndItemDelegate)
{
	ProjectExplorerListView view(nullptr);

	EXPECT_EQ(view.viewMode(), QListView::ListMode);

	// The delegate classes carry no Q_OBJECT, so cast with RTTI
	EXPECT_NE(dynamic_cast<ProjectExplorerListViewItemDelegate *>(
				  view.itemDelegate()),
			  nullptr);

	// Inherits the base view behavior
	EXPECT_EQ(view.selectionMode(), QAbstractItemView::ExtendedSelection);
	EXPECT_EQ(view.contextMenuPolicy(), Qt::CustomContextMenu);
}

TEST(ProjectExplorerIconView, UsesIconModeAndItemDelegate)
{
	ProjectExplorerIconView view(nullptr);

	EXPECT_EQ(view.viewMode(), QListView::IconMode);

	EXPECT_NE(dynamic_cast<ProjectExplorerIconViewItemDelegate *>(
				  view.itemDelegate()),
			  nullptr);

	EXPECT_EQ(view.selectionMode(), QAbstractItemView::ExtendedSelection);
}

TEST(ProjectExplorerTreeView, ConstructionDefaultsEnableDragDrop)
{
	ProjectExplorerTreeView view(nullptr);

	EXPECT_EQ(view.selectionMode(), QAbstractItemView::ExtendedSelection);
	EXPECT_EQ(view.dragDropMode(), QAbstractItemView::DragDrop);
	EXPECT_TRUE(view.dragEnabled());
	EXPECT_TRUE(view.acceptDrops());
	EXPECT_EQ(view.contextMenuPolicy(), Qt::CustomContextMenu);
}

TEST(ProjectExplorerTreeView, DoubleClickEmptyAreaEmitsSignal)
{
	QStringListModel model({ QStringLiteral("One"), QStringLiteral("Two"),
							 QStringLiteral("Three") });

	ProjectExplorerTreeView view(nullptr);
	view.setModel(&model);
	view.resize(400, 300);
	view.show();
	ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));

	const QModelIndex first = model.index(0, 0);
	const QRect item_rect = view.visualRect(first);
	if (!item_rect.isValid()) {
		GTEST_SKIP() << "View did not lay out items under the offscreen platform";
	}

	int hits = 0;
	QObject::connect(&view, &ProjectExplorerTreeView::double_clicked_empty_area,
					 [&hits] { ++hits; });

	QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
					   item_rect.center());
	EXPECT_EQ(hits, 0);

	const QPoint empty(view.viewport()->width() - 4,
					   view.viewport()->height() - 4);
	if (view.indexAt(empty).isValid()) {
		GTEST_SKIP() << "Could not find an item-free spot in the viewport";
	}
	QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, empty);
	EXPECT_EQ(hits, 1);
}

TEST(ProjectExplorerListViewItemDelegate, SizeHintIsSquareFromDecorationHeight)
{
	ProjectExplorerListViewItemDelegate delegate;

	QStyleOptionViewItem opt;
	opt.decorationSize = QSize(10, 32);
	EXPECT_EQ(delegate.sizeHint(opt, QModelIndex()), QSize(32, 32));

	opt.decorationSize = QSize(64, 20);
	EXPECT_EQ(delegate.sizeHint(opt, QModelIndex()), QSize(20, 20));
}

TEST(ProjectExplorerListViewItemDelegate, PaintFillsHighlightWhenSelected)
{
	QStandardItemModel model;
	model.appendRow(new QStandardItem(QStringLiteral("Clip")));

	QStyleOptionViewItem opt;
	opt.rect = QRect(0, 0, 200, 24);
	opt.decorationSize = QSize(24, 24);

	ProjectExplorerListViewItemDelegate delegate;

	// Selected rows are filled with the palette highlight
	QImage selected_img(200, 24, QImage::Format_ARGB32);
	selected_img.fill(Qt::transparent);
	opt.state = QStyle::State_Enabled | QStyle::State_Selected;
	{
		QPainter p(&selected_img);
		delegate.paint(&p, opt, model.index(0, 0));
	}
	EXPECT_EQ(selected_img.pixelColor(0, 0).rgb(),
			  opt.palette.highlight().color().rgb());

	// Unselected rows paint nothing into the icon area (no icon set)
	QImage plain_img(200, 24, QImage::Format_ARGB32);
	plain_img.fill(Qt::transparent);
	opt.state = QStyle::State_Enabled;
	{
		QPainter p(&plain_img);
		delegate.paint(&p, opt, model.index(0, 0));
	}
	EXPECT_EQ(plain_img.pixelColor(0, 0).alpha(), 0);
}

TEST(ProjectExplorerIconViewItemDelegate, SizeHintIsFixed)
{
	ProjectExplorerIconViewItemDelegate delegate;

	QStyleOptionViewItem opt;
	opt.decorationSize = QSize(16, 16);

	// Always 256x256 regardless of the option
	EXPECT_EQ(delegate.sizeHint(opt, QModelIndex()), QSize(256, 256));
	EXPECT_EQ(delegate.sizeHint(QStyleOptionViewItem(), QModelIndex()),
			  QSize(256, 256));
}

TEST(ProjectExplorerIconViewItemDelegate, PaintDrawsTextBand)
{
	QStandardItemModel model;
	model.appendRow(new QStandardItem(QStringLiteral("Clip")));
	const QModelIndex index = model.index(0, 0);
	model.setData(index, QStringLiteral("00:00:01:00"), Qt::UserRole);

	ProjectExplorerIconViewItemDelegate delegate;

	QStyleOptionViewItem opt;
	opt.rect = QRect(0, 0, 256, 256);

	// The text band occupies the bottom fm.height() rows of the cell; the
	// painter over a bare QImage uses the default application font
	const QFontMetrics fm((QFont()));
	const int band_top = opt.rect.height() - fm.height();
	ASSERT_GT(opt.rect.height() / 2, fm.height());

	// Unselected: band background is white
	QImage plain_img(256, 256, QImage::Format_ARGB32);
	plain_img.fill(Qt::transparent);
	opt.state = QStyle::State_Enabled;
	{
		QPainter p(&plain_img);
		delegate.paint(&p, opt, index);
	}
	EXPECT_EQ(plain_img.pixelColor(opt.rect.width() / 2, band_top).rgb(),
			  QColor(Qt::white).rgb());

	// Selected: band background follows the palette highlight
	QImage selected_img(256, 256, QImage::Format_ARGB32);
	selected_img.fill(Qt::transparent);
	opt.state = QStyle::State_Enabled | QStyle::State_Selected;
	{
		QPainter p(&selected_img);
		delegate.paint(&p, opt, index);
	}
	EXPECT_EQ(selected_img.pixelColor(opt.rect.width() / 2, band_top).rgb(),
			  opt.palette.highlight().color().rgb());
}

class ProjectExplorerUndoTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();
		ensure_app_singletons();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	template <typename T> T *make_node()
	{
		auto *node = new T();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(ProjectExplorerUndoTest, FolderAddChildRedoUndoRoundTrips)
{
	Folder *root = project_->root();
	Footage *footage = make_node<Footage>();
	ASSERT_EQ(root->item_child_count(), 0);
	ASSERT_EQ(footage->folder(), nullptr);

	FolderAddChild cmd(root, footage);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(root->item_child_count(), 1);
	EXPECT_EQ(root->item_child(0), footage);
	EXPECT_EQ(footage->folder(), root);

	// UndoCommand guards on a done flag: a second redo is a no-op
	cmd.redo_now();
	EXPECT_EQ(root->item_child_count(), 1);

	cmd.undo_now();
	EXPECT_EQ(root->item_child_count(), 0);
	EXPECT_EQ(footage->folder(), nullptr);

	// Likewise a second undo does nothing
	cmd.undo_now();
	EXPECT_EQ(root->item_child_count(), 0);
}

TEST_F(ProjectExplorerUndoTest, FolderAddChildEmitsFolderInsertRemoveSignals)
{
	Folder *root = project_->root();
	Footage *footage = make_node<Footage>();

	// The signal payload carries Node*, which is not a registered metatype,
	// so count with lambdas rather than QSignalSpy
	int insert_began = 0, insert_ended = 0, remove_began = 0, remove_ended = 0;
	Node *inserted_node = nullptr;
	int inserted_index = -1;
	QObject::connect(root, &Folder::begin_insert_item,
					 [&](Node *n, int index) {
						 ++insert_began;
						 inserted_node = n;
						 inserted_index = index;
					 });
	QObject::connect(root, &Folder::end_insert_item,
					 [&] { ++insert_ended; });
	QObject::connect(root, &Folder::begin_remove_item,
					 [&](Node *, int) { ++remove_began; });
	QObject::connect(root, &Folder::end_remove_item,
					 [&] { ++remove_ended; });

	FolderAddChild cmd(root, footage);
	cmd.redo_now();
	EXPECT_EQ(insert_began, 1);
	EXPECT_EQ(insert_ended, 1);
	EXPECT_EQ(inserted_node, footage);
	EXPECT_EQ(inserted_index, 0);
	EXPECT_EQ(remove_began, 0);

	cmd.undo_now();
	EXPECT_EQ(remove_began, 1);
	EXPECT_EQ(remove_ended, 1);
	EXPECT_EQ(insert_began, 1);
}

TEST_F(ProjectExplorerUndoTest, RemoveElementCommandRedoUndoRoundTrips)
{
	Folder *root = project_->root();
	Footage *a = make_node<Footage>();
	Footage *b = make_node<Footage>();
	FolderAddChild(root, a).redo_now();
	FolderAddChild(root, b).redo_now();
	ASSERT_EQ(root->item_child_count(), 2);

	Folder::RemoveElementCommand cmd(root, a);
	EXPECT_EQ(cmd.get_relevant_project(), project_.get());

	cmd.redo_now();
	EXPECT_EQ(root->item_child_count(), 1);
	EXPECT_EQ(root->index_of_child(a), -1);
	EXPECT_EQ(a->folder(), nullptr);
	EXPECT_EQ(b->folder(), root);

	cmd.undo_now();
	EXPECT_EQ(root->item_child_count(), 2);
	EXPECT_NE(root->index_of_child(a), -1);
	EXPECT_EQ(a->folder(), root);
	EXPECT_EQ(b->folder(), root);
}

TEST_F(ProjectExplorerUndoTest, RemoveElementCommandOnNonChildIsNoOp)
{
	Folder *root = project_->root();
	Footage *stray = make_node<Footage>();
	ASSERT_EQ(root->item_child_count(), 0);

	// The child was never connected to the folder, so the command finds no
	// array index and both directions do nothing
	Folder::RemoveElementCommand cmd(root, stray);
	cmd.redo_now();
	EXPECT_EQ(root->item_child_count(), 0);
	EXPECT_EQ(stray->folder(), nullptr);

	cmd.undo_now();
	EXPECT_EQ(root->item_child_count(), 0);
}

TEST_F(ProjectExplorerUndoTest, NestedFolderAddChildTracksHierarchy)
{
	Folder *root = project_->root();
	Folder *folder = make_node<Folder>();
	Footage *footage = make_node<Footage>();

	FolderAddChild add_folder(root, folder);
	add_folder.redo_now();
	FolderAddChild add_footage(folder, footage);
	add_footage.redo_now();

	EXPECT_EQ(root->item_child_count(), 1);
	EXPECT_EQ(folder->item_child_count(), 1);
	EXPECT_TRUE(root->has_child_recursive(footage));
	EXPECT_EQ(footage->folder(), folder);

	// Undoing the inner add leaves the folder in place but empty
	add_footage.undo_now();
	EXPECT_EQ(folder->item_child_count(), 0);
	EXPECT_EQ(footage->folder(), nullptr);
	EXPECT_EQ(folder->folder(), root);

	add_folder.undo_now();
	EXPECT_EQ(root->item_child_count(), 0);
	EXPECT_EQ(folder->folder(), nullptr);
}
