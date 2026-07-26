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

#include "projectviewmodel.h"
#include "ui/icons/icons.h"

#include <QDebug>
#include <QMimeData>
#include <QUrl>

#include "common/qtutils.h"
#include "core.h"
#include "oakengine/project.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"

namespace olive
{

ProjectViewModel::ProjectViewModel(QObject *parent)
	: QAbstractItemModel(parent)
	, project_(nullptr)
	, bridge_(new EngineEventBridge(this))
{
	connect_bridge_signals();
}

void ProjectViewModel::connect_bridge_signals()
{
	connect(bridge_, &EngineEventBridge::folder_begin_insert_item, this,
			[this](OakEngineNode *folder, OakEngineNode *child, int index) {
				this->folder_begin_insert_item(
					reinterpret_cast<Folder *>(folder),
					reinterpret_cast<Node *>(child), index);
			});
	connect(bridge_, &EngineEventBridge::folder_end_insert_item, this,
			[this](OakEngineNode *) {
				this->folder_end_insert_item();
			});
	connect(bridge_, &EngineEventBridge::folder_begin_remove_item, this,
			[this](OakEngineNode *folder, OakEngineNode *child, int index) {
				this->folder_begin_remove_item(
					reinterpret_cast<Folder *>(folder),
					reinterpret_cast<Node *>(child), index);
			});
	connect(bridge_, &EngineEventBridge::folder_end_remove_item, this,
			[this](OakEngineNode *) {
				this->folder_end_remove_item();
			});
	connect(bridge_, &EngineEventBridge::node_label_changed, this,
			&ProjectViewModel::item_renamed);
}

Project *ProjectViewModel::project() const
{
	return project_;
}

void ProjectViewModel::set_project(Project *p)
{
	beginResetModel();

	if (project_) {
		disconnect_item(project_->root());
		// Recreate bridge to clear all folder subscriptions
		delete bridge_;
		bridge_ = new EngineEventBridge(this);
		connect_bridge_signals();
	}

	project_ = p;

	if (project_) {
		connect_item(project_->root());
	}

	endResetModel();
}

QModelIndex ProjectViewModel::index(int row, int column,
									const QModelIndex &parent) const
{
	// I'm actually not 100% sure what this does, but it seems logical and was in the earlier code
	if (!hasIndex(row, column, parent)) {
		return QModelIndex();
	}

	// Get the parent object, we assume it's a folder since only folders can have children
	Folder *item_parent = static_cast<Folder *>(get_item_object_from_index(parent));

	// Return an index to this object
	return createIndex(row, column, item_parent->item_child(row));
}

QModelIndex ProjectViewModel::parent(const QModelIndex &child) const
{
	// Get the Item object from the index
	Node *item = get_item_object_from_index(child);

	// Get Item's parent object
	Folder *par = item->folder();

	// If the parent is the root, return an empty index
	if (par == project_->root()) {
		return QModelIndex();
	}

	// Otherwise return a true index to its parent
	int parent_index = index_of_child(par);

	// Make sure the index is valid (there's no reason it shouldn't be)
	Q_ASSERT(parent_index > -1);

	// Return an index to the parent
	return createIndex(parent_index, 0, par);
}

int ProjectViewModel::rowCount(const QModelIndex &parent) const
{
	// If there's no project, there are obviously no items to show
	if (project_ == nullptr) {
		return 0;
	}

	// If the index is the root, return the root child count
	if (parent == QModelIndex()) {
		return project_->root()->item_child_count();
	}

	// Otherwise, the index must contain a valid pointer, so we just return its child count
	return static_cast<Folder *>(get_item_object_from_index(parent))
		->item_child_count();
}

int ProjectViewModel::columnCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent)

	// Not strictly necessary, but a decent visual cue that there's no project currently active
	if (project_ == nullptr) {
		return 0;
	}

	return k_column_count;
}

