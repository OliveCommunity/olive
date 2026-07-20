/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "projectexplorer.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

#include "common/define.h"
#include "core.h"
#include "dialog/footageproperties/footageproperties.h"
#include "dialog/proxy/proxydialog.h"
#include "dialog/sequence/sequence.h"
#include "projectexplorerundo.h"
#include "oakengine/footage.h"
#include "oakengine/node.h"
#include "task/taskmanager.h"
#include "widget/menu/menu.h"
#include "widget/menu/menushared.h"
#include "node/nodeundo.h"
#include "window/mainwindow/mainwindow.h"
#include "window/mainwindow/mainwindowundo.h"
#include "widget/timelinewidget/timelinewidget.h"

namespace olive
{

namespace
{
QVector<Footage *> get_selected_proxy_footage(const QVector<Node *> &items)
{
	QVector<Footage *> footage;
	for (Node *node : items) {
		Footage *candidate = dynamic_cast<Footage *>(node);
		if (!candidate || !candidate->get_first_enabled_video_stream().is_valid() ||
			footage.contains(candidate)) {
			continue;
		}
		footage.append(candidate);
	}
	return footage;
}

/**
 * @brief Proxy generation driven by the liboakengine C ABI facade
 *
 * Replaces the direct ProxyManager::get_or_start_proxy() drive: the actual
 * transcode and its synchronous wait live behind
 * oakengine_footage_proxy_generate() (which also records the proxy state
 * on the footage and invalidates it), while the task stays on the
 * TaskManager queue like before.
 */
class FacadeProxyTask : public Task {
public:
	FacadeProxyTask(Footage *footage)
		: footage_(footage)
	{
		set_title(tr("Generating proxy for \"%1\"")
					  .arg(footage->get_label_or_name()));
	}

protected:
	virtual bool run() override
	{
		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(footage_));
		const int rc = oakengine_footage_proxy_generate(handle);
		oakengine_footage_free(handle);
		if (rc != OAKENGINE_OK) {
			char err[512];
			err[0] = '\0';
			oakengine_footage_last_error(err, sizeof(err));
			set_error(err[0] ? QString::fromUtf8(err) :
							   tr("Proxy generation failed"));
			return false;
		}
		return true;
	}

private:
	Footage *footage_;
};
}

ProjectExplorer::ProjectExplorer(QWidget *parent)
	: QWidget(parent)
	, model_(this)
{
	// Create layout
	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	// Set up navigation bar
	nav_bar_ = new ProjectExplorerNavigation(this);
	connect(nav_bar_, &ProjectExplorerNavigation::size_changed, this,
			&ProjectExplorer::size_changed_slot);
	connect(nav_bar_, &ProjectExplorerNavigation::directory_up_clicked, this,
			&ProjectExplorer::dir_up_slot);
	layout->addWidget(nav_bar_);

	// Set up stacked widget
	stacked_widget_ = new QStackedWidget(this);
	layout->addWidget(stacked_widget_);

	// Set up sort filter proxy model
	sort_model_.setSourceModel(&model_);
	sort_model_.setFilterCaseSensitivity(Qt::CaseInsensitive);
	sort_model_.setSortRole(ProjectViewModel::k_inner_text_role);

	// Add tree view to stacked widget
	tree_view_ = new ProjectExplorerTreeView(stacked_widget_);
	tree_view_->setSortingEnabled(true);
	tree_view_->sortByColumn(0, Qt::AscendingOrder);
	tree_view_->setContextMenuPolicy(Qt::CustomContextMenu);
	add_view(tree_view_);

	// Add list view to stacked widget
	list_view_ = new ProjectExplorerListView(stacked_widget_);
	list_view_->setContextMenuPolicy(Qt::CustomContextMenu);
	add_view(list_view_);

	// Add icon view to stacked widget
	icon_view_ = new ProjectExplorerIconView(stacked_widget_);
	icon_view_->setContextMenuPolicy(Qt::CustomContextMenu);
	add_view(icon_view_);

	// Set default view to tree view
	set_view_type(ProjectToolbar::tree_view);

	// Set default icon size
	size_changed_slot(k_project_icon_size_default);

	connect(tree_view_, &ProjectExplorerTreeView::customContextMenuRequested,
			this, &ProjectExplorer::show_context_menu);
	connect(list_view_, &ProjectExplorerListView::customContextMenuRequested,
			this, &ProjectExplorer::show_context_menu);
	connect(icon_view_, &ProjectExplorerIconView::customContextMenuRequested,
			this, &ProjectExplorer::show_context_menu);

	update_nav_bar_text();
}

