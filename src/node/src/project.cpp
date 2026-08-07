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

#include "project.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <set>

#include "current.h"
#include "render/manager.h"
#include "xmlutils.h"
#include "color/ociobase/ociobase.h"
#include "factory.h"
#include "group/group.h"
#include "project/folder/folder.h"
#include "serializeddata.h"
#include "pluginSupport/olivehost.h"
#include "ofxhPluginCache.h"
#include "render/diskmanager.h"

namespace olive
{

const std::string Project::k_cache_location_setting_key = "cachesetting";
const std::string Project::k_cache_path_key = "customcachepath";
const std::string Project::k_color_config_filename = "colorconfigfilename";
const std::string Project::k_default_input_color_space_key =
	"defaultinputcolorspace";
const std::string Project::k_color_reference_space = "colorreferencespace";
const std::string Project::k_root_key = "root";

const std::string Project::k_item_mime_type =
	"application/x-oliveprojectitemdata";

Project::Project()
	: root_(nullptr)
	, is_modified_(false)
	, autorecovery_saved_(true)
{
	// Generate UUID for this project
	regenerate_uuid();

	// Initialize color manager (owned via unique_ptr; it used to be a
	// QObject child of this Project)
	color_manager_ = std::make_unique<ColorManager>(this);
	color_manager_->init();
}

Project::~Project()
{
	clear();
}

void Project::initialize()
{
	if (!root_) {
		root_ = new Folder();
		add_node(root_);
		root_->set_label("Root");
		settings_.insert_or_assign(
			k_root_key,
			std::to_string(reinterpret_cast<uintptr_t>(root_)));
	}
}

void Project::clear()
{
	is_being_cleared_ = true;

	// Notify each node while it is still fully constructed. Notifying from
	// node destructors would walk into half-destroyed derived classes.
	// (The former node_removed/removed_from_graph signals were removed with
	// QObject; observers are notified through the facade layer instead.)
	for (Node *node : node_children_) {
		node->RemovedFromGraphEvent(this);
	}

	// By deleting the last nodes first, we assume that nodes that are most important are deleted last
	// (e.g. Project's ColorManager or ProjectSettingsNode.
	for (auto it = node_children_.cbegin(); it != node_children_.cend(); it++) {
		(*it)->set_caches_enabled(false);
	}

	while (!node_children_.empty()) {
		Node *node = node_children_.back();
		node_children_.pop_back();
		// Keep the parent pointer set while deleting: ~Node() calls
		// disconnect_all(), whose edges point at nodes still owned by this
		// graph, and the disconnect asserts require both sides to share the
		// same parent. During teardown disconnect_all() runs silent (see
		// is_being_cleared_) and ~Node() clears the parent itself.
		delete node;
	}

	// Reset root so initialize() can be called again after clear()
	root_ = nullptr;

	is_being_cleared_ = false;
}

SerializedData Project::load(XmlStreamReader *reader)
{
	SerializedData data;
	std::set<std::string> plugin_paths;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "uuid") {
			this->set_uuid(reader->read_element_text());

		} else if (reader->name() == "plugins") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "plugin") {
					std::string bundle_path;
					std::string file_path;
					for (const XmlStreamAttribute &attr : reader->attributes()) {
						if (attr.name == "bundle") {
							bundle_path = attr.value;
						} else if (attr.name == "file") {
							file_path = attr.value;
						}
					}

					const std::string path =
						bundle_path.empty() ? file_path : bundle_path;
					if (!path.empty()) {
						plugin_paths.insert(path);
					}

					reader->skip_current_element();
				} else {
					reader->skip_current_element();
				}
			}

			if (!plugin_paths.empty()) {
				if (!Current::get_instance().plugin_host() ||
					!Current::get_instance().plugin_cache()) {
					// ADAPT(M9): plugin::load_plugins still takes a QString
					// (pluginSupport/olivehost.h is converted in M9)
					plugin::load_plugins(std::string());
				}

				auto *cache = OFX::Host::PluginCache::getPluginCache();
				for (const std::string &path : plugin_paths) {
					cache->addFileToPath(path, true);
				}
				cache->scanPluginFiles();
				NodeFactory::register_plugin_nodes();
			}

		} else if (reader->name() == "nodes") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "node") {
					std::string id;

					{
						for (const XmlStreamAttribute &attr :
							 reader->attributes()) {
							if (attr.name == "id") {
								id = attr.value;
							}
						}
					}

					if (id.empty()) {
						fprintf(stderr, "Failed to load node with empty ID\n");
						reader->skip_current_element();
					} else {
						Node *node = NodeFactory::create_from_id(id);

						if (!node) {
							fprintf(stderr, "Failed to find node with ID %s\n",
									id.c_str());
							reader->skip_current_element();
						} else {
							// Disable cache while node is being loaded (we'll re-enable it later)
							node->set_caches_enabled(false);

							node->load(reader, &data);

							add_node(node);
						}
					}
				} else {
					reader->skip_current_element();
				}
			}

		} else if (reader->name() == "settings") {
			while (xml_read_next_start_element(reader)) {
				std::string key = reader->name();
				std::string val = reader->read_element_text();
				set_setting(key, val);
			}
		} else {
			// Skip this
			reader->skip_current_element();
		}
	}

	// Resolve root if applicable
	std::string root = get_setting(k_root_key);
	if (!root.empty()) {
		uintptr_t r = strtoull(root.c_str(), nullptr, 10);
		auto it = data.node_ptrs.find(r);
		if (it != data.node_ptrs.end()) {
			Node *n = it->second;
			assert(!root_);
			root_ = dynamic_cast<Folder *>(n);
			set_setting(k_root_key,
					   std::to_string(reinterpret_cast<uintptr_t>(root_)));
		}
	}

	return data;
}

