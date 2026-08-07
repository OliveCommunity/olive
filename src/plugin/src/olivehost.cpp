/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "olivehost.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <memory>

#include <ofxhBinary.h>
#include <ofxhPluginCache.h>
#include <ofxMessage.h>

#include "common/current.h"
#include "oliveplugininstance.h"

using namespace OFX::Host;
using namespace olive::plugin;

namespace olive
{
namespace plugin
{
class PluginNode;
}
}

#ifndef OAK_APP_VERSION
#define OAK_APP_VERSION "0.0.0"
#endif

namespace
{

olive::plugin::HostMessageHandler message_handler_;

void add_plugin_path(OFX::Host::PluginCache *cache, const std::string &path,
					 bool recurse = true)
{
	if (!cache || path.empty()) {
		return;
	}
	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		return;
	}
	cache->addFileToPath(
		std::filesystem::weakly_canonical(path, ec).string(), recurse);
}

void add_plugin_paths_from_env(OFX::Host::PluginCache *cache,
							   const char *env_var)
{
	const char *raw = std::getenv(env_var);
	if (!raw || !*raw) {
		return;
	}

	const char separator =
#if defined(_WIN32)
		';';
#else
		':';
#endif

	std::string remaining(raw);
	size_t pos;
	while ((pos = remaining.find(separator)) != std::string::npos) {
		std::string path = remaining.substr(0, pos);
		remaining.erase(0, pos + 1);
		if (!path.empty()) {
			add_plugin_path(cache, path);
		}
	}
	if (!remaining.empty()) {
		add_plugin_path(cache, remaining);
	}
}

std::string format_message(const char *format, va_list args)
{
	char buffer[1024];
	buffer[0] = '\0';
	vsnprintf(buffer, sizeof(buffer), format, args);
	return buffer;
}

} // namespace

void olive::plugin::set_host_message_handler(HostMessageHandler handler)
{
	message_handler_ = std::move(handler);
}

olive::plugin::HostMessageHandler olive::plugin::get_host_message_handler()
{
	return message_handler_;
}

void olive::plugin::load_plugins(const std::string &path)
{
	OakCurrent current = oakcommon_current_instance();

	void *host_ptr = nullptr;
	void *cache_ptr = nullptr;
	oakcommon_current_get_plugin_host(current, &host_ptr);
	oakcommon_current_get_plugin_cache(current, &cache_ptr);

	std::shared_ptr<OliveHost> host;
	std::shared_ptr<ImageEffect::PluginCache> image_effect_plugin_cache;

	if (host_ptr) {
		host = *static_cast<std::shared_ptr<OliveHost> *>(host_ptr);
	}
	if (cache_ptr) {
		image_effect_plugin_cache =
			*static_cast<std::shared_ptr<ImageEffect::PluginCache> *>(
				cache_ptr);
	}

	if (!host || !image_effect_plugin_cache) {
		host = std::make_shared<OliveHost>();
		image_effect_plugin_cache =
			std::make_shared<ImageEffect::PluginCache>(*host);

		// The Current slots hold owning shared_ptr copies; the destroy
		// callbacks free those copies when the slots are replaced or the
		// current object dies.
		auto *host_slot = new std::shared_ptr<OliveHost>(host);
		oakcommon_current_set_plugin_host(
			current, host_slot,
			[](void *p) { delete static_cast<std::shared_ptr<OliveHost> *>(p); });

		auto *cache_slot =
			new std::shared_ptr<ImageEffect::PluginCache>(
				image_effect_plugin_cache);
		oakcommon_current_set_plugin_cache(
			current, cache_slot,
			[](void *p) {
				delete static_cast<std::shared_ptr<ImageEffect::PluginCache> *>(
					p);
			});

		image_effect_plugin_cache->registerInCache(
			*OFX::Host::PluginCache::getPluginCache());
	}
	oakcommon_current_free(&current);

	OFX::Host::PluginCache *cache = OFX::Host::PluginCache::getPluginCache();
	cache->setPluginHostPath("Olive");

	const std::string home_path = std::getenv("HOME") ? std::getenv("HOME") : "";
	if (!home_path.empty()) {
		add_plugin_path(cache, home_path + "/.OFX/Plugins");
		add_plugin_path(cache, home_path + "/.local/share/OFX/Plugins");
		add_plugin_path(cache,
						home_path + "/.local/share/olive/ofx/Plugins");
	}

	// Application-relative plugin paths are resolved by the facade; scan
	// the default locations relative to the working directory here.
	add_plugin_path(cache, "../OFX/Plugins");
	add_plugin_path(cache, "../share/olive/ofx/Plugins");
	add_plugin_path(cache, "../lib/olive/ofx/Plugins");

	add_plugin_paths_from_env(cache, "OLIVE_OFX_PLUGIN_PATH");
	add_plugin_paths_from_env(cache, "OLIVE_PLUGIN_PATH");

	if (!path.empty()) {
		add_plugin_path(cache, path, true);
	}
	cache->scanPluginFiles();
}

