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

#include "projectcopier.h"

#include "group/group.h"
#include "project/footage/footage.h"

namespace olive
{

ProjectCopier::ProjectCopier()
{
	original_ = nullptr;
	copy_ = new Project();
}

void ProjectCopier::set_project(Project *project)
{
	if (original_) {
		// Clear current project
		for (Node *n : created_nodes_) {
			copy_->remove_node(n);
			delete n;
		}
		created_nodes_.clear();
		copy_map_.clear();
		graph_update_queue_.clear();

		// The Project signal connections (node_added etc.) were removed with
		// QObject; there is nothing to disconnect, the facade stops calling
		// the queue_* entry points when it detaches this copier.
	}

	original_ = project;

	if (original_) {
		// NOTE: the QObject `destroyed` connection that nulled `original_` is
		// gone. The owner (RenderManager/PreviewAutoCacher via the facade)
		// must call set_project(nullptr) before destroying the project.

		// Add all nodes
		for (size_t i = 0; i < copy_->nodes().size(); i++) {
			insert_into_copy_map(original_->nodes().at(i), copy_->nodes().at(i));
		}

		for (size_t i = copy_->nodes().size(); i < original_->nodes().size();
			 i++) {
			do_node_add(original_->nodes().at(i));
		}

		// Add all connections
		for (Node *node : original_->nodes()) {
			for (auto it = node->input_connections().cbegin();
				 it != node->input_connections().cend(); it++) {
				do_edge_add(it->second, it->first);
			}
		}

		// Copy project settings
		Project::copy_settings(original_, copy_);

		// The copied project is only used as an in-memory render proxy. Mark it so
		// downstream code (e.g. RenderWorkerPool) knows it is safe to reset its
		// modified flag after serializing a snapshot. (Was the QObject dynamic
		// property "_oak_render_proxy"; stored as a project setting now. Set
		// after copy_settings() because that replaces the whole settings map.)
		copy_->set_setting("_oak_render_proxy", "1");

		// Ensure graph change value is just before the sync value
		update_graph_change_value();
		update_last_synced_value();

		// The Project signal connections for future node additions/deletions
		// were removed with QObject; the facade calls the queue_* entry
		// points directly.
	}
}

void ProjectCopier::process_update_queue()
{
	bool copy_changed = false;

	// Iterate everything that happened to the graph and do the same thing on our end
	while (!graph_update_queue_.empty()) {
		QueuedJob job = graph_update_queue_.front();
		graph_update_queue_.pop_front();
		copy_changed = true;

		switch (job.type) {
		case QueuedJob::k_node_added:
			do_node_add(job.node);
			break;
		case QueuedJob::k_node_removed:
			do_node_remove(job.node);
			break;
		case QueuedJob::k_edge_added:
			do_edge_add(job.output, job.input);
			break;
		case QueuedJob::k_edge_removed:
			do_edge_remove(job.output, job.input);
			break;
		case QueuedJob::k_value_changed:
			do_value_change(job.input);
			break;
		case QueuedJob::k_value_hint_changed:
			do_value_hint_change(job.input);
			break;
		case QueuedJob::k_project_setting_changed:
			do_project_setting_change(job.key, job.value);
			break;
		}
	}

	// The copied project is not saved, so its modified flag is only used by the
	// render worker pool to decide whether the serialized graph snapshot is stale.
	// Mark it modified whenever the copy has actually changed.
	if (copy_changed) {
		copy_->set_modified(true);
	}

	// Indicate that we have synchronized to this point, which is compared with the graph change
	// time to see if our copied graph is up to date
	update_last_synced_value();
}

void ProjectCopier::do_node_add(Node *node)
{
	if (dynamic_cast<NodeGroup *>(node)) {
		// Group nodes are just dummy nodes, no need to copy them
		return;
	}

	// Copy node
	Node *copy = node->copy();

	// Add to project (takes ownership, was QObject parentship)
	copy_->add_node(copy);

	// Disable caches for copy
	copy->set_caches_enabled(false);

	// Copy cache UUIDs
	copy->copy_cache_uuids_from(node);

	// Insert into map
	insert_into_copy_map(node, copy);

	// Keep track of our nodes
	created_nodes_.push_back(copy);
}

void ProjectCopier::do_node_remove(Node *node)
{
	// Find our copy and remove it
	Node *copy = nullptr;
	auto it = copy_map_.find(node);
	if (it != copy_map_.end()) {
		copy = it->second;
		copy_map_.erase(it);
	}

	// Disconnect from node's caches
	if (removed_node_handler_) {
		removed_node_handler_(node);
	}

	// Remove from created list
	auto created_it =
		std::find(created_nodes_.begin(), created_nodes_.end(), copy);
	if (created_it != created_nodes_.end()) {
		created_nodes_.erase(created_it);
	}

	// Detach from the owning project, then delete it
	if (copy) {
		copy_->remove_node(copy);
	}
	delete copy;
}

void ProjectCopier::do_edge_add(Node *output, const NodeInput &input)
{
	// Create same connection with our copied graph
	Node *our_output = get_copy(output);
	Node *our_input = get_copy(input.node());

	Node::connect_edge(our_output,
					   NodeInput(our_input, input.input(), input.element()));
}

void ProjectCopier::do_edge_remove(Node *output, const NodeInput &input)
{
	// Remove same connection with our copied graph
	Node *our_output = get_copy(output);
	Node *our_input = get_copy(input.node());

	Node::disconnect_edge(our_output,
						  NodeInput(our_input, input.input(), input.element()));
}

void ProjectCopier::do_value_change(const NodeInput &input)
{
	if (dynamic_cast<NodeGroup *>(input.node())) {
		// Group nodes are just dummy nodes, no need to copy them
		return;
	}

	// Copy all values to our graph
	Node *our_input = get_copy(input.node());
	Node::copy_values_of_element(input.node(), our_input, input.input(),
								 input.element());
}

void ProjectCopier::do_value_hint_change(const NodeInput &input)
{
	if (dynamic_cast<NodeGroup *>(input.node())) {
		// Group nodes are just dummy nodes, no need to copy them
		return;
	}

	// Copy value hint to our graph
	Node *our_input = get_copy(input.node());
	Node::ValueHint hint =
		input.node()->get_value_hint_for_input(input.input(), input.element());
	our_input->set_value_hint_for_input(input.input(), hint, input.element());
}

void ProjectCopier::do_project_setting_change(const std::string &key,
											  const std::string &value)
{
	copy_->set_setting(key, value);
}

void ProjectCopier::insert_into_copy_map(Node *node, Node *copy)
{
	// Insert into map
	copy_map_.insert({ node, copy });

	// Copy parameters
	Node::copy_inputs(node, copy, false);

	// Sync Footage proxy state (which is not stored as a Node input)
	if (Footage *src_footage = dynamic_cast<Footage *>(node)) {
		if (dynamic_cast<Footage *>(copy)) {
			// The Footage::proxy_settings_changed signal was removed with
			// QObject; the facade calls sync_footage_proxy_settings() when
			// proxy settings change.
			sync_footage_proxy_settings(src_footage);
		}
	}

	// Connect to node's cache
	if (added_node_handler_) {
		added_node_handler_(node);
	}
}

void ProjectCopier::sync_footage_proxy_settings(Footage *source)
{
	Footage *copy = get_copy(source);
	if (!copy) {
		fprintf(stderr,
				"ProjectCopier::SyncFootageProxySettings: no copy for %s\n",
				source->filename().c_str());
		return;
	}

	fprintf(stderr, "ProjectCopier::SyncFootageProxySettings: %s enabled=%d->%d "
					"state=%s\n",
			source->filename().c_str(), source->proxy_enabled(),
			copy->proxy_enabled(),
			ProxyManager::proxy_state_to_string(source->proxy_state()).c_str());

	copy->set_proxy(source->proxy_path(), source->proxy_state(),
					source->proxy_video_stream_index(),
					source->proxy_preset_version(), source->proxy_enabled());

	if (Project *cp = copy->project()) {
		cp->set_modified(true);
	}
}

void ProjectCopier::queue_node_add(Node *node)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_node_added, node, NodeInput(), nullptr, std::string(),
		  std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_node_remove(Node *node)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_node_removed, node, NodeInput(), nullptr, std::string(),
		  std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_edge_add(Node *output, const NodeInput &input)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_edge_added, nullptr, input, output, std::string(),
		  std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_edge_remove(Node *output, const NodeInput &input)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_edge_removed, nullptr, input, output, std::string(),
		  std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_value_change(const NodeInput &input)
{
	/*for (auto it = graph_update_queue_.begin(); it != graph_update_queue_.end(); ) {
    if (it->type == QueuedJob::kValueChanged && it->input == input) {
      it = graph_update_queue_.erase(it);
    } else {
      it++;
    }
  }*/

	graph_update_queue_.push_back(
		{ QueuedJob::k_value_changed, nullptr, input, nullptr, std::string(),
		  std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_value_hint_change(const NodeInput &input)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_value_hint_changed, nullptr, input, nullptr,
		  std::string(), std::string() });
	update_graph_change_value();
}

void ProjectCopier::queue_project_setting_change(const std::string &key,
												 const std::string &value)
{
	graph_update_queue_.push_back(
		{ QueuedJob::k_project_setting_changed, nullptr, NodeInput(), nullptr,
		  key, value });
	update_graph_change_value();
}

void ProjectCopier::update_graph_change_value()
{
	graph_changed_time_.acquire();
}

void ProjectCopier::update_last_synced_value()
{
	last_update_time_.acquire();
}

}
