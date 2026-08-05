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

#ifndef OAK_PROJECTCOPIER_H
#define OAK_PROJECTCOPIER_H

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/jobtime.h"
#include "project.h"
#include "project/footage/footage.h"

namespace olive
{

class ProjectCopier {
public:
	ProjectCopier();

	~ProjectCopier()
	{
		delete copy_;
	}

	void set_project(Project *project);

	template <typename T> T *get_copy(T *original)
	{
		auto it = copy_map_.find(original);
		return it != copy_map_.end() ? static_cast<T *>(it->second) : nullptr;
	}

	template <typename T> T *get_original(T *copy)
	{
		for (const auto &e : copy_map_) {
			if (e.second == copy) {
				return static_cast<T *>(e.first);
			}
		}
		return nullptr;
	}

	Project *get_copied_project() const
	{
		return copy_;
	}

	const std::unordered_map<Node *, Node *> &get_node_map() const
	{
		return copy_map_;
	}

	const JobTime &get_graph_change_time() const
	{
		return graph_changed_time_;
	}
	const JobTime &get_last_update_time() const
	{
		return last_update_time_;
	}

	bool has_updates_in_queue() const
	{
		return !graph_update_queue_.empty();
	}

	/**
   * @brief Process all changes to internal NodeGraph copy
   *
   * PreviewAutoCacher staggers updates to its internal NodeGraph copy, only applying them when the
   * RenderManager is not reading from it. This function is called when such an opportunity arises.
   */
	void process_update_queue();

	// Explicit intra-module callbacks replacing the `added_node` /
	// `removed_node` signals (subscriber: PreviewAutoCacher)
	void set_added_node_handler(std::function<void(Node *)> handler)
	{
		added_node_handler_ = std::move(handler);
	}
	void set_removed_node_handler(std::function<void(Node *)> handler)
	{
		removed_node_handler_ = std::move(handler);
	}

	// Formerly slots connected to Project's (now removed) signals. Kept as
	// public methods; the facade / project wave wires the notifications to
	// these entry points.
	void queue_node_add(Node *node);

	void queue_node_remove(Node *node);

	void queue_edge_add(Node *output, const NodeInput &input);

	void queue_edge_remove(Node *output, const NodeInput &input);

	void queue_value_change(const NodeInput &input);

	void queue_value_hint_change(const NodeInput &input);

	void queue_project_setting_change(const std::string &key,
									  const std::string &value);

	// Formerly connected to Footage::proxy_settings_changed (signal removed);
	// must now be called by whoever owns that notification (facade wave).
	void sync_footage_proxy_settings(Footage *source);

private:
	void do_node_add(Node *node);
	void do_node_remove(Node *node);
	void do_edge_add(Node *output, const NodeInput &input);
	void do_edge_remove(Node *output, const NodeInput &input);
	void do_value_change(const NodeInput &input);
	void do_value_hint_change(const NodeInput &input);
	void do_project_setting_change(const std::string &key,
								   const std::string &value);

	void insert_into_copy_map(Node *node, Node *copy);

	void update_graph_change_value();
	void update_last_synced_value();

	Project *original_;
	Project *copy_;

	class QueuedJob {
	public:
		enum Type {
			k_node_added,
			k_node_removed,
			k_edge_added,
			k_edge_removed,
			k_value_changed,
			k_value_hint_changed,
			k_project_setting_changed
		};

		Type type;
		Node *node;
		NodeInput input;
		Node *output;

		std::string key;
		std::string value;
	};

	std::list<QueuedJob> graph_update_queue_;
	std::unordered_map<Node *, Node *> copy_map_;
	std::vector<Node *> created_nodes_;

	JobTime graph_changed_time_;
	JobTime last_update_time_;

	std::function<void(Node *)> added_node_handler_;
	std::function<void(Node *)> removed_node_handler_;
};

}

#endif // OAK_PROJECTCOPIER_H