OliveHost::OliveHost()
{
	// Identify the host to plugins; HostSupport seeds these with "UNKNOWN".
	_properties.setStringProperty(kOfxPropName, "Oak Video Editor");
	_properties.setStringProperty(kOfxPropLabel, "Oak Video Editor");
	_properties.setStringProperty(kOfxPropVersionLabel, OAK_APP_VERSION);

	// Numeric version for plugins that query kOfxPropVersion directly.
	int version_parts[3] = { 0, 0, 0 };
	sscanf(OAK_APP_VERSION, "%d.%d.%d", &version_parts[0], &version_parts[1],
		   &version_parts[2]);
	_properties.setIntProperty(kOfxPropVersion, version_parts[0], 0);
	_properties.setIntProperty(kOfxPropVersion, version_parts[1], 1);
	_properties.setIntProperty(kOfxPropVersion, version_parts[2], 2);
}

OliveHost::~OliveHost()
{
}

void OliveHost::destroy_instance(OFX::Host::ImageEffect::Instance *instance)
{
	if (!instance) {
		return;
	}
	for (auto it = instances_.begin(); it != instances_.end(); ++it) {
		if (it->get() == instance) {
			instances_.erase(it);
			break;
		}
	}
}

std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
OliveHost::makeDescriptor(ImageEffect::ImageEffectPlugin *plugin)
{
	std::shared_ptr<OFX::Host::ImageEffect::Descriptor> desc =
		std::make_shared<ImageEffect::Descriptor>(plugin);
	descriptors_.push_back(std::shared_ptr<ImageEffect::Descriptor>(desc));
	return desc;
}

std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
OliveHost::makeDescriptor(const ImageEffect::Descriptor &root_context,
						  ImageEffect::ImageEffectPlugin *plugin)
{
	std::shared_ptr<OFX::Host::ImageEffect::Descriptor> desc =
		std::make_shared<ImageEffect::Descriptor>(root_context, plugin);
	descriptors_.push_back(std::shared_ptr<ImageEffect::Descriptor>(desc));
	return desc;
}

std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
OliveHost::makeDescriptor(const std::string &bundle_path,
						  ImageEffect::ImageEffectPlugin *plugin)
{
	std::shared_ptr<OFX::Host::ImageEffect::Descriptor> desc =
		std::make_shared<ImageEffect::Descriptor>(bundle_path, plugin);
	descriptors_.push_back(std::shared_ptr<ImageEffect::Descriptor>(desc));
	return desc;
}

ImageEffect::Instance *
OliveHost::newInstance(void *client_data, ImageEffect::ImageEffectPlugin *plugin,
					   ImageEffect::Descriptor &desc,
					   const std::string &context)
{
	auto *instance = new OlivePluginInstance(plugin, desc, context, true);
	if (client_data) {
		instance->set_node_handle(
			*static_cast<OakNodeNode *>(client_data));
	}
	instances_.push_back(std::shared_ptr<OlivePluginInstance>(instance));
	return instance;
}

OfxStatus OliveHost::vmessage(const char *type, const char *id,
							  const char *format, va_list args)
{
	if (!type || !format) {
		return kOfxStatFailed;
	}

	std::string message = format_message(format, args);

	if (message_handler_) {
		return message_handler_(type, message);
	}

	// Headless default: log to stderr, questions get "no".
	fprintf(stderr, "OFX message: %s %s\n", type, message.c_str());
	if (strcmp(type, kOfxMessageQuestion) == 0) {
		return kOfxStatReplyNo;
	}
	return kOfxStatOK;
}

OfxStatus OliveHost::setPersistentMessage(const char *type, const char *id,
										  const char *format, va_list args)
{
	if (!type || !format) {
		return kOfxStatFailed;
	}

	std::string message = format_message(format, args);

	if (strcmp(type, kOfxMessageError) == 0) {
		persistent_messages_.push_back({ HostMessageType::error, message });
	} else if (strcmp(type, kOfxMessageWarning) == 0) {
		persistent_messages_.push_back({ HostMessageType::warning, message });
	} else if (strcmp(type, kOfxMessageMessage) == 0) {
		persistent_messages_.push_back({ HostMessageType::message, message });
	} else {
		return kOfxStatFailed;
	}

	if (message_handler_) {
		return message_handler_(type, message);
	}

	fprintf(stderr, "OFX %s: %s\n", type, message.c_str());
	return kOfxStatOK;
}

OfxStatus OliveHost::clearPersistentMessage()
{
	persistent_messages_.clear();
	return kOfxStatOK;
}