const ProjectToolbar::ViewType &ProjectExplorer::view_type() const
{
	return view_type_;
}

void ProjectExplorer::set_view_type(ProjectToolbar::ViewType type)
{
	view_type_ = type;

	// Set widget based on view type
	switch (view_type_) {
	case ProjectToolbar::tree_view:
		stacked_widget_->setCurrentWidget(tree_view_);
		nav_bar_->setVisible(false);
		break;
	case ProjectToolbar::list_view:
		stacked_widget_->setCurrentWidget(list_view_);
		nav_bar_->setVisible(true);
		break;
	case ProjectToolbar::icon_view:
		stacked_widget_->setCurrentWidget(icon_view_);
		nav_bar_->setVisible(true);
		break;
	}
}

void ProjectExplorer::edit(Node *item)
{
	current_view()->edit(
		sort_model_.mapFromSource(model_.create_index_from_item(item)));
}

void ProjectExplorer::add_view(QAbstractItemView *view)
{
	view->setModel(&sort_model_);
	view->setEditTriggers(QAbstractItemView::SelectedClicked);
	connect(view, &QAbstractItemView::doubleClicked, this,
			&ProjectExplorer::item_double_clicked_slot);
	connect(view->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &ProjectExplorer::view_selection_changed);
	connect(view, SIGNAL(double_clicked_empty_area()), this,
			SLOT(view_empty_area_double_clicked_slot()));
	stacked_widget_->addWidget(view);
}

void ProjectExplorer::browse_to_folder(const QModelIndex &index)
{
	// Set appropriate views to this index
	icon_view_->setRootIndex(index);
	list_view_->setRootIndex(index);

	// Set navbar text to folder's name
	update_nav_bar_text();

	// Set directory up enabled button based on whether we're in root or not
	nav_bar_->set_dir_up_enabled(index.isValid());
}

int ProjectExplorer::confirm_item_deletion(Node *item)
{
	QMessageBox msgbox(this);
	msgbox.setWindowTitle(tr("Confirm Item Deletion"));
	msgbox.setIcon(QMessageBox::Warning);

	QStringList connected_nodes_names;
	foreach (const Node::OutputConnection &connected,
			 item->output_connections()) {
		if (!dynamic_cast<Folder *>(connected.second.node())) {
			connected_nodes_names.append(
				get_human_readable_node_name(connected.second.node()));
		}
	}

	msgbox.setText(
		tr("The item \"%1\" is currently connected to the following nodes:\n\n"
		   "%2\n\n"
		   "Are you sure you wish to delete this footage?")
			.arg(get_human_readable_node_name(item),
				 connected_nodes_names.join('\n')));

	// Set up buttons
	msgbox.addButton(QMessageBox::Yes);
	msgbox.addButton(QMessageBox::YesToAll);
	msgbox.addButton(QMessageBox::No);
	msgbox.addButton(QMessageBox::Cancel);

	// Run messagebox
	return msgbox.exec();
}