void Project::save(XmlStreamWriter *writer) const
{
	writer->write_attribute("version", std::to_string(1));

	writer->write_text_element("uuid", this->get_uuid());

	std::vector<std::pair<std::string, std::map<std::string, std::string>>>
		plugins_to_save;
	{
		std::set<std::string> seen;
		for (Node *node : this->nodes()) {
			auto *plugin = node->getPlugin();
			if (!plugin) {
				continue;
			}

			const std::string id = plugin->getIdentifier();
			const int major = plugin->getVersionMajor();
			const int minor = plugin->getVersionMinor();
			std::string bundle_path;
			std::string file_path;
			if (auto *binary = plugin->getBinary()) {
				bundle_path = binary->getBundlePath();
				file_path = binary->getFilePath();
			}

			const std::string key = id + "|" + std::to_string(major) + "|" +
									std::to_string(minor) + "|" + bundle_path +
									"|" + file_path;
			if (seen.count(key)) {
				continue;
			}
			seen.insert(key);

			std::map<std::string, std::string> attrs;
			attrs.insert({ "id", id });
			attrs.insert({ "major", std::to_string(major) });
			attrs.insert({ "minor", std::to_string(minor) });
			if (!bundle_path.empty()) {
				attrs.insert({ "bundle", bundle_path });
			}
			if (!file_path.empty()) {
				attrs.insert({ "file", file_path });
			}

			plugins_to_save.push_back({ id, attrs });
		}
	}

	if (!plugins_to_save.empty()) {
		writer->write_start_element("plugins");
		for (const auto &entry : plugins_to_save) {
			writer->write_start_element("plugin");
			for (auto it = entry.second.cbegin(); it != entry.second.cend();
				 ++it) {
				writer->write_attribute(it->first, it->second);
			}
			writer->write_end_element();
		}
		writer->write_end_element();
	}

	if (!this->nodes().empty()) {
		writer->write_start_element("nodes");

		for (Node *node : this->nodes()) {
			writer->write_start_element("node");

			node->save(writer);

			writer->write_end_element(); // node
		}

		writer->write_end_element(); // nodes
	}

	if (!this->settings_.empty()) {
		writer->write_start_element("settings");

		for (auto it = this->settings_.cbegin(); it != this->settings_.cend();
			 it++) {
			writer->write_text_element(it->first, it->second);
		}

		writer->write_end_element(); // settings
	}
}

int Project::get_number_of_contexts_node_is_in(Node *node, bool except_itself) const
{
	int count = 0;

	for (Node *ctx : node_children_) {
		if (ctx->context_contains_node(node) && (!except_itself || ctx != node)) {
			count++;
		}
	}

	return count;
}

