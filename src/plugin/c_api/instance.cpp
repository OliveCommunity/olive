/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "plugin/instance.h"

#include <atomic>
#include <new>

#include <ofxhImageEffectAPI.h>
#include <ofxhPluginCache.h>

#include "../src/oliveclip.h"
#include "../src/oliveplugininstance.h"

namespace
{

struct InstanceBox {
	olive::plugin::OlivePluginInstance *instance;
	std::atomic<uint32_t> refs;
	oakplugin_progress_fn progress_fn;
	void *progress_userdata;

	InstanceBox(olive::plugin::OlivePluginInstance *i)
		: instance(i)
		, refs(1)
		, progress_fn(nullptr)
		, progress_userdata(nullptr)
	{
	}
};

std::atomic<int> g_alive{ 0 };

void box_addref(void *ctx)
{
	if (ctx) {
		static_cast<InstanceBox *>(ctx)->refs.fetch_add(1);
	}
}

void box_release(void *ctx)
{
	if (!ctx) {
		return;
	}
	InstanceBox *box = static_cast<InstanceBox *>(ctx);
	if (box->refs.fetch_sub(1) == 1) {
		delete box->instance;
		delete box;
		g_alive--;
	}
}

OakPluginInstance make_handle(olive::plugin::OlivePluginInstance *instance)
{
	OakPluginInstance handle = {};
	if (!instance) {
		return handle;
	}

	InstanceBox *box = new (std::nothrow) InstanceBox(instance);
	if (!box) {
		delete instance;
		return handle;
	}

	handle.ctx = box;
	handle.addref = box_addref;
	handle.release = box_release;
	handle.abi_version = OAKPLUGIN_ABI_VERSION;
	g_alive++;
	return handle;
}

InstanceBox *impl(OakPluginInstance h)
{
	if (!h.ctx) {
		return nullptr;
	}
	return static_cast<InstanceBox *>(h.ctx);
}

} // namespace

OakPluginInstance oakplugin_instance_create(const char *plugin_id)
{
	if (!plugin_id) {
		return OakPluginInstance{};
	}

	try {
		OFX::Host::PluginCache *cache =
			OFX::Host::PluginCache::getPluginCache();
		if (!cache) {
			return OakPluginInstance{};
		}

		for (const auto &plugin : cache->getPlugins()) {
			if (plugin && plugin->getIdentifier() == plugin_id) {
				auto *effect_plugin =
					dynamic_cast<OFX::Host::ImageEffect::ImageEffectPlugin *>(
						plugin);
				if (!effect_plugin) {
					return OakPluginInstance{};
				}
				auto *instance = effect_plugin->createInstance(
					kOfxImageEffectContextFilter, nullptr);
				auto *olive_instance =
					dynamic_cast<olive::plugin::OlivePluginInstance *>(
						instance);
				if (!olive_instance) {
					delete instance;
					return OakPluginInstance{};
				}
				return make_handle(olive_instance);
			}
		}
		return OakPluginInstance{};
	} catch (...) {
		return OakPluginInstance{};
	}
}

void oakplugin_instance_free(OakPluginInstance *instance)
{
	if (!instance || !instance->ctx) {
		return;
	}
	instance->release(instance->ctx);
	instance->ctx = NULL;
}

