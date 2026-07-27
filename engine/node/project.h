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

#ifndef OAK_PROJECT_H
#define OAK_PROJECT_H

#include <memory>
#include <QObject>
#include <QUuid>

#include "node/output/viewer/viewer.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/color/colormanager/colormanager.h"

namespace olive
{

class NodeGroup;

/**
 * @brief A project instance containing all the data pertaining to the user's project
 *
 * A project instance uses a parent-child hierarchy of Item objects. Projects will usually contain the following:
 *
 * * Footage
 * * Sequences
 * * Folders
 * * Project Settings
 * * Window Layout
 */
class Project : public QObject {
	Q_OBJECT
public:
	enum CacheSetting {
		k_cache_use_default_location,
		k_cache_store_alongside_project,
		k_cache_custom_path
	};

	Project();

	virtual ~Project() override;

	/**
   * @brief Destructively destroys all nodes in the graph
   */
	void clear();

	/**
   * @brief Retrieve a complete list of the nodes belonging to this graph
   */
	const QVector<Node *> &nodes() const
	{
		return node_children_;
	}

	void initialize();

	SerializedData load(QXmlStreamReader *reader);
	void save(QXmlStreamWriter *writer) const;

	int get_number_of_contexts_node_is_in(Node *node,
									bool except_itself = false) const;

	QString name() const;

	const QString &filename() const;
	QString pretty_filename() const;
	void set_filename(const QString &s);

	Folder *root() const
	{
		return root_;
	}
	ColorManager *color_manager() const
	{
		return color_manager_;
	}

	bool is_modified() const
	{
		return is_modified_;
	}
	void set_modified(bool e);

	bool has_autorecovery_been_saved() const;
	void set_autorecovery_saved(bool e);

	bool is_new() const;

	QString get_cache_alongside_project_path() const;
	QString cache_path() const;

	const QUuid &get_uuid() const
	{
		return uuid_;
	}

	void set_uuid(const QUuid &uuid)
	{
		uuid_ = uuid;
	}

	void regenerate_uuid();

	/**
   * @brief Returns the filename the project was saved as, but not necessarily where it is now
   *
   * May help for resolving relative paths.
   */
	const QString &get_saved_url() const
	{
		return saved_url_;
	}

	void set_saved_url(const QString &url)
	{
		saved_url_ = url;
	}

	/**
   * @brief Find project parent from object
   *
   * If an object is expected to be a child of a project, this function will traverse its parent
   * tree until it finds it.
   */
	static Project *get_project_from_object(const QObject *o);

	static void copy_settings(Project *from, Project *to)
	{
		to->settings_ = from->settings_;
	}

	static const QString k_item_mime_type;

	static const QString k_cache_location_setting_key;
	static const QString k_cache_path_key;
	static const QString k_color_config_filename;
	static const QString k_color_reference_space;
	static const QString k_default_input_color_space_key;
	static const QString k_root_key;

	QString get_setting(const QString &key) const
	{
		return settings_.value(key);
	}
	void set_setting(const QString &key, const QString &value);

	CacheSetting get_cache_location_setting() const
	{
		return static_cast<CacheSetting>(
			get_setting(k_cache_location_setting_key).toInt());
	}
	void set_cache_location_setting(CacheSetting s)
	{
		set_setting(k_cache_location_setting_key, QString::number(s));
	}

	QString get_custom_cache_path() const
	{
		return get_setting(k_cache_path_key);
	}
	void set_custom_cache_path(const QString &path)
	{
		set_setting(k_cache_path_key, path);
	}

	QString get_color_config_filename() const
	{
		return get_setting(k_color_config_filename);
	}
	void set_color_config_filename(const QString &s)
	{
		set_setting(k_color_config_filename, s);
	}

	QString get_default_input_color_space() const
	{
		return get_setting(k_default_input_color_space_key);
	}
	void set_default_input_color_space(const QString &s)
	{
		set_setting(k_default_input_color_space_key, s);
	}

	QString get_color_reference_space() const
	{
		return get_setting(k_color_reference_space);
	}
	void set_color_reference_space(const QString &s)
	{
		set_setting(k_color_reference_space, s);
	}

signals:
	void name_changed();

	void modified_changed(bool e);

	/**
   * @brief Signal emitted when a Node is added to the graph
   */
	void node_added(Node *node);

	/**
   * @brief Signal emitted when a Node is removed from the graph
   */
	void node_removed(Node *node);

	void input_connected(Node *output, const NodeInput &input);

	void input_disconnected(Node *output, const NodeInput &input);

	void value_changed(const NodeInput &input);

	void input_value_hint_changed(const NodeInput &input);

	void group_added_input_passthrough(NodeGroup *group, const NodeInput &input);

	void group_removed_input_passthrough(NodeGroup *group, const NodeInput &input);

	void group_changed_output_passthrough(NodeGroup *group, Node *output);

	void setting_changed(const QString &key, const QString &value);

protected:
	virtual void childEvent(QChildEvent *event) override;

private:
	QUuid uuid_;

	Folder *root_;

	QString filename_;

	QString saved_url_;

	bool is_modified_;

	bool autorecovery_saved_;

	ColorManager *color_manager_;

	QVector<Node *> node_children_;

	QMap<QString, QString> settings_;
};

}

#endif // OAK_PROJECT_H