bool ProjectExplorer::delete_items_internal(const QVector<Node *> &selected,
										  bool &check_if_item_is_in_use,
										  MultiUndoCommand *command)
{
	for (int i = 0; i < selected.size(); i++) {
		// Delete sequences first
		Node *node = selected.at(i);

		bool can_delete_item = true;

		if (check_if_item_is_in_use) {
			foreach (const Node::OutputConnection &oc,
					 node->output_connections()) {
				Folder *folder_test = dynamic_cast<Folder *>(oc.second.node());
				if (!folder_test) {
					// This sequence outputs to SOMETHING, confirm the user if they want to delete this
					int r = confirm_item_deletion(node);

					switch (r) {
					case QMessageBox::No:
						can_delete_item = false;
						break;
					case QMessageBox::Cancel:
						return false;
					case QMessageBox::YesToAll:
						check_if_item_is_in_use = false;
						break;
					}
				}
			}
		}

		if (can_delete_item) {
			Sequence *sequence = dynamic_cast<Sequence *>(node);
			if (sequence &&
				Core::instance()->main_window()->is_sequence_open(sequence)) {
				command->add_child(new CloseSequenceCommand(sequence));
			}

			if (node->folder()) {
				command->add_child(
					new Folder::RemoveElementCommand(node->folder(), node));
			}

			command->add_child(
				new NodeRemoveWithExclusiveDependenciesAndDisconnect(node));
		}
	}

	return true;
}

QString ProjectExplorer::get_human_readable_node_name(Node *node)
{
	if (node->get_label().isEmpty()) {
		return node->name();
	} else {
		return tr("%1 (%2)").arg(node->get_label(), node->name());
	}
}

void ProjectExplorer::update_nav_bar_text()
{
	QString absolute;

	Folder *f = static_cast<Folder *>(
		sort_model_.mapToSource(list_view_->rootIndex()).internalPointer());
	while (f && f != project()->root()) {
		absolute.prepend(QStringLiteral("%1 / ").arg(f->get_label()));
		f = f->folder();
	}

	absolute.prepend(QStringLiteral("/ "));

	nav_bar_->set_text(absolute);
}

QAbstractItemView *ProjectExplorer::current_view() const
{
	return static_cast<QAbstractItemView *>(stacked_widget_->currentWidget());
}

void ProjectExplorer::view_empty_area_double_clicked_slot()
{
	emit double_clicked_item(nullptr);
}

void ProjectExplorer::item_double_clicked_slot(const QModelIndex &index)
{
	// Retrieve source item from index
	Node *i =
		static_cast<Node *>(sort_model_.mapToSource(index).internalPointer());

	// If the item is a folder, browse to it
	if (dynamic_cast<Folder *>(i) &&
		(view_type() == ProjectToolbar::list_view ||
		 view_type() == ProjectToolbar::icon_view)) {
		browse_to_folder(index);
	}

	// Emit a signal
	emit double_clicked_item(i);
}

void ProjectExplorer::size_changed_slot(int s)
{
	icon_view_->setGridSize(QSize(s, s));

	list_view_->setIconSize(QSize(s, s));
}

void ProjectExplorer::dir_up_slot()
{
	QModelIndex current_root = icon_view_->rootIndex();

	if (current_root.isValid()) {
		QModelIndex parent = current_root.parent();

		browse_to_folder(parent);
	}
}

void ProjectExplorer::rename_selected_item()
{
	auto indexes = current_view()->selectionModel()->selectedRows();
	if (!indexes.empty()) {
		current_view()->edit(indexes.first());
	}
}

void ProjectExplorer::set_search_filter(const QString &s)
{
	sort_model_.setFilterFixedString(s);
}

