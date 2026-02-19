/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef OLIVE_HOST_H
#define OLIVE_HOST_H
#include "node/plugins/Plugin.h"
#include  "ofxhHost.h"
#include "ofxhImageEffectAPI.h"
#include "ofxCore.h"
#include "ofxhImageEffect.h"

#include <QVariant>
#include <cstdint>
#include <QString>
#include <QMap>
#include <any>
#include <list>
#include <memory>
#include <qlist.h>
namespace olive {
namespace plugin {
enum class HostMessageType{
	Error,
	Warning,
	Message
};
struct HostPersistentMessage{
	HostMessageType type;
	QString message;
};


void loadPlugins(QString path);
class OliveHost: public OFX::Host::ImageEffect::Host{
public:
	OliveHost()=default;
	~OliveHost() override;
	void destroyInstance(OFX::Host::ImageEffect::Instance *instance);

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

	OFX::Host::ImageEffect::Instance* newInstance(void *clientData,
							OFX::Host::ImageEffect::ImageEffectPlugin* plugin,
							OFX::Host::ImageEffect::Descriptor& desc,
							const std::string& context) override;


	std::shared_ptr<OFX::Host::ImageEffect::Descriptor> makeDescriptor(
		OFX::Host::ImageEffect::ImageEffectPlugin* plugin) override;

	std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
	makeDescriptor(const OFX::Host::ImageEffect::Descriptor &rootContext,
				   OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;

	std::shared_ptr<OFX::Host::ImageEffect::Descriptor>
	makeDescriptor(const std::string &bundlePath,
				   OFX::Host::ImageEffect::ImageEffectPlugin *plugin) override;
	/// vmessage
	virtual OfxStatus vmessage(const char *type, const char *id,
							   const char *format, va_list args) override;

	/// vmessage
	virtual OfxStatus setPersistentMessage(const char *type, const char *id,
										   const char *format,
										   va_list args) override;
	/// vmessage
	virtual OfxStatus clearPersistentMessage() override;

#ifdef OFX_SUPPORTS_OPENGLRENDER
	/// @see OfxImageEffectOpenGLRenderSuiteV1.flushResources()
	virtual OfxStatus flushOpenGLResources() const override
	{
		return kOfxStatFailed;
	};
#endif
private:
	QList<std::shared_ptr<OFX::Host::ImageEffect::Descriptor>> descriptors_;
	QList<std::shared_ptr<OFX::Host::ImageEffect::Instance>> instances_;
	QList<HostPersistentMessage> persistent_messages_;
};
}
}
#endif
