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

#include <QDir>
#include <QFileInfo>

#include "common/current.h"
#include "common/qtutils.h"
#include "common/xmlutils.h"
#include "node/color/ociobase/ociobase.h"
#include "node/factory.h"
#include "node/group/group.h"
#include "node/project/folder/folder.h"
#include "node/serializeddata.h"
#include "pluginSupport/olivehost.h"
#include "ofxhPluginCache.h"
#include "render/diskmanager.h"

namespace olive
{

#define super QObject

const QString Project::k_cache_location_setting_key =
	QStringLiteral("cachesetting");
const QString Project::k_cache_path_key = QStringLiteral("customcachepath");
const QString Project::k_color_config_filename =
	QStringLiteral("colorconfigfilename");
const QString Project::k_default_input_color_space_key =
	QStringLiteral("defaultinputcolorspace");
const QString Project::k_color_reference_space =
	QStringLiteral("colorreferencespace");
const QString Project::k_root_key = QStringLiteral("root");

const QString Project::k_item_mime_type =
	QStringLiteral("application/x-oliveprojectitemdata");

Project::Project()
	: root_(nullptr)
	, is_modified_(false)
	, autorecovery_saved_(true)
{
	// Generate UUID for this project
	regenerate_uuid();

	// Initialize color manager
	color_manager_ = new ColorManager(this);
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
		root_->setParent(this);
		root_->set_label(tr("Root"));
		settings_.insert(k_root_key,
						 QString::number(reinterpret_cast<quintptr>(root_)));
	}
}

void Project::clear()
{
	// By deleting the last nodes first, we assume that nodes that are most important are deleted last
	// (e.g. Project's ColorManager or ProjectSettingsNode.
	for (auto it = node_children_.cbegin(); it != node_children_.cend(); it++) {
		(*it)->set_caches_enabled(false);
	}

	while (!node_children_.isEmpty()) {
		delete node_children_.last();
	}
}

SerializedData Project::load(QXmlStreamReader *reader)
{
	SerializedData data;
	QSet<QString> plugin_paths;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("uuid")) {
			this->set_uuid(QUuid::fromString(reader->readElementText()));

		} else if (reader->name() == QStringLiteral("plugins")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("plugin")) {
					QString bundle_path;
					QString file_path;
					XMLAttributeLoop(reader, attr)
					{
						if (attr.name() == QStringLiteral("bundle")) {
							bundle_path = attr.value().toString();
						} else if (attr.name() == QStringLiteral("file")) {
							file_path = attr.value().toString();
						}
					}

					const QString path = bundle_path.isEmpty() ? file_path :
																 bundle_path;
					if (!path.isEmpty()) {
						plugin_paths.insert(path);
					}

					reader->skipCurrentElement();
				} else {
					reader->skipCurrentElement();
				}
			}

			if (!plugin_paths.isEmpty()) {
				if (!Current::getInstance().plugin_host() ||
					!Current::getInstance().plugin_cache()) {
					plugin::load_plugins(QString());
				}

				auto *cache = OFX::Host::PluginCache::getPluginCache();
				for (const QString &path : plugin_paths) {
					cache->addFileToPath(path.toStdString(), true);
				}
				cache->scanPluginFiles();
				NodeFactory::register_plugin_nodes();
			}

		} else if (reader->name() == QStringLiteral("nodes")) {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == QStringLiteral("node")) {
					QString id;

					{
						XMLAttributeLoop(reader, attr)
						{
							if (attr.name() == QStringLiteral("id")) {
								id = attr.value().toString();
							}
						}
					}

					if (id.isEmpty()) {
						qWarning() << "Failed to load node with empty ID";
						reader->skipCurrentElement();
					} else {
						Node *node = NodeFactory::create_from_id(id);

						if (!node) {
							qWarning() << "Failed to find node with ID" << id;
							reader->skipCurrentElement();
						} else {
							// Disable cache while node is being loaded (we'll re-enable it later)
							node->set_caches_enabled(false);

							node->load(reader, &data);

							node->setParent(this);
						}
					}
				} else {
					reader->skipCurrentElement();
				}
			}

		} else if (reader->name() == QStringLiteral("settings")) {
			while (xml_read_next_start_element(reader)) {
				QString key = reader->name().toString();
				QString val = reader->readElementText();
				set_setting(key, val);
			}
		} else {
			// Skip this
			reader->skipCurrentElement();
		}
	}

	// Resolve root if applicable
	QString root = get_setting(k_root_key);
	if (!root.isEmpty()) {
		quintptr r = root.toULongLong();
		if (Node *n = data.node_ptrs.value(r)) {
			Q_ASSERT(!root_);
			root_ = dynamic_cast<Folder *>(n);
			set_setting(k_root_key,
					   QString::number(reinterpret_cast<quintptr>(root_)));
		}
	}

	return data;
}