void ProjectExplorer::show_context_menu()
{
	Menu menu;
	Menu new_menu;

	context_menu_items_ = selected_items();

	if (context_menu_items_.isEmpty()) {
		// Items to show if no items are selected

		// "New" menu
		new_menu.setTitle(tr("&New"));
		MenuShared::instance()->add_items_for_new_menu(&new_menu);
		menu.addMenu(&new_menu);

		// "Import" action
		QAction *import_action = menu.addAction(tr("&Import..."));
		connect(import_action, &QAction::triggered, Core::instance(),
				&Core::dialog_import_show);
	} else {
		// Actions to add when only one item is selected
		if (context_menu_items_.size() == 1) {
			Node *context_menu_item = context_menu_items_.first();

			if (dynamic_cast<Folder *>(context_menu_item)) {
				QAction *open_in_new_tab =
					menu.addAction(tr("Open in New Tab"));
				connect(open_in_new_tab, &QAction::triggered, this,
						&ProjectExplorer::open_context_menu_item_in_new_tab);

				QAction *open_in_new_window =
					menu.addAction(tr("Open in New Window"));
				connect(open_in_new_window, &QAction::triggered, this,
						&ProjectExplorer::open_context_menu_item_in_new_window);

			} else if (dynamic_cast<Footage *>(context_menu_item)) {
				QString reveal_text;

#if defined(Q_OS_WINDOWS)
				reveal_text = tr("Reveal in Explorer");
#elif defined(Q_OS_MAC)
				reveal_text = tr("Reveal in Finder");
#else
				reveal_text = tr("Reveal in File Manager");
#endif

				QAction *reveal_action = menu.addAction(reveal_text);
				connect(reveal_action, &QAction::triggered, this,
						&ProjectExplorer::reveal_selected_footage);

				QAction *replace_action = menu.addAction(tr("Replace Footage"));
				connect(replace_action, &QAction::triggered, this,
						&ProjectExplorer::replace_selected_footage);
			}

			menu.addSeparator();
		}

		bool all_items_are_footage = true;
		bool all_items_have_video_streams = true;
		bool all_items_are_footage_or_sequence = true;

		foreach (Node *i, context_menu_items_) {
			Footage *footage_cast_test = dynamic_cast<Footage *>(i);
			Sequence *sequence_cast_test = dynamic_cast<Sequence *>(i);

			if (footage_cast_test &&
				!footage_cast_test->has_enabled_video_streams()) {
				all_items_have_video_streams = false;
			}

			if (!footage_cast_test) {
				all_items_are_footage = false;
			}

			if (!footage_cast_test && !sequence_cast_test) {
				all_items_are_footage_or_sequence = false;
			}
		}

		if (all_items_are_footage && all_items_have_video_streams) {
			const QVector<Footage *> proxy_footage =
				get_selected_proxy_footage(context_menu_items_);

			Menu *proxy_menu = new Menu(tr("Proxy"), &menu);
			menu.addMenu(proxy_menu);

			QAction *generate_proxy =
				proxy_menu->addAction(tr("Generate Proxy"));
			generate_proxy->setEnabled(!proxy_footage.isEmpty());
			connect(generate_proxy, &QAction::triggered, this,
					&ProjectExplorer::generate_proxies_for_selected_footage);

			QAction *use_proxy = proxy_menu->addAction(tr("Use Proxy"));
			use_proxy->setCheckable(true);
			use_proxy->setEnabled(!proxy_footage.isEmpty());
			use_proxy->setChecked(
				!proxy_footage.isEmpty() &&
				std::all_of(proxy_footage.cbegin(), proxy_footage.cend(),
							[](const Footage *footage) {
								return footage->proxy_enabled();
							}));
			connect(use_proxy, &QAction::triggered, this,
					&ProjectExplorer::set_selected_footage_proxy_enabled);

			QAction *reveal_proxy = proxy_menu->addAction(tr("Reveal Proxy"));
			reveal_proxy->setEnabled(
				std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
							[](const Footage *footage) {
								return !footage->proxy_path().isEmpty();
							}));
			connect(reveal_proxy, &QAction::triggered, this,
					&ProjectExplorer::reveal_proxy_for_selected_footage);

			QAction *delete_proxy = proxy_menu->addAction(tr("Delete Proxy"));
			delete_proxy->setEnabled(
				std::any_of(proxy_footage.cbegin(), proxy_footage.cend(),
							[](const Footage *footage) {
								return !footage->proxy_path().isEmpty();
							}));
			connect(delete_proxy, &QAction::triggered, this,
					&ProjectExplorer::delete_proxies_for_selected_footage);

			QAction *proxy_settings =
				proxy_menu->addAction(tr("Proxy Settings..."));
			connect(proxy_settings, &QAction::triggered, this,
					&ProjectExplorer::show_proxy_dialog_for_selected_footage);
		}

		Q_UNUSED(all_items_are_footage_or_sequence)

		if (context_menu_items_.size() == 1) {
			menu.addSeparator();

			auto rename_action = menu.addAction(tr("Rename"));
			connect(rename_action, &QAction::triggered, this,
					&ProjectExplorer::rename_selected_item);
		}

		auto delete_action = menu.addAction(tr("Delete"));
		connect(delete_action, &QAction::triggered, this,
				&ProjectExplorer::delete_selected);

		if (context_menu_items_.size() == 1) {
			menu.addSeparator();

			QAction *properties_action = menu.addAction(tr("P&roperties"));
			connect(properties_action, &QAction::triggered, this,
					&ProjectExplorer::show_item_properties_dialog);
		}
	}

	menu.exec(QCursor::pos());
}