void Project::add_node(Node *node)
{
	node_children_.push_back(node);
	node->set_parent(this);

	// (The signal forwarding and node_added/added_to_graph emissions of the
	// former ChildAdded branch were removed with QObject; the facade layer
	// subscribes through oakengine_event instead.)
	node->AddedToGraphEvent(this);
}

void Project::remove_node(Node *node)
{
	auto it = std::find(node_children_.begin(), node_children_.end(), node);
	if (it == node_children_.end()) {
		return;
	}
	node_children_.erase(it);

	if (is_being_cleared_) {
		// Teardown: everything is being destroyed anyway; clear() detaches
		// and deletes the nodes itself.
		return;
	}

	node->RemovedFromGraphEvent(this);

	// Remove from any contexts
	for (Node *context : node_children_) {
		context->remove_node_from_context(node);
	}

	node->set_parent(nullptr);
}

std::string Project::name() const
{
	if (filename_.empty()) {
		return "(untitled)";
	} else {
		// Same as QFileInfo::completeBaseName(): filename up to the first dot
		std::string base = std::filesystem::path(filename_).filename().string();
		std::string::size_type dot = base.find('.');
		if (dot != std::string::npos) {
			base.erase(dot);
		}
		return base;
	}
}

const std::string &Project::filename() const
{
	return filename_;
}

std::string Project::pretty_filename() const
{
	std::string fn = filename();

	if (fn.empty()) {
		return "(untitled)";
	} else {
		return fn;
	}
}

void Project::set_filename(const std::string &s)
{
	filename_ = s;

	// (The name_changed() signal was removed with QObject; observers are
	// notified through the facade layer.)
}

void Project::set_modified(bool e)
{
	is_modified_ = e;
	set_autorecovery_saved(!e);

	// (The modified_changed() signal was removed with QObject.)
}

bool Project::has_autorecovery_been_saved() const
{
	return autorecovery_saved_;
}

void Project::set_autorecovery_saved(bool e)
{
	autorecovery_saved_ = e;
}

bool Project::is_new() const
{
	return !is_modified_ && filename_.empty();
}

std::string Project::get_cache_alongside_project_path() const
{
	if (!filename_.empty()) {
		// Non-translated string so the path doesn't change if the language does
		return (std::filesystem::path(filename_).parent_path() / "cache")
			.string();
	}
	return std::string();
}

std::string Project::cache_path() const
{
	CacheSetting setting = get_cache_location_setting();

	switch (setting) {
	case k_cache_use_default_location:
		break;
	case k_cache_custom_path: {
		std::string cache_path = get_custom_cache_path();
		if (!cache_path.empty()) {
			return cache_path;
		}
		break;
	}
	case k_cache_store_alongside_project: {
		std::string alongside = get_cache_alongside_project_path();
		if (!alongside.empty()) {
			return alongside;
		}
		break;
	}
	}

	int needed = oakrender_disk_cache_path(NULL, 0);
	if (needed <= 0) {
		return std::string();
	}
	std::string path(size_t(needed), '\0');
	oakrender_disk_cache_path(path.data(), needed);
	path.resize(size_t(needed - 1));
	return path;
}

void Project::regenerate_uuid()
{
	// Same text format as QUuid::createUuid().toString():
	// "{xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx}" (lowercase hex, version 4)
	uint8_t b[16];
	std::random_device rd;
	for (int i = 0; i < 16; i += 4) {
		uint32_t r = (uint32_t(rd()) << 16) ^ uint32_t(rd());
		memcpy(b + i, &r, 4);
	}
	b[6] = (b[6] & 0x0F) | 0x40; // version 4
	b[8] = (b[8] & 0x3F) | 0x80; // variant 1

	char text[40];
	snprintf(text, sizeof(text),
			 "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
			 "%02x%02x%02x%02x%02x%02x}",
			 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
			 b[10], b[11], b[12], b[13], b[14], b[15]);
	uuid_ = text;
}

void Project::set_setting(const std::string &key, const std::string &value)
{
	settings_.insert_or_assign(key, value);

	// (The setting_changed() signal and the ColorManager change signals were
	// removed with QObject; observers are notified through the facade layer.)
	if (key == k_color_config_filename) {
		color_manager_->update_config_from_filename();
	}
}

}