void Project::save(QXmlStreamWriter *writer) const
{
	writer->writeAttribute(QStringLiteral("version"), QString::number(1));

	writer->writeTextElement(QStringLiteral("uuid"),
							 this->get_uuid().toString());

	QVector<QPair<QString, QMap<QString, QString>>> plugins_to_save;
	{
		QSet<QString> seen;
		for (Node *node : this->nodes()) {
			auto *plugin = node->getPlugin();
			if (!plugin) {
				continue;
			}

			const std::string id = plugin->getIdentifier();
			const int major = plugin->getVersionMajor();
			const int minor = plugin->getVersionMinor();
			QString bundle_path;
			QString file_path;
			if (auto *binary = plugin->getBinary()) {
				bundle_path = QString::fromStdString(binary->getBundlePath());
				file_path = QString::fromStdString(binary->getFilePath());
			}

			const QString key = QStringLiteral("%1|%2|%3|%4|%5")
									.arg(QString::fromStdString(id))
									.arg(major)
									.arg(minor)
									.arg(bundle_path)
									.arg(file_path);
			if (seen.contains(key)) {
				continue;
			}
			seen.insert(key);

			QMap<QString, QString> attrs;
			attrs.insert(QStringLiteral("id"), QString::fromStdString(id));
			attrs.insert(QStringLiteral("major"), QString::number(major));
			attrs.insert(QStringLiteral("minor"), QString::number(minor));
			if (!bundle_path.isEmpty()) {
				attrs.insert(QStringLiteral("bundle"), bundle_path);
			}
			if (!file_path.isEmpty()) {
				attrs.insert(QStringLiteral("file"), file_path);
			}

			plugins_to_save.append({ QString::fromStdString(id), attrs });
		}
	}

	if (!plugins_to_save.isEmpty()) {
		writer->writeStartElement(QStringLiteral("plugins"));
		for (const auto &entry : plugins_to_save) {
			writer->writeStartElement(QStringLiteral("plugin"));
			for (auto it = entry.second.cbegin(); it != entry.second.cend();
				 ++it) {
				writer->writeAttribute(it.key(), it.value());
			}
			writer->writeEndElement();
		}
		writer->writeEndElement();
	}

	if (!this->nodes().isEmpty()) {
		writer->writeStartElement(QStringLiteral("nodes"));

		foreach (Node *node, this->nodes()) {
			writer->writeStartElement(QStringLiteral("node"));

			node->save(writer);

			writer->writeEndElement(); // node
		}

		writer->writeEndElement(); // nodes
	}

	if (!this->settings_.isEmpty()) {
		writer->writeStartElement(QStringLiteral("settings"));

		for (auto it = this->settings_.cbegin(); it != this->settings_.cend();
			 it++) {
			writer->writeTextElement(it.key(), it.value());
		}

		writer->writeEndElement(); // settings
	}
}

int Project::get_number_of_contexts_node_is_in(Node *node, bool except_itself) const
{
	int count = 0;

	foreach (Node *ctx, node_children_) {
		if (ctx->context_contains_node(node) && (!except_itself || ctx != node)) {
			count++;
		}
	}

	return count;
}