void ProjectExplorer::show_item_properties_dialog()
{
	Node *sel = context_menu_items_.first();

	// FIXME: Support for multiple items
	if (dynamic_cast<Footage *>(sel)) {
		FootagePropertiesDialog fpd(this, static_cast<Footage *>(sel));
		fpd.exec();

	} else if (dynamic_cast<Folder *>(sel)) {
		Core::instance()->label_nodes(context_menu_items_);

	} else if (dynamic_cast<Sequence *>(sel)) {
		SequenceDialog sd(static_cast<Sequence *>(sel),
						  SequenceDialog::k_existing, this);
		sd.exec();
	}
}

void ProjectExplorer::reveal_selected_footage()
{
	Footage *footage = static_cast<Footage *>(context_menu_items_.first());

#if defined(Q_OS_WINDOWS)
	// Explorer
	QStringList args;
	args << "/select," << QDir::toNativeSeparators(footage->filename());
	QProcess::startDetached("explorer", args);
#elif defined(Q_OS_MAC)
	QStringList args;
	args << "-e";
	args << "tell application \"Finder\"";
	args << "-e";
	args << "activate";
	args << "-e";
	args << "select POSIX file \"" + footage->filename() + "\"";
	args << "-e";
	args << "end tell";
	QProcess::startDetached("osascript", args);
#else
	QDesktopServices::openUrl(QUrl::fromLocalFile(
		QFileInfo(footage->filename()).dir().absolutePath()));
#endif
}

void ProjectExplorer::replace_selected_footage()
{
	Footage *footage = static_cast<Footage *>(context_menu_items_.first());

	QString file =
		QFileDialog::getOpenFileName(this, tr("Replace Footage"), QString(),
									 Core::footage_file_dialog_filter());
	if (!file.isEmpty()) {
		if (!Core::is_footage_extension_allowed(file)) {
			QMessageBox::warning(
				this, tr("Unsupported media"),
				tr("This file type is not allowed by the current media type "
				   "filter."));
			return;
		}

		// Change the filename through the facade relink (reprobes the new
		// file and resets proxy/stream state); the label policy stays here.
		OakEngineFootage *facade_handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(footage));
		const int relink_rc = oakengine_footage_relink(
			facade_handle, file.toUtf8().constData());
		oakengine_footage_free(facade_handle);
		if (relink_rc != OAKENGINE_OK) {
			char err[512];
			err[0] = '\0';
			oakengine_footage_last_error(err, sizeof(err));
			QMessageBox::warning(
				this, tr("Cannot replace footage"),
				err[0] ? QString::fromUtf8(err) :
						 tr("The file could not be used as media."));
			return;
		}

		if (QFileInfo(footage->filename()).fileName() ==
			footage->get_label()) {
			// Footage label == filename, change label too
			oakengine_node_set_label(
				reinterpret_cast<OakEngineNode *>(footage),
				QFileInfo(file).fileName().toUtf8().constData());
		}
	}
}

void ProjectExplorer::open_context_menu_item_in_new_tab()
{
	Core::instance()->main_window()->open_folder(
		static_cast<Folder *>(context_menu_items_.first()), false);
}

void ProjectExplorer::open_context_menu_item_in_new_window()
{
	Core::instance()->main_window()->open_folder(
		static_cast<Folder *>(context_menu_items_.first()), true);
}

