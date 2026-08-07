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
#ifndef OAK_OLIVE_HOST_H
#define OAK_OLIVE_HOST_H

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "ofxCore.h"
#include "ofxhHost.h"
#include "ofxhImageEffect.h"
#include "ofxhImageEffectAPI.h"

namespace olive
{
namespace plugin
{

enum class HostMessageType { error, warning, message };
struct HostPersistentMessage {
	HostMessageType type;
	std::string message;
};

/**
 * @brief UI message handler for OFX host messages
 *
 * Registered by the facade. Return kOfxStatReplyYes/No for questions,
 * kOfxStatOK otherwise. Without a handler, messages are logged to
 * stderr and questions get kOfxStatReplyNo (headless default).
 */
using HostMessageHandler =
	std::function<OfxStatus(const char *type, const std::string &message)>;

void set_host_message_handler(HostMessageHandler handler);

/** @brief Currently registered handler (may be empty). */
HostMessageHandler get_host_message_handler();

void load_plugins(const std::string &path = std::string());

class OliveHost : public OFX::Host::ImageEffect::Host {
public:
	OliveHost();
	~OliveHost() override;
	void destroy_instance(OFX::Host::ImageEffect::Instance *instance);

	bool pluginSupported(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
						 std::string &reason) const override
	{
		if (!plugin) {
			reason = "null plugin";
			return false;
		}
		if (plugin->getContexts().empty()) {
			reason = "no supported contexts (describe failed)";
			return false;
		}
		return true;
	};

	OFX::Host::ImageEffect::Instance *
	newInstance(void *client_data,
				OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
				OFX::Host::ImageEffect::Descriptor &desc,
				const std::string &context) override;

	std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
	makeDescriptor(OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;

	std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
	makeDescriptor(const OFX::Host::ImageEffect::Descriptor &root_context,
				   OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;

	std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
	makeDescriptor(const std::string &bundle_path,
				   OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;
	/// vmessage
	OfxStatus vmessage(const char *type, const char *id,
					   const char *format, va_list args) override;

	/// vmessage
	OfxStatus setPersistentMessage(const char *type, const char *id,
								   const char *format, va_list args) override;
	/// vmessage
	OfxStatus clearPersistentMessage() override;

#ifdef OFX_SUPPORTS_OPENGLRENDER
	/// @see OfxImageEffectOpenGLRenderSuiteV1.flushResources()
	virtual OfxStatus flushOpenGLResources() const override
	{
		return kOfxStatFailed;
	};
#endif

	int persistent_message_count() const
	{
		return int(persistent_messages_.size());
	}

	const std::vector<HostPersistentMessage> &persistent_messages() const
	{
		return persistent_messages_;
	}

private:
	std::vector<std::shared_ptr<OFX::Host::ImageEffect::Descriptor>>
		descriptors_;
	std::vector<std::shared_ptr<OFX::Host::ImageEffect::Instance>> instances_;
	std::vector<HostPersistentMessage> persistent_messages_;
};
}
}
#endif
