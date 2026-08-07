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
#include "oliveplugininstance.h"

#include <cstdio>
#include <cstring>

#include "ofxGPURender.h"
#include "node/sequence.h"
#include "olivehost.h"
#include "ofxMessage.h"
#include "oliveclip.h"
#include "paraminstance.h"

namespace olive
{
namespace plugin
{
namespace
{

const std::string k_image_field_none_str(kOfxImageFieldNone);
const std::string k_image_field_upper_str(kOfxImageFieldUpper);
const std::string k_image_field_lower_str(kOfxImageFieldLower);

std::string format_ofx_message(const char *format, va_list args)
{
	char buffer[1024];
	va_list args_copy;
	va_copy(args_copy, args);
	const int needed = vsnprintf(buffer, sizeof(buffer), format, args_copy);
	va_end(args_copy);
	if (needed < 0) {
		return std::string();
	}
	if (needed < static_cast<int>(sizeof(buffer))) {
		return std::string(buffer);
	}
	std::vector<char> dynamic_buffer(size_t(needed) + 1);
	const int written =
		vsnprintf(dynamic_buffer.data(), dynamic_buffer.size(), format, args);
	if (written < 0) {
		return std::string();
	}
	return std::string(dynamic_buffer.data());
}

const std::string &field_order_for_params(OakVideoParams params)
{
	int interlacing = 0;
	if (params.ctx) {
		oakcommon_videoparams_get_interlacing(params, &interlacing);
	}
	switch (interlacing) {
	case OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST:
		return k_image_field_upper_str;
	case OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST:
		return k_image_field_lower_str;
	default:
		return k_image_field_none_str;
	}
}

std::thread::id main_thread_id_;
bool main_thread_id_set_ = false;

ActiveViewerProvider active_viewer_provider_;

UndoSubmitFn undo_submit_fn_;

} // namespace

bool is_gui_thread()
{
	if (!main_thread_id_set_) {
		main_thread_id_ = std::this_thread::get_id();
		main_thread_id_set_ = true;
	}
	return std::this_thread::get_id() == main_thread_id_;
}

void set_main_thread_id(std::thread::id id)
{
	main_thread_id_ = id;
	main_thread_id_set_ = true;
}

void set_active_viewer_provider(ActiveViewerProvider provider)
{
	active_viewer_provider_ = std::move(provider);
}

void set_undo_submit_callback(UndoSubmitFn fn)
{
	undo_submit_fn_ = std::move(fn);
}

const std::string &OlivePluginInstance::getDefaultOutputFielding() const
{
	return field_order_for_params(params_);
}

void register_node_instance(uintptr_t identity,
							  OlivePluginInstance *instance);

void OlivePluginInstance::set_node_handle(OakNodeNode node)
{
	if (node_.ctx) {
		register_node_instance(oaknode_node_identity(node_), nullptr);
	}
	node_ = node;
	if (node_.ctx) {
		register_node_instance(oaknode_node_identity(node_), this);
	}
	for (const auto &entry : getParams()) {
		if (!entry.second) {
			continue;
		}
		if (auto *bound = dynamic_cast<NodeBoundParam *>(entry.second)) {
			bound->set_node(node_);
		}
	}
}

OfxStatus OlivePluginInstance::vmessage(const char *type, const char *id,
										const char *format, va_list args)
{
	const std::string message = format_ofx_message(format, args);
	if (message.empty()) {
		return kOfxStatFailed;
	}

	// UI messages route through the host's registered handler
	HostMessageHandler handler = get_host_message_handler();
	if (handler) {
		return handler(type, message);
	}

	fprintf(stderr, "OFX message: %s %s\n", type, message.c_str());
	if (strcmp(type, kOfxMessageQuestion) == 0) {
		return kOfxStatReplyNo;
	}
	return kOfxStatOK;
}

OfxStatus OlivePluginInstance::setPersistentMessage(const char *type,
													const char *id,
													const char *format,
													va_list args)
{
	const std::string message = format_ofx_message(format, args);
	if (message.empty()) {
		return kOfxStatFailed;
	}

	ErrorType error_type;
	// If this is an error message
	if (strncmp(type, kOfxMessageError, strlen(kOfxMessageError)) == 0) {
		error_type = ErrorType::error;
	}
	// A warning
	else if (strncmp(type, kOfxMessageWarning, strlen(kOfxMessageWarning)) ==
			 0) {
		error_type = ErrorType::warning;
	}
	// A simple information
	else if (strncmp(type, kOfxMessageMessage, strlen(kOfxMessageMessage)) ==
			 0) {
		error_type = ErrorType::message;
	} else {
		return kOfxStatFailed;
	}

	persistent_errors_.push_back({ error_type, message });

	HostMessageHandler handler = get_host_message_handler();
	if (handler) {
		return handler(type, message);
	}

	fprintf(stderr, "OFX %s: %s\n", type, message.c_str());
	return kOfxStatOK;
}

OfxStatus OlivePluginInstance::clearPersistentMessage()
{
	persistent_errors_.clear();
	return kOfxStatOK;
}

void OlivePluginInstance::getProjectSize(double &x_size, double &y_size) const
{
	double par = 1.0;
	int par_num = 0, par_den = 1;
	int width = 0, height = 0;
	if (params_.ctx) {
		oakcommon_videoparams_get_pixel_aspect_ratio(params_, &par_num,
												   &par_den);
		oakcommon_videoparams_get_width(params_, &width);
		oakcommon_videoparams_get_height(params_, &height);
	}
	if (par_den != 0) {
		par = double(par_num) / par_den;
	}
	x_size = width * par;
	y_size = height;
}

void OlivePluginInstance::getProjectOffset(double &x_offset,
										   double &y_offset) const
{
	double par = 1.0;
	int par_num = 0, par_den = 1;
	float x = 0, y = 0;
	if (params_.ctx) {
		oakcommon_videoparams_get_pixel_aspect_ratio(params_, &par_num,
												   &par_den);
		oakcommon_videoparams_get_x(params_, &x);
		oakcommon_videoparams_get_y(params_, &y);
	}
	if (par_den != 0) {
		par = double(par_num) / par_den;
	}
	x_offset = x * par;
	y_offset = y;
}

void OlivePluginInstance::getProjectExtent(double &x_size, double &y_size) const
{
	getProjectSize(x_size, y_size);
}

double OlivePluginInstance::getProjectPixelAspectRatio() const
{
	int par_num = 0, par_den = 1;
	if (params_.ctx) {
		oakcommon_videoparams_get_pixel_aspect_ratio(params_, &par_num,
												   &par_den);
	}
	if (par_den == 0) {
		return 1.0;
	}
	double par = double(par_num) / par_den;
	if (par == 0.0) {
		return 1.0; // default PAR when not explicitly set
	}
	return par;
}

double OlivePluginInstance::getFrameRate() const
{
	int num = 0, den = 1;
	if (params_.ctx) {
		oakcommon_videoparams_get_frame_rate(params_, &num, &den);
	}
	return den ? double(num) / den : 0.0;
}

double OlivePluginInstance::getEffectDuration() const
{
	// Return a default duration value
	return 100.0;
}

double OlivePluginInstance::getFrameRecursive() const
{
	// Return current frame (this would typically be set by the host during rendering)
	return 0.0;
}

void OlivePluginInstance::getRenderScaleRecursive(double &x, double &y) const
{
	// Return default render scale (1.0, 1.0)
	x = 1.0;
	y = 1.0;
}

OFX::Host::Param::Instance *
OlivePluginInstance::newParam(const std::string &name,
							  OFX::Host::Param::Descriptor &desc)
{
	const std::string &type = desc.getType();

	if (type == kOfxParamTypeInteger) {
		return new IntegerInstance(node_, desc, this);
	} else if (type == kOfxParamTypeDouble) {
		return new DoubleInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeBoolean) {
		return new BooleanInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeChoice) {
		return new ChoiceInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeString) {
		return new StringInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeRGBA) {
		return new RGBAInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeRGB) {
		return new RGBInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeDouble2D) {
		return new Double2DInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeInteger2D) {
		return new Integer2DInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeDouble3D) {
		return new Double3DInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeInteger3D) {
		return new Integer3DInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeCustom || type == kOfxParamTypeBytes) {
		return new CustomInstance(node_, name, desc, this);
	} else if (type == kOfxParamTypeGroup) {
		return new GroupInstance(desc, this);
	} else if (type == kOfxParamTypePage) {
		return new PageInstance(desc, this);
	} else if (type == kOfxParamTypePushButton) {
		return new PushbuttonInstance(node_, name, desc, this);
	}

	return nullptr; // 未实现的类型
}

OfxStatus OlivePluginInstance::editBegin(const std::string &name)
{
	edit_depth_++;
	if (edit_depth_ == 1) {
		edit_command_ = {};
		edit_label_.clear();
		edit_first_label_.clear();
		edit_param_count_ = 0;
		if (!name.empty()) {
			edit_first_label_ = "Change " + name;
		}
	}
	return kOfxStatOK;
}

OfxStatus OlivePluginInstance::editEnd()
{
	if (edit_depth_ > 0) {
		edit_depth_--;
	}
	if (edit_depth_ == 0 && edit_command_.ctx) {
		std::string label = edit_label_;
		if (label.empty()) {
			if (edit_param_count_ <= 1 && !edit_first_label_.empty()) {
				label = edit_first_label_;
			} else if (edit_param_count_ > 1 && !edit_first_label_.empty()) {
				label = edit_first_label_ + " (+" +
						std::to_string(edit_param_count_ - 1) + ")";
			} else {
				label = "Edit Parameters";
			}
		}
		if (undo_submit_fn_) {
			undo_submit_fn_(edit_command_, label);
		} else {
			oakundo_command_redo_now(edit_command_);
			oakundo_command_free(&edit_command_);
		}
		edit_command_ = {};
		edit_label_.clear();
		edit_first_label_.clear();
		edit_param_count_ = 0;
	}
	return kOfxStatOK;
}

void OlivePluginInstance::submit_undo_command(OakUndoCommand command,
											  const std::string &label)
{
	if (!command.ctx) {
		return;
	}

	if (edit_depth_ > 0) {
		if (!edit_command_.ctx) {
			edit_command_ = oakundo_command_init_multi();
		}
		edit_param_count_++;
		if (!label.empty() && edit_first_label_.empty()) {
			edit_first_label_ = label;
		}

		oakundo_command_redo_now(command);
		oakundo_command_multi_add_child(edit_command_, command);
		return;
	}

	if (undo_submit_fn_ && is_gui_thread()) {
		undo_submit_fn_(command, label);
		return;
	}

	oakundo_command_redo_now(command);
	oakundo_command_free(&command);
}

void OlivePluginInstance::progressStart(const std::string &message,
										const std::string &messageid)
{
	(void)messageid;
	progress_cancelled_ = false;
	progress_active_ = true;

	if (progress_reporter_) {
		progress_reporter_->close();
		progress_reporter_.reset();
	}

	std::string dialog_message =
		message.empty() ? "Processing..." : message;

	progress_reporter_.reset(
		create_plugin_progress_reporter(dialog_message, "OpenFX"));
	progress_reporter_->set_cancel_callback(
		[this](void *) { progress_cancelled_ = true; }, nullptr);
	progress_reporter_->show();
}

void OlivePluginInstance::progressEnd()
{
	progress_active_ = false;
	progress_cancelled_ = false;

	if (progress_reporter_) {
		progress_reporter_->close();
		progress_reporter_.reset();
	}
}

bool OlivePluginInstance::progressUpdate(double t)
{
	if (!progress_active_) {
		return true;
	}

	if (progress_reporter_) {
		double clamped = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		progress_reporter_->set_progress(clamped);
	}

	return !progress_cancelled_;
}

#ifdef OFX_SUPPORTS_OPENGLRENDER
OfxStatus OlivePluginInstance::contextAttachedAction()
{
	if (!open_gl_enabled_) {
		return kOfxStatReplyDefault;
	}
	return kOfxStatOK;
}

OfxStatus OlivePluginInstance::contextDetachedAction()
{
	if (!open_gl_enabled_) {
		return kOfxStatReplyDefault;
	}
	return kOfxStatOK;
}
#endif

double OlivePluginInstance::timeLineGetTime()
{
	if (active_viewer_provider_) {
		OakNodeNode viewer = active_viewer_provider_();
		if (viewer.ctx) {
			// Playhead as seconds
			int num = 0, den = 1;
			if (oaknode_sequence_get_playhead(
					oaknode_sequence_from_node(viewer),
					&num, &den) == OAKNODE_OK && den != 0) {
				return double(num) / den;
			}
		}
	}

	return 0.0;
}

void OlivePluginInstance::timeLineGotoTime(double t)
{
	if (active_viewer_provider_) {
		OakNodeNode viewer = active_viewer_provider_();
		if (viewer.ctx) {
			olive::core::Rational r = olive::core::Rational::from_double(t);
			oaknode_sequence_set_playhead(
				oaknode_sequence_from_node(viewer), r.numerator(),
				r.denominator());
		}
	}
}

void OlivePluginInstance::timeLineGetBounds(double &t1, double &t2)
{
	if (active_viewer_provider_) {
		OakNodeNode viewer = active_viewer_provider_();
		if (viewer.ctx) {
			int len_num = 0, len_den = 1;
			if (oaknode_sequence_get_length(
					oaknode_sequence_from_node(viewer), &len_num,
					&len_den) == OAKNODE_OK && len_den != 0) {
				t1 = 0.0;
				t2 = double(len_num) / len_den;
				return;
			}
		}
	}

	t1 = 0.0;
	t2 = 0.0;
}

void OlivePluginInstance::setCustomInArgs(const std::string &action,
										  OFX::Host::Property::Set &in_args)
{
	if (action == kOfxImageEffectActionRender ||
		action == kOfxImageEffectActionBeginSequenceRender ||
		action == kOfxImageEffectActionEndSequenceRender) {
		in_args.setIntProperty(kOfxImageEffectPropOpenGLEnabled,
							   open_gl_enabled_ ? 1 : 0);
	}
}

OFX::Host::ImageEffect::ClipInstance *OlivePluginInstance::newClipInstance(
	OFX::Host::ImageEffect::Instance *plugin,
	OFX::Host::ImageEffect::ClipDescriptor *descriptor, int index)
{
	// Create a new clip instance
	OliveClipInstance *clip_instance =
		new OliveClipInstance(plugin, *descriptor, params_);

	// Initialize base class clip properties from VideoParams so that
	// setupClipPreferencesArgs and plugin constructors (which may fetch
	// clips and query their properties before getClipPreferences is called)
	// have valid defaults instead of kOfxImageComponentNone / kOfxBitDepthNone.
	std::string depth = kOfxBitDepthFloat; // host default
	std::string comp = kOfxImageComponentRGBA; // host default

	int format = -1;
	int channels = 0;
	if (params_.ctx) {
		oakcommon_videoparams_get_format(params_, &format);
		oakcommon_videoparams_get_channel_count(params_, &channels);
	}

	switch (format) {
	case OAKCOMMON_PIXEL_FORMAT_U8:
		depth = kOfxBitDepthByte;
		break;
	case OAKCOMMON_PIXEL_FORMAT_U16:
		depth = kOfxBitDepthShort;
		break;
	case OAKCOMMON_PIXEL_FORMAT_F16:
		depth = kOfxBitDepthHalf;
		break;
	case OAKCOMMON_PIXEL_FORMAT_F32:
		depth = kOfxBitDepthFloat;
		break;
	default:
		break; // keep F32 default
	}

	switch (channels) {
	case 1:
		comp = kOfxImageComponentAlpha;
		break;
	case 3:
		comp = kOfxImageComponentRGB;
		break;
	case 4:
		comp = kOfxImageComponentRGBA;
		break;
	default:
		break; // keep RGBA default
	}

	clip_instance->setPixelDepth(depth);
	clip_instance->setComponents(comp);

	return clip_instance;
}

OlivePluginInstance::~OlivePluginInstance()
{
	if (node_.ctx) {
		register_node_instance(oaknode_node_identity(node_), nullptr);
	}
	_created = false;
	if (params_.ctx) {
		oakcommon_videoparams_free(&params_);
	}
}

}
}