void ProjectExplorer::generate_proxies_for_selected_footage()
{
	if (!project()) {
		qWarning() << "GenerateProxiesForSelectedFootage: no project";
		return;
	}

	const QVector<Footage *> footage =
		get_selected_proxy_footage(context_menu_items_);
	qDebug()
		<< "GenerateProxiesForSelectedFootage: starting proxy generation for"
		<< footage.size() << "footage item(s)";
	for (Footage *item : footage) {
		const VideoParams video = item->get_first_enabled_video_stream();
		if (!video.is_valid()) {
			qWarning()
				<< "GenerateProxiesForSelectedFootage: skipping item with no valid video stream"
				<< item->filename();
			continue;
		}

		// Queue one facade-backed task per footage item (same queueing
		// semantics as the old per-footage proxy tasks).
		TaskManager::instance()->add_task(new FacadeProxyTask(item));
	}
}

void ProjectExplorer::set_selected_footage_proxy_enabled(bool enabled)
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(context_menu_items_);
	qDebug() << "ProjectExplorer::SetSelectedFootageProxyEnabled:" << enabled
			 << "footage count=" << footage.size();
	for (Footage *item : footage) {
		if (item->proxy_path().isEmpty()) {
			qDebug()
				<< "  skipping item with empty proxy path" << item->filename();
			continue;
		}

		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(item));
		oakengine_footage_proxy_set_enabled(handle, enabled ? 1 : 0);
		oakengine_footage_free(handle);
		// The facade call toggles the flag; cache invalidation for the UI
		// stays here.
		item->invalidate_all(Footage::k_filename_input);
	}
}

void ProjectExplorer::reveal_proxy_for_selected_footage()
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(context_menu_items_);
	for (Footage *item : footage) {
		char proxy_path[4096];
		proxy_path[0] = '\0';
		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(item));
		oakengine_footage_proxy_get_path(handle, proxy_path,
										 sizeof(proxy_path));
		oakengine_footage_free(handle);
		if (proxy_path[0] == '\0') {
			continue;
		}
		const QString path = QString::fromUtf8(proxy_path);

#if defined(Q_OS_WINDOWS)
		QStringList args;
		args << "/select," << QDir::toNativeSeparators(path);
		QProcess::startDetached(QStringLiteral("explorer"), args);
#elif defined(Q_OS_MAC)
		QStringList args;
		args << "-e";
		args << "tell application \"Finder\"";
		args << "-e";
		args << "activate";
		args << "-e";
		args << "select POSIX file \"" + path + "\"";
		args << "-e";
		args << "end tell";
		QProcess::startDetached(QStringLiteral("osascript"), args);
#else
		QDesktopServices::openUrl(QUrl::fromLocalFile(
			QFileInfo(path).dir().absolutePath()));
#endif
	}
}

void ProjectExplorer::delete_proxies_for_selected_footage()
{
	const QVector<Footage *> footage =
		get_selected_proxy_footage(context_menu_items_);
	for (Footage *item : footage) {
		if (item->proxy_path().isEmpty()) {
			continue;
		}

		// Facade delete: removes the file, clears the proxy state and
		// invalidates the footage.
		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(item));
		oakengine_footage_proxy_delete(handle);
		oakengine_footage_free(handle);
	}
}

void ProjectExplorer::show_proxy_dialog_for_selected_footage()
{
	ProxyDialog d(this, get_selected_proxy_footage(context_menu_items_));
	d.exec();
}

void ProjectExplorer::view_selection_changed()
{
	QItemSelectionModel *model = static_cast<QItemSelectionModel *>(sender());

	QModelIndexList selection = model->selectedIndexes();

	QVector<Node *> nodes;

	foreach (const QModelIndex &index, selection) {
		Node *sel = static_cast<Node *>(
			sort_model_.mapToSource(index).internalPointer());
		if (!nodes.contains(sel)) {
			nodes.append(sel);
		}
	}

	if (nodes.isEmpty()) {
		nodes.append(get_root());
	}

	emit selection_changed(nodes);
}