QVariant ProjectViewModel::data(const QModelIndex &index, int role) const
{
	Node *internal_item = get_item_object_from_index(index);

	ColumnType column_type = static_cast<ColumnType>(index.column());

	switch (role) {
	case Qt::DisplayRole:
	case k_inner_text_role: {
		// Standard text role

		switch (column_type) {
		case k_name:
			return internal_item->get_label();
		case k_duration:
			return internal_item->data(Node::duration);
		case k_rate:
			return internal_item->data(Node::frequency_rate);
		case k_last_modified:
		case k_created_time: {
			qint64 using_time =
				(column_type == k_last_modified) ?
					internal_item->data(Node::modified_time).toLongLong() :
					internal_item->data(Node::created_time).toLongLong();

			if (using_time == 0) {
				// 0 is the null value, return nothing
				break;
			}

			QVariant ret;

			if (role == k_inner_text_role) {
				// Use time value directly for correct sorting
				ret = using_time;
			} else {
				// Display role, format to a human readable string
				ret = QtUtils::get_formatted_date_time(
					QDateTime::fromSecsSinceEpoch(using_time));
			}

			return ret;
		}
		case k_column_count:
			break;
		}
	} break;
	case Qt::EditRole:
		if (column_type == k_name) {
			return internal_item->get_label();
		}
		break;
	case Qt::DecorationRole:
		// If this is the first column, return the Item's icon
		if (column_type == k_name) {
			return icon::from_name(internal_item->data(Node::icon).toString());
		}
		break;
	case Qt::ToolTipRole:
		return internal_item->data(Node::tooltip);
	}

	return QVariant();
}

QVariant ProjectViewModel::headerData(int section, Qt::Orientation orientation,
									  int role) const
{
	// Check if we need text data (DisplayRole) and orientation is horizontal
	// FIXME I'm not 100% sure what happens if the orientation is vertical/if that check is necessary
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
		ColumnType column_type = static_cast<ColumnType>(section);

		// Return the name based on the column's current type
		switch (column_type) {
		case k_name:
			return tr("Name");
		case k_duration:
			return tr("Duration");
		case k_rate:
			return tr("Rate");
		case k_last_modified:
			return tr("Modified");
		case k_created_time:
			return tr("Created");
		case k_column_count:
			break;
		}
	}

	return QAbstractItemModel::headerData(section, orientation, role);
}

bool ProjectViewModel::hasChildren(const QModelIndex &parent) const
{
	// If it's a folder, we always return TRUE in order to always show the "expand triangle" icon,
	// even when there are no "physical" children
	Node *item = get_item_object_from_index(parent);

	return dynamic_cast<Folder *>(item);
}

bool ProjectViewModel::setData(const QModelIndex &index, const QVariant &value,
							   int role)
{
	// The name is editable
	if (index.isValid() && index.column() == k_name && role == Qt::EditRole) {
		Node *item = get_item_object_from_index(index);

		QString new_name = value.toString();

		if (!new_name.isEmpty()) {
			void *nrc = oakengine_node_rename_command(
				reinterpret_cast<OakEngineNode *>(item),
				new_name.toUtf8().constData());

			oakengine_undo_push(
				nrc,
				tr("Renamed Item \"%1\" to \"%2\"").arg(item->get_label(), new_name).toUtf8().constData());

			return true;
		}
	}

	return false;
}

bool ProjectViewModel::canFetchMore(const QModelIndex &parent) const
{
	// Use the same hack that always returns true with folders so the expand triangle is always visible
	return hasChildren(parent);
}

Qt::ItemFlags ProjectViewModel::flags(const QModelIndex &index) const
{
	if (!index.isValid()) {
		// Allow dropping files from external sources
		return Qt::ItemIsDropEnabled;
	}

	Qt::ItemFlags f = Qt::ItemIsDragEnabled | QAbstractItemModel::flags(index);

	if (dynamic_cast<Folder *>(get_item_object_from_index(index))) {
		f |= Qt::ItemIsDropEnabled;
	}

	// If the column is the kName column, that means it's editable
	if (index.column() == k_name) {
		f |= Qt::ItemIsEditable;
	}

	return f;
}

