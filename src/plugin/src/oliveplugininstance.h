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
#ifndef OAK_OLIVE_INSTANCE_H
#define OAK_OLIVE_INSTANCE_H

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxhImageEffect.h"

#include "common/videoparams.h"
#include "node/node.h"
#include "pluginprogressreporter.h"
#include "undo/undocommand.h"

namespace olive
{
namespace plugin
{

/**
 * @brief Whether the calling thread is the registered main (GUI) thread
 *
 * The facade registers the main thread at startup with
 * set_main_thread_id(); before registration the first caller's thread
 * is treated as main (headless default).
 */
bool is_gui_thread();

void set_main_thread_id(std::thread::id id);

class PluginProgressReporter;
enum class ErrorType { error, warning, message };
struct PersistentErrors {
	ErrorType type;
	std::string message;
};

/**
 * @brief Provider returning the currently active viewer as an oaknode
 *        handle
 *
 * Registered by the facade. Without a provider, the OFX timeline suite
 * falls back to its safe defaults (current time 0, empty bounds,
 * seeking does nothing).
 */
using ActiveViewerProvider = std::function<OakNodeNode()>;

void set_active_viewer_provider(ActiveViewerProvider provider);

/**
 * @brief Undo submission callback (facade pushes onto its undo stack)
 *
 * The command handle is consumed by the callback (the facade stack
 * takes a reference; see oakundo push semantics). Without a callback,
 * commands are redone immediately and released.
 */
using UndoSubmitFn = std::function<void(OakUndoCommand command,
										const std::string &label)>;

void set_undo_submit_callback(UndoSubmitFn fn);

class OlivePluginInstance : public OFX::Host::ImageEffect::Instance {
public:
	OlivePluginInstance(OFX::Host::ImageEffect::ImageEffectPlugin *plugin,
						OFX::Host::ImageEffect::Descriptor &desc,
						const std::string &context, bool interactive)
		: OFX::Host::ImageEffect::Instance(plugin, desc, context, interactive)
	{
	}
	OlivePluginInstance(OlivePluginInstance &instance)
		: Instance(instance._plugin, *instance._descriptor, instance._context,
				   instance._interactive)
	{
		// Do NOT shallow-copy _clips: Instance::~Instance() deletes them,
		// which would cause a double-free. Clips are re-created in populate().
		_created = instance._created;
		_clipPrefsDirty = instance._clipPrefsDirty;
		_continuousSamples = instance._continuousSamples;
		_frameVarying = instance._frameVarying;
		_outputPreMultiplication = instance._outputPreMultiplication;
		_outputFielding = instance._outputFielding;
		_outputFrameRate = instance._outputFrameRate;
	}
	explicit OlivePluginInstance(Instance &instance)
		: Instance(instance) {};
	~OlivePluginInstance() override;
	const std::string &getDefaultOutputFielding() const override;

	void setVideoParam(OakVideoParams params)
	{
		if (params_.ctx) {
			oakcommon_videoparams_free(&params_);
		}
		params_ = params;
		if (params_.ctx) {
			params_.addref(params_.ctx);
		}
	}
	void set_node_handle(OakNodeNode node);
	OakNodeNode node_handle() const
	{
		return node_;
	}
	void setOpenGLEnabled(bool enabled)
	{
		open_gl_enabled_ = enabled;
	}
	bool isCreated() const
	{
		return _created;
	}
	OFX::Host::ImageEffect::ClipInstance *
	newClipInstance(OFX::Host::ImageEffect::Instance *plugin,
					OFX::Host::ImageEffect::ClipDescriptor *descriptor,
					int index) override;

	OfxStatus vmessage(const char *type, const char *id, const char *format,
					   va_list args) override;

	OfxStatus setPersistentMessage(const char *type, const char *id,
								   const char *format, va_list args) override;

	OfxStatus clearPersistentMessage() override;
	int persistent_message_count() const
	{
		return int(persistent_errors_.size());
	}
	const std::vector<PersistentErrors> &persistent_messages() const
	{
		return persistent_errors_;
	}

	void getProjectSize(double &x_size, double &y_size) const override;
	void getProjectOffset(double &x_offset, double &y_offset) const override;
	void getProjectExtent(double &x_size, double &y_size) const override;
	// The pixel aspect ratio of the current project
	double getProjectPixelAspectRatio() const override;

	// The duration of the effect
	// This contains the duration of the plug-in effect, in frames.
	double getEffectDuration() const override;

	// For an instance, this is the frame rate of the project the effect is in.
	double getFrameRate() const override;

	/// This is called whenever a param is changed by the plugin so that
	/// the recursive instanceChangedAction will be fed the correct frame
	double getFrameRecursive() const override;

	/// This is called whenever a param is changed by the plugin so that
	/// the recursive instanceChangedAction will be fed the correct
	/// renderScale
	void getRenderScaleRecursive(double &x, double &y) const override;

	////////////////////////////////////////////////////////////////////////////////
	// overridden for Param::SetInstance

	/// make a parameter instance
	OFX::Host::Param::Instance *
	newParam(const std::string &name,
			 OFX::Host::Param::Descriptor &descriptor) override;

	void submit_undo_command(OakUndoCommand command, const std::string &label);

	/// Triggered when the plug-in calls OfxParameterSuiteV1::paramEditBegin
	virtual OfxStatus editBegin(const std::string &name) override;

	/// Triggered when the plug-in calls OfxParameterSuiteV1::paramEditEnd
	virtual OfxStatus editEnd() override;

	////////////////////////////////////////////////////////////////////////////////
	// overridden for Progress::ProgressI

	/// Start doing progress.
	virtual void progressStart(const std::string &message,
							   const std::string &messageid) override;

	/// finish yer progress
	virtual void progressEnd() override;

	/// set the progress to some level of completion, returns
	/// false if you should abandon processing, true to continue
	virtual bool progressUpdate(double t) override;

#ifdef OFX_SUPPORTS_OPENGLRENDER
	virtual OfxStatus contextAttachedAction() override;
	virtual OfxStatus contextDetachedAction() override;
#endif

	////////////////////////////////////////////////////////////////////////////////
	// overridden for TimeLine::TimeLineI

	/// get the current time on the timeline. This is not necessarily the same
	/// time as being passed to an action (eg render)
	double timeLineGetTime() override;

	/// set the timeline to a specific time
	void timeLineGotoTime(double t) override;

	/// get the first and last times available on the effect's timeline
	void timeLineGetBounds(double &t1, double &t2) override;

	void setCustomInArgs(const std::string &action,
						 OFX::Host::Property::Set &in_args) override;

private:
	std::vector<PersistentErrors> persistent_errors_;
	OakVideoParams params_ = {};
	OakNodeNode node_ = {};
	int edit_depth_ = 0;
	OakUndoCommand edit_command_ = {};
	std::string edit_label_;
	std::string edit_first_label_;
	int edit_param_count_ = 0;
	std::unique_ptr<PluginProgressReporter> progress_reporter_;
	bool progress_cancelled_ = false;
	bool progress_active_ = false;
	bool open_gl_enabled_ = false;

public:
	std::mutex &mutex()
	{
		return mutex_;
	}

private:
	std::mutex mutex_;
};
}
}
#endif