Project *ProjectExplorer::project() const
{
	return model_.project();
}

void ProjectExplorer::set_project(Project *p)
{
	model_.set_project(p);
}

Folder *ProjectExplorer::get_root() const
{
	QModelIndex root_index = sort_model_.mapToSource(tree_view_->rootIndex());

	if (!root_index.isValid()) {
		return project()->root();
	}

	return static_cast<Folder *>(root_index.internalPointer());
}

void ProjectExplorer::set_root(Folder *item)
{
	QModelIndex index =
		sort_model_.mapFromSource(model_.create_index_from_item(item));

	browse_to_folder(index);
	tree_view_->setRootIndex(index);
}

QVector<Node *> ProjectExplorer::selected_items() const
{
	// Determine which view is active and get its selected indexes
	QModelIndexList index_list =
		current_view()->selectionModel()->selectedRows();

	// Convert indexes to item objects
	QVector<Node *> selected_items;

	for (int i = 0; i < index_list.size(); i++) {
		QModelIndex index = sort_model_.mapToSource(index_list.at(i));

		Node *item = static_cast<Node *>(index.internalPointer());

		selected_items.append(item);
	}

	return selected_items;
}

Folder *ProjectExplorer::get_selected_folder() const
{
	if (project() == nullptr) {
		return nullptr;
	}

	Folder *folder = nullptr;

	// Get the selected items from the panel
	QVector<Node *> selected_nodes = selected_items();

	// Heuristic for finding the selected folder:
	//
	// - If `folder` is nullptr, we set the first folder we find. Either the item itself if it's a folder, or the
	//   item's parent.
	// - Otherwise, if all folders found are the same, we'll use that to import into.
	// - If more than one folder is found, we play it safe and import into the root folder

	for (int i = 0; i < selected_nodes.size(); i++) {
		Node *sel_item = selected_nodes.at(i);

		// If this item is not a folder, presumably it's parent is
		if (!dynamic_cast<Folder *>(sel_item)) {
			sel_item = sel_item->folder();
		}

		if (folder == nullptr) {
			// If the folder is nullptr, cache it as this folder
			folder = static_cast<Folder *>(sel_item);
		} else if (folder != sel_item) {
			// If not, we've already cached a folder so we check if it's the same
			// If it isn't, we "play it safe" and use the root folder
			folder = nullptr;
			break;
		}
	}

	// If we didn't pick up a folder from the heuristic above for whatever reason, use root
	if (folder == nullptr) {
		folder = project()->root();
	}

	return folder;
}

ProjectViewModel *ProjectExplorer::model()
{
	return &model_;
}

void ProjectExplorer::select_all()
{
	current_view()->selectAll();
}

void ProjectExplorer::deselect_all()
{
	current_view()->selectionModel()->clearSelection();
}

void ProjectExplorer::delete_selected()
{
	QVector<Node *> selected = selected_items();

	if (selected.isEmpty()) {
		return;
	}

	MultiUndoCommand *command = new MultiUndoCommand();

	bool check_if_item_is_in_use = true;

	if (delete_items_internal(selected, check_if_item_is_in_use, command)) {
		Core::instance()->undo_stack()->push(
			command, tr("Deleted %1 Item(s)").arg(selected.size()));
	} else {
		delete command;
	}
}

bool ProjectExplorer::select_item(Node *n, bool deselect_all_first)
{
	if (deselect_all_first) {
		deselect_all();
	}

	QModelIndex index = model_.create_index_from_item(n);

	if (index.isValid()) {
		index = sort_model_.mapFromSource(index);

		QModelIndex parent = index.parent();
		if (view_type() == ProjectToolbar::tree_view) {
			// Expand all folders until this index is visible
			while (parent.isValid()) {
				tree_view_->expand(parent);
				parent = parent.parent();
			}
		} else {
			browse_to_folder(parent);
		}

		current_view()->selectionModel()->select(
			index, QItemSelectionModel::Select | QItemSelectionModel::Rows);

		return true;
	}

	return false;
}

}
