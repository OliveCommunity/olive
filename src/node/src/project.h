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

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "output/viewer/viewer.h"
#include "project/folder/folder.h"
#include "project/footage/footage.h"
#include "project/sequence/sequence.h"
#include "color/colormanager/colormanager.h"

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
class Project {
public:
	enum CacheSetting {
		k_cache_use_default_location,
		k_cache_store_alongside_project,
		k_cache_custom_path
	};

	Project();

	virtual ~Project();

	/**
   * @brief Destructively destroys all nodes in the graph
   */
	void clear();

	/**
   * @brief Retrieve a complete list of the nodes belonging to this graph
   */
	const std::vector<Node *> &nodes() const
	{
		return node_children_;
	}

	void initialize();

	SerializedData load(XmlStreamReader *reader);
	void save(XmlStreamWriter *writer) const;

	int get_number_of_contexts_node_is_in(Node *node,
									bool except_itself = false) const;

	std::string name() const;

	const std::string &filename() const;
	std::string pretty_filename() const;
	void set_filename(const std::string &s);

	Folder *root() const
	{
		return root_;
	}
	ColorManager *color_manager() const
	{
		return color_manager_.get();
	}

	bool is_modified() const
	{
		return is_modified_;
	}
	void set_modified(bool e);

	/**
	 * @brief True while clear()/teardown is deleting all nodes.
	 *
	 * Nodes use this to suppress cache invalidation storms while the graph
	 * is being torn down: invalidating during destruction walks edges into
	 * half-destroyed nodes and has repeatedly caused use-after-free crashes.
	 */
	bool is_being_cleared() const
	{
		return is_being_cleared_;
	}

	bool has_autorecovery_been_saved() const;
	void set_autorecovery_saved(bool e);

	bool is_new() const;

	std::string get_cache_alongside_project_path() const;
	std::string cache_path() const;

	const std::string &get_uuid() const
	{
		return uuid_;
	}

	void set_uuid(const std::string &uuid)
	{
		uuid_ = uuid;
	}

	void regenerate_uuid();

	/**
   * @brief Returns the filename the project was saved as, but not necessarily where it is now
   *
   * May help for resolving relative paths.
   */
	const std::string &get_saved_url() const
	{
		return saved_url_;
	}

	void set_saved_url(const std::string &url)
	{
		saved_url_ = url;
	}

	/**
	 * @brief Add a node to this graph (replaces QObject::setParent(project))
	 *
	 * The project takes ownership of the node: it is deleted by clear()
	 * or by the project destructor. Use remove_node() to detach a node
	 * without deleting it.
	 */
	void add_node(Node *node);

	/**
	 * @brief Detach a node from this graph without deleting it
	 */
	void remove_node(Node *node);

	/**
   * @brief Find project parent from object
   *
   * Nodes now store their graph directly (Node::project()), so this simply
   * returns the graph the node belongs to.
   */
	static Project *get_project_from_object(const Node *o)
	{
		return o ? o->project() : nullptr;
	}

	static void copy_settings(Project *from, Project *to)
	{
		to->settings_ = from->settings_;
	}

	static const std::string k_item_mime_type;

	static const std::string k_cache_location_setting_key;
	static const std::string k_cache_path_key;
	static const std::string k_color_config_filename;
	static const std::string k_color_reference_space;
	static const std::string k_default_input_color_space_key;
	static const std::string k_root_key;

	std::string get_setting(const std::string &key) const
	{
		auto it = settings_.find(key);
		return it != settings_.end() ? it->second : std::string();
	}
	void set_setting(const std::string &key, const std::string &value);

	CacheSetting get_cache_location_setting() const
	{
		return static_cast<CacheSetting>(
			atoi(get_setting(k_cache_location_setting_key).c_str()));
	}
	void set_cache_location_setting(CacheSetting s)
	{
		set_setting(k_cache_location_setting_key, std::to_string(s));
	}

	std::string get_custom_cache_path() const
	{
		return get_setting(k_cache_path_key);
	}
	void set_custom_cache_path(const std::string &path)
	{
		set_setting(k_cache_path_key, path);
	}

	std::string get_color_config_filename() const
	{
		return get_setting(k_color_config_filename);
	}
	void set_color_config_filename(const std::string &s)
	{
		set_setting(k_color_config_filename, s);
	}

	std::string get_default_input_color_space() const
	{
		return get_setting(k_default_input_color_space_key);
	}
	void set_default_input_color_space(const std::string &s)
	{
		set_setting(k_default_input_color_space_key, s);
	}

	std::string get_color_reference_space() const
	{
		return get_setting(k_color_reference_space);
	}
	void set_color_reference_space(const std::string &s)
	{
		set_setting(k_color_reference_space, s);
	}

private:
	std::string uuid_;

	Folder *root_;

	std::string filename_;

	std::string saved_url_;

	bool is_modified_;

	bool autorecovery_saved_;

	bool is_being_cleared_ = false;

	// Owned directly (ColorManager used to be a QObject child of this)
	std::unique_ptr<ColorManager> color_manager_;

	std::vector<Node *> node_children_;

	std::map<std::string, std::string> settings_;
};

}

#endif // OAK_PROJECT_H