void Project::childEvent(QChildEvent *event)
{
	super::childEvent(event);

	Node *node = dynamic_cast<Node *>(event->child());

	if (node) {
		if (event->type() == QEvent::ChildAdded) {
			node_children_.append(node);

			// Connect signals
			connect(node, &Node::input_connected, this, &Project::input_connected,
					Qt::DirectConnection);
			connect(node, &Node::input_disconnected, this,
					&Project::input_disconnected, Qt::DirectConnection);
			connect(node, &Node::value_changed, this, &Project::value_changed,
					Qt::DirectConnection);
			connect(node, &Node::input_value_hint_changed, this,
					&Project::input_value_hint_changed, Qt::DirectConnection);

			if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
				connect(group, &NodeGroup::input_passthrough_added, this,
						&Project::group_added_input_passthrough,
						Qt::DirectConnection);
				connect(group, &NodeGroup::input_passthrough_removed, this,
						&Project::group_removed_input_passthrough,
						Qt::DirectConnection);
				connect(group, &NodeGroup::output_passthrough_changed, this,
						&Project::group_changed_output_passthrough,
						Qt::DirectConnection);
			}

			emit node_added(node);
			emit node->added_to_graph(this);
			node->AddedToGraphEvent(this);

			// Emit input connections
			for (auto it = node->input_connections().cbegin();
				 it != node->input_connections().cend(); it++) {
				if (nodes().contains(it->second)) {
					emit input_connected(it->second, it->first);
				}
			}

			// Emit output connections
			for (auto it = node->output_connections().cbegin();
				 it != node->output_connections().cend(); it++) {
				if (nodes().contains(it->second.node())) {
					emit input_connected(it->first, it->second);
				}
			}

		} else if (event->type() == QEvent::ChildRemoved) {
			node_children_.removeOne(node);

			// Disconnect signals
			disconnect(node, &Node::input_connected, this,
					   &Project::input_connected);
			disconnect(node, &Node::input_disconnected, this,
					   &Project::input_disconnected);
			disconnect(node, &Node::value_changed, this, &Project::value_changed);
			disconnect(node, &Node::input_value_hint_changed, this,
					   &Project::input_value_hint_changed);

			if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
				disconnect(group, &NodeGroup::input_passthrough_added, this,
						   &Project::group_added_input_passthrough);
				disconnect(group, &NodeGroup::input_passthrough_removed, this,
						   &Project::group_removed_input_passthrough);
				disconnect(group, &NodeGroup::output_passthrough_changed, this,
						   &Project::group_changed_output_passthrough);
			}

			emit node_removed(node);
			emit node->removed_from_graph(this);
			node->RemovedFromGraphEvent(this);

			// Remove from any contexts
			foreach (Node *context, node_children_) {
				context->remove_node_from_context(node);
			}
		}
	}
}

QString Project::name() const
{
	if (filename_.isEmpty()) {
		return tr("(untitled)");
	} else {
		return QFileInfo(filename_).completeBaseName();
	}
}

const QString &Project::filename() const
{
	return filename_;
}

QString Project::pretty_filename() const
{
	QString fn = filename();

	if (fn.isEmpty()) {
		return tr("(untitled)");
	} else {
		return fn;
	}
}

void Project::set_filename(const QString &s)
{
	filename_ = s;

#ifdef Q_OS_WINDOWS
	// Prevents filenames
	filename_.replace('/', '\\');
#endif

	emit name_changed();
}

void Project::set_modified(bool e)
{
	is_modified_ = e;
	set_autorecovery_saved(!e);

	emit modified_changed(is_modified_);
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
	return !is_modified_ && filename_.isEmpty();
}

QString Project::get_cache_alongside_project_path() const
{
	if (!filename_.isEmpty()) {
		// Non-translated string so the path doesn't change if the language does
		return QFileInfo(filename_).dir().filePath(QStringLiteral("cache"));
	}
	return QString();
}

QString Project::cache_path() const
{
	CacheSetting setting = get_cache_location_setting();

	switch (setting) {
	case k_cache_use_default_location:
		break;
	case k_cache_custom_path: {
		QString cache_path = get_custom_cache_path();
		if (!cache_path.isEmpty()) {
			return cache_path;
		}
		break;
	}
	case k_cache_store_alongside_project: {
		QString alongside = get_cache_alongside_project_path();
		if (!alongside.isEmpty()) {
			return alongside;
		}
		break;
	}
	}

	return DiskManager::instance()->get_default_cache_path();
}

void Project::regenerate_uuid()
{
	uuid_ = QUuid::createUuid();
}

Project *Project::get_project_from_object(const QObject *o)
{
	return QtUtils::get_parent_of_type<Project>(o);
}

void Project::set_setting(const QString &key, const QString &value)
{
	settings_.insert(key, value);
	emit setting_changed(key, value);

	if (key == k_color_reference_space) {
		emit color_manager_->reference_space_changed(value);
	} else if (key == k_color_config_filename) {
		color_manager_->update_config_from_filename();
	} else if (key == k_default_input_color_space_key) {
		emit color_manager_->default_input_changed(value);
	}
}

}