QStringList ProjectViewModel::mimeTypes() const
{
	// Allow data from this model and a file list from external sources
	return { QString::fromUtf8(oakengine_project_item_mime_type()), QStringLiteral("text/uri-list") };
}

QMimeData *ProjectViewModel::mimeData(const QModelIndexList &indexes) const
{
	// Compliance with Qt standard
	if (indexes.isEmpty()) {
		return nullptr;
	}

	// Encode mime data for the rows/items that were dragged
	QMimeData *data = new QMimeData();

	// Use QDataStream to stream the item data into a byte array
	QByteArray encoded_data;
	QDataStream stream(&encoded_data, QIODevice::WriteOnly);

	// The indexes list includes indexes for each column which we don't use. To make sure each row only gets sent *once*,
	// we keep a list of dragged items
	QVector<void *> dragged_items;

	foreach (QModelIndex index, indexes) {
		if (index.isValid()) {
			// Check if we've dragged this item before
			if (!dragged_items.contains(index.internalPointer())) {
				// If not, add it to the stream (and also keep track of it in the vector)
				Node *item = static_cast<Node *>(index.internalPointer());
				QVector<Track::Reference> streams;

				if (ViewerOutput *footage =
						dynamic_cast<ViewerOutput *>(item)) {
					streams = footage->get_enabled_streams_as_references();
				}

				stream << streams << reinterpret_cast<quintptr>(item);

				dragged_items.append(item);
			}
		}
	}

	// Set byte array as the mime data and return the mime data
	data->setData(QString::fromUtf8(oakengine_project_item_mime_type()), encoded_data);

	return data;
}

bool ProjectViewModel::dropMimeData(const QMimeData *data,
									Qt::DropAction action, int row, int column,
									const QModelIndex &drop)
{
	// Default recommended checks from https://doc.qt.io/qt-5/model-view-programming.html#using-drag-and-drop-with-item-views
	if (!canDropMimeData(data, action, row, column, drop)) {
		return false;
	}

	if (action == Qt::IgnoreAction) {
		return true;
	}

	// Probe mime data for its format
	QStringList mime_formats = data->formats();

	if (mime_formats.contains(QString::fromUtf8(oakengine_project_item_mime_type()))) {
		// Data is drag/drop data from this model
		QByteArray model_data = data->data(QString::fromUtf8(oakengine_project_item_mime_type()));

		// Use QDataStream to deserialize the data
		QDataStream stream(&model_data, QIODevice::ReadOnly);

		// Get the Item object that the items were dropped on
		Folder *drop_location =
			dynamic_cast<Folder *>(get_item_object_from_index(drop));

		// If this is not a folder, we cannot drop these items here
		if (!drop_location) {
			return false;
		}

		// Variables to deserialize into
		quintptr item_ptr;
		QList<Track::Reference> streams;

		// Loop through all data, collecting the items to move
		QVector<OakEngineNode *> items_to_move;

		while (!stream.atEnd()) {
			stream >> streams >> item_ptr;

			Node *item = reinterpret_cast<Node *>(item_ptr);

			// Check if Item is already the drop location or if its parent is the drop location, in which case this is a
			// no-op

			if (item != drop_location && item->folder() != drop_location &&
				(!dynamic_cast<Folder *>(item) ||
				 !item_is_parent_of_child(static_cast<Folder *>(item),
									  drop_location))) {
				items_to_move.append(reinterpret_cast<OakEngineNode *>(item));
			}
		}

		if (!items_to_move.isEmpty()) {
			// ONE undoable command for the whole move (facade removes each
			// item from its old folder, then adds it to the drop location)
			oakengine_folder_move_children(
				items_to_move.constData(), items_to_move.size(),
				reinterpret_cast<OakEngineNode *>(drop_location),
				tr("Move %1 Item(s)").arg(items_to_move.size()).toUtf8().constData());
		}

		return true;

	} else if (mime_formats.contains(QStringLiteral("text/uri-list"))) {
		// We received a list of files
		QByteArray file_data = data->data(QStringLiteral("text/uri-list"));

		// Use text stream to parse (just an easy way of sifting through line breaks
		QTextStream stream(&file_data);

		// Convert QByteArray to QStringList (which Core takes for importing)
		QStringList urls;
		while (!stream.atEnd()) {
			QUrl url = stream.readLine();

			if (!url.isEmpty()) {
				urls.append(url.toLocalFile());
			}
		}

		// Get folder dropped onto
		Node *drop_item = get_item_object_from_index(drop);

		// If we didn't drop onto an item, find the nearest parent folder (should eventually terminate at root either way)
		if (!dynamic_cast<Folder *>(drop_item)) {
			drop_item = drop_item->folder();

			if (!drop_item) {
				// Failed to find folder to place this in
				return false;
			}
		}

		// Trigger an import
		Core::instance()->import_files(urls, static_cast<Folder *>(drop_item));

		return true;
	}

	return false;
}

