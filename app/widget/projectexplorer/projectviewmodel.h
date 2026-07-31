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

#ifndef OAK_VIEWMODEL_H
#define OAK_VIEWMODEL_H

#include <QAbstractItemModel>
#include <QHash>

#include "engineeventbridge.h"
#include "oakutil/oaknode.h"

namespace olive
{

/**
 * @brief An adapter that interprets the data in a Project into a Qt item model for usage in ViewModel Views.
 *
 * Assuming a Project is currently "open" (i.e. the Project is connected to a ProjectExplorer/ProjectPanel through
 * a ProjectViewModel), it may be better to make modifications (e.g. additions/removals/renames) through the
 * ProjectViewModel so that the views can be efficiently and correctly updated. ProjectViewModel contains several
 * "wrapper" functions for Project and Item functions that also signal any connected views to update accordingly.
 *
 * Engine access goes through the oak:: C++ wrapper layer (oakutil/oaknode.h),
 * which re-wraps the engine's pure C ABI into object form.
 */
class ProjectViewModel : public QAbstractItemModel {
	Q_OBJECT
public:
	enum ColumnType {
		/// Media name
		k_name,

		/// Media duration
		k_duration,

		/// Media rate (frame rate for video, sample rate for audio)
		k_rate,

		/// Last modified time (for footage/files)
		k_last_modified,

		/// Creation time (for footage/files)
		k_created_time,

		/// Count
		k_column_count
	};

	static const int k_inner_text_role = Qt::UserRole + 1;

	/**
   * @brief ProjectViewModel Constructor
   *
   * @param parent
   * Parent object for memory handling
   */
	ProjectViewModel(QObject *parent);

	/**
   * @brief Get currently active project
   *
   * @return
   *
   * Currently active project or a null handle if there is none
   */
	oak::Project project() const;

	/**
   * @brief Set the project to adapt
   *
   * Any views attached to this model will get updated by this function.
   *
   * @param p
   *
   * Project to adapt, can be set to null to "close" the project (will show an empty model that cannot be modified)
   */
	void set_project(oak::Project p);

	/** Compulsory Qt QAbstractItemModel overrides */
	virtual QModelIndex
	index(int row, int column,
		  const QModelIndex &parent = QModelIndex()) const override;
	virtual QModelIndex parent(const QModelIndex &child) const override;
	virtual int
	rowCount(const QModelIndex &parent = QModelIndex()) const override;
	virtual int
	columnCount(const QModelIndex &parent = QModelIndex()) const override;
	virtual QVariant data(const QModelIndex &index,
						  int role = Qt::DisplayRole) const override;

	/** Optional Qt QAbstractItemModel overrides */
	virtual QVariant headerData(int section, Qt::Orientation orientation,
								int role = Qt::DisplayRole) const override;
	virtual bool
	hasChildren(const QModelIndex &parent = QModelIndex()) const override;
	virtual bool setData(const QModelIndex &index, const QVariant &value,
						 int role = Qt::EditRole) override;
	virtual bool canFetchMore(const QModelIndex &parent) const override;

	/** Drag and drop support */
	virtual Qt::ItemFlags flags(const QModelIndex &index) const override;
	virtual QStringList mimeTypes() const override;
	virtual QMimeData *mimeData(const QModelIndexList &indexes) const override;
	virtual bool dropMimeData(const QMimeData *data, Qt::DropAction action,
							  int row, int column,
							  const QModelIndex &parent) override;

	/**
   * @brief Convenience function for creating QModelIndexes from an Item object
   */
	QModelIndex create_index_from_item(oak::Node item, int column = 0);

private:
	/**
   * @brief Retrieve the index of `item` in its parent
   *
   * This function will return the index of a specified item in its parent according to whichever sorting algorithm
   * is currently active.
   *
   * @return
   *
   * Index of the specified item, or -1 if the item is root (in which case it has no parent).
   */
	int index_of_child(oak::Node item) const;

	/**
   * @brief Retrieves the Item object from a given index
   *
   * A convenience function for retrieving Item objects. If the index is not valid, this returns the root Item.
   */
	oak::Node get_item_object_from_index(const QModelIndex &index) const;

	/**
   * @brief Check if an Item is a parent of a Child
   *
   * Checks entire "parent hierarchy" of `child` to see if `parent` is one of its parents.
   */
	bool item_is_parent_of_child(oak::Node parent, oak::Node child) const;

	void connect_item(oak::Node n);

	void disconnect_item(oak::Node n);

	/**
	 * @brief Wire the bridge's folder signals to our handlers.
	 *
	 * Must be re-run every time bridge_ is recreated (set_project), since
	 * Qt connections belong to the old bridge instance.
	 */
	void connect_bridge_signals();

	void folder_begin_insert_item(oak::Node folder, oak::Node n, int insert_index);

	void folder_end_insert_item();

	void folder_begin_remove_item(oak::Node folder, oak::Node n, int child_index);

	void folder_end_remove_item();

	oak::Project project_;

	EngineEventBridge *bridge_;

	QHash<oak::Node, int64_t> label_changed_subs_;

private slots:
	void item_renamed(OakEngineNode *source);
};

}

#endif // OAK_VIEWMODEL_H