int oakplugin_instance_set_param(OakPluginInstance instance,
								 const char *param_id,
								 const oaknode_value *value)
{
	InstanceBox *box = impl(instance);
	if (!box || !param_id || !value) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		const std::map<std::string, OFX::Host::Param::Instance *> &params_map =
			box->instance->getParams();
		auto it = params_map.find(param_id);
		if (it == params_map.end() || !it->second) {
			return OAKPLUGIN_E_NOT_FOUND;
		}
		OFX::Host::Param::Instance *param = it->second;

		switch (value->type) {
		case OAKNODE_VALUE_INT:
		case OAKNODE_VALUE_COMBO:
			return static_cast<OFX::Host::Param::IntegerInstance *>(param)
						   ->set(int(value->num)) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		case OAKNODE_VALUE_BOOL:
			return static_cast<OFX::Host::Param::BooleanInstance *>(param)
						   ->set(value->num != 0) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		case OAKNODE_VALUE_FLOAT:
			return static_cast<OFX::Host::Param::DoubleInstance *>(param)
						   ->set(value->f[0]) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		case OAKNODE_VALUE_VEC2:
			return static_cast<OFX::Host::Param::Double2DInstance *>(param)
						   ->set(value->f[0], value->f[1]) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		case OAKNODE_VALUE_VEC3:
			return static_cast<OFX::Host::Param::Double3DInstance *>(param)
						   ->set(value->f[0], value->f[1],
								 value->f[2]) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		case OAKNODE_VALUE_COLOR:
		case OAKNODE_VALUE_VEC4:
			return static_cast<OFX::Host::Param::RGBAInstance *>(param)
						   ->set(value->f[0], value->f[1], value->f[2],
								 value->f[3]) == kOfxStatOK
					   ? OAKPLUGIN_OK
					   : OAKPLUGIN_E_FAILED;
		default:
			return OAKPLUGIN_E_INVALID;
		}
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_instance_get_param(OakPluginInstance instance,
								 const char *param_id, oaknode_value *out)
{
	InstanceBox *box = impl(instance);
	if (!box || !param_id || !out) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		const std::map<std::string, OFX::Host::Param::Instance *> &params_map =
			box->instance->getParams();
		auto it = params_map.find(param_id);
		if (it == params_map.end() || !it->second) {
			return OAKPLUGIN_E_NOT_FOUND;
		}
		OFX::Host::Param::Instance *param = it->second;

		out->type = OAKNODE_VALUE_NONE;
		const std::string &type = param->getType();
		if (type == kOfxParamTypeInteger || type == kOfxParamTypeChoice) {
			int v = 0;
			static_cast<OFX::Host::Param::IntegerInstance *>(param)->get(v);
			out->type = OAKNODE_VALUE_INT;
			out->num = v;
		} else if (type == kOfxParamTypeBoolean) {
			bool v = false;
			static_cast<OFX::Host::Param::BooleanInstance *>(param)->get(v);
			out->type = OAKNODE_VALUE_BOOL;
			out->num = v ? 1 : 0;
		} else if (type == kOfxParamTypeDouble) {
			double v = 0;
			static_cast<OFX::Host::Param::DoubleInstance *>(param)->get(v);
			out->type = OAKNODE_VALUE_FLOAT;
			out->f[0] = v;
		} else if (type == kOfxParamTypeDouble2D) {
			double x = 0, y = 0;
			static_cast<OFX::Host::Param::Double2DInstance *>(param)->get(x,
																		  y);
			out->type = OAKNODE_VALUE_VEC2;
			out->f[0] = x;
			out->f[1] = y;
		} else if (type == kOfxParamTypeDouble3D) {
			double x = 0, y = 0, z = 0;
			static_cast<OFX::Host::Param::Double3DInstance *>(param)->get(x,
																		  y,
																		  z);
			out->type = OAKNODE_VALUE_VEC3;
			out->f[0] = x;
			out->f[1] = y;
			out->f[2] = z;
		} else if (type == kOfxParamTypeRGBA) {
			double r = 0, g = 0, b = 0, a = 0;
			static_cast<OFX::Host::Param::RGBAInstance *>(param)->get(r, g,
																	  b, a);
			out->type = OAKNODE_VALUE_COLOR;
			out->f[0] = r;
			out->f[1] = g;
			out->f[2] = b;
			out->f[3] = a;
		} else {
			return OAKPLUGIN_E_INVALID;
		}
		return OAKPLUGIN_OK;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_instance_set_param_string(OakPluginInstance instance,
										const char *param_id,
										const char *value)
{
	InstanceBox *box = impl(instance);
	if (!box || !param_id) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		const std::map<std::string, OFX::Host::Param::Instance *> &params_map =
			box->instance->getParams();
		auto it = params_map.find(param_id);
		if (it == params_map.end() || !it->second) {
			return OAKPLUGIN_E_NOT_FOUND;
		}
		OFX::Host::Param::Instance *param = it->second;
		return static_cast<OFX::Host::Param::StringInstance *>(param)
					   ->set(value ? value : "") == kOfxStatOK
				   ? OAKPLUGIN_OK
				   : OAKPLUGIN_E_FAILED;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_instance_get_param_string(OakPluginInstance instance,
										const char *param_id, char *buf,
										int buf_size)
{
	InstanceBox *box = impl(instance);
	if (!box || !param_id) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		const std::map<std::string, OFX::Host::Param::Instance *> &params_map =
			box->instance->getParams();
		auto it = params_map.find(param_id);
		if (it == params_map.end() || !it->second) {
			return OAKPLUGIN_E_NOT_FOUND;
		}
		OFX::Host::Param::Instance *param = it->second;
		std::string value;
		static_cast<OFX::Host::Param::StringInstance *>(param)->get(value);
		int needed = int(value.size()) + 1;
		if (buf && buf_size >= needed) {
			memcpy(buf, value.c_str(), needed);
		}
		return needed;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_instance_render(OakPluginInstance instance,
							  OakRenderTexture dst, OakRenderTexture src,
							  double time_seconds)
{
	InstanceBox *box = impl(instance);
	if (!box) {
		return OAKPLUGIN_E_INVALID;
	}

	try {
		const char *clip_names[] = { "Output", "Source" };
		for (const char *clip_name : clip_names) {
			auto *clip =
				dynamic_cast<olive::plugin::OliveClipInstance *>(
					box->instance->getClip(clip_name));
			if (!clip) {
				continue;
			}
			if (clip->isOutput()) {
				clip->setOutputTexture(dst, time_seconds);
			} else if (src.ctx) {
				clip->setInputTexture(src, time_seconds, false);
			}
		}

		OfxRectI roi = { 0, 0, 0, 0 };
		OfxPointD scale = { 1.0, 1.0 };
		OfxStatus status = box->instance->renderAction(
			time_seconds, kOfxImageFieldNone, roi, scale, false, false,
			false);
		return status == kOfxStatOK ? OAKPLUGIN_OK : OAKPLUGIN_E_FAILED;
	} catch (...) {
		return OAKPLUGIN_E_FAILED;
	}
}

int oakplugin_instance_set_progress_cb(OakPluginInstance instance,
									   oakplugin_progress_fn fn,
									   void *userdata)
{
	InstanceBox *box = impl(instance);
	if (!box) {
		return OAKPLUGIN_E_INVALID;
	}
	box->progress_fn = fn;
	box->progress_userdata = userdata;
	return OAKPLUGIN_OK;
}

int oakplugin_instance_cancel(OakPluginInstance instance)
{
	InstanceBox *box = impl(instance);
	if (!box) {
		return OAKPLUGIN_E_INVALID;
	}
	if (box->progress_fn) {
		box->progress_fn(1.0, box->progress_userdata);
	}
	return OAKPLUGIN_OK;
}

int oakplugin_debug_alive_count(void)
{
	return g_alive.load();
}