int ProjectViewModel::index_of_child(Node *item) const
{
	// Find parent's index within its own parent
	Folder *parent = item->folder();

	if (parent) {
		return parent->index_of_child(item);
	}

	return -1;
}

Node *ProjectViewModel::get_item_object_from_index(const QModelIndex &index) const
{
	if (index.isValid()) {
		return static_cast<Node *>(index.internalPointer());
	}

	return project_ ? project_->root() : nullptr;
}

bool ProjectViewModel::item_is_parent_of_child(Folder *parent, Node *child) const
{
	// Loop through parent hierarchy checking if `parent` is one of its parents
	do {
		child = child->folder();

		if (parent == child) {
			return true;
		}
	} while (child != nullptr);

	return false;
}

void ProjectViewModel::connect_item(Node *n)
{
	label_changed_subs_[n] = bridge_->subscribe(
		reinterpret_cast<void *>(n), OAKENGINE_EVENT_NODE_LABEL_CHANGED);

	Folder *f = dynamic_cast<Folder *>(n);
	if (f) {
		OakEngineNode *handle = reinterpret_cast<OakEngineNode *>(f);
		bridge_->subscribe(handle, OAKENGINE_EVENT_FOLDER_BEGIN_INSERT_ITEM);
		bridge_->subscribe(handle, OAKENGINE_EVENT_FOLDER_END_INSERT_ITEM);
		bridge_->subscribe(handle, OAKENGINE_EVENT_FOLDER_BEGIN_REMOVE_ITEM);
		bridge_->subscribe(handle, OAKENGINE_EVENT_FOLDER_END_REMOVE_ITEM);

		foreach (Node *c, f->children()) {
			connect_item(c);
		}
	}
}

void ProjectViewModel::disconnect_item(Node *n)
{
	int64_t sub = label_changed_subs_.take(n);
	if (sub > 0) {
		bridge_->unsubscribe(sub);
	}

	Folder *f = dynamic_cast<Folder *>(n);
	if (f) {
		// Bridge subscriptions are cleaned up by recreating the bridge in set_project
		foreach (Node *c, f->children()) {
			disconnect_item(c);
		}
	}
}

void ProjectViewModel::folder_begin_insert_item(Folder *folder, Node *n,
												int insert_index)
{
	connect_item(n);

	QModelIndex index;

	if (folder != project_->root()) {
		index = create_index_from_item(folder);
	}

	beginInsertRows(index, insert_index, insert_index);
}

void ProjectViewModel::folder_end_insert_item()
{
	endInsertRows();
}

void ProjectViewModel::folder_begin_remove_item(Folder *folder, Node *n,
												int child_index)
{
	disconnect_item(n);

	QModelIndex index;

	if (folder != project_->root()) {
		index = create_index_from_item(folder);
	}

	beginRemoveRows(index, child_index, child_index);
}

void ProjectViewModel::folder_end_remove_item()
{
	endRemoveRows();
}

void ProjectViewModel::item_renamed(OakEngineNode *source)
{
	Node *item = reinterpret_cast<Node *>(source);

	QModelIndex index = create_index_from_item(item);

	emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole });
}

QModelIndex ProjectViewModel::create_index_from_item(Node *item, int column)
{
	return createIndex(index_of_child(item), column, item);
}

}
