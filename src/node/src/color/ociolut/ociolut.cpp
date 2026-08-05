/***

  Oak - Non-Linear Video Editor
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

#include "ociolut.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <mutex>

#include "color/colormanager/colormanager.h"
#include "render/lutlibrary.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"

namespace olive
{

const std::string OCIOLutNode::k_file_input = "lut_file_in";
const std::string OCIOLutNode::k_direction_input = "lut_dir_in";

#define super OCIOBaseNode

namespace
{

bool is_main_process()
{
	// Qt-free replacement for qobject_cast<QApplication*>(QCoreApplication::instance()):
	// only the main GUI process creates a RenderManager (the render worker
	// never does), so its presence identifies the main process.
	return RenderManager::instance() != nullptr;
}

int read_direction_input(const Node *node)
{
	Variant v = node->get_standard_value(OCIOLutNode::k_direction_input);

	bool ok = false;
	int direction = v.to_int(&ok);
	if (ok) {
		return direction;
	}

	// Some old serializers stored the combo value as a string.
	std::string s = v.to_string();
	std::transform(s.begin(), s.end(), s.begin(),
				   [](unsigned char c) { return std::tolower(c); });
	if (s == "forward" || s == "0") {
		return 0;
	}
	if (s == "inverse" || s == "1") {
		return 1;
	}

	fprintf(stderr, "OCIOLutNode: unexpected direction value %s\n",
			v.to_string().c_str());
	return 0;
}

} // namespace

OCIOLutNode::OCIOLutNode()
{
	add_input(k_file_input, NodeValue::k_file, std::string(),
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));
	const StringList &extensions = LUTLibrary::supported_extensions();
	std::string all_luts = "*.";
	for (size_t i = 0; i < extensions.size(); i++) {
		if (i > 0) {
			all_luts += " *.";
		}
		all_luts += extensions[i];
	}
	set_input_property(k_file_input, "filter",
					 "LUT Files (" + all_luts + ");;All Files (*)");
	set_input_property(k_file_input, "placeholder", "Select a LUT file");
	// Allow the UI to offer the global LUT library for this input
	set_input_property(k_file_input, "lut_library", true);

	add_input(k_direction_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));
}

std::string OCIOLutNode::name() const
{
	return "OCIO LUT";
}

std::string OCIOLutNode::id() const
{
	return "org.olivevideoeditor.Olive.ociolut";
}

std::vector<Node::CategoryID> OCIOLutNode::category() const
{
	return { k_category_color };
}

std::string OCIOLutNode::description() const
{
	return "Applies a LUT file through OpenColorIO.";
}

void OCIOLutNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_file_input, "LUT File");
	set_input_name(k_direction_input, "Direction");
	set_combo_box_strings(k_direction_input, { "Forward", "Inverse" });
}

void OCIOLutNode::InputValueChangedEvent(const std::string &input, int element)
{
	(void) element;

	if (input == k_file_input || input == k_direction_input) {
		// In the worker process, creating the OCIO processor can be slow and we
		// are often called from LoadGraph while the main process is blocked
		// waiting for a response. Defer generation to Value() time so the worker
		// can ack the graph load immediately.
		if (is_main_process()) {
			generate_processor();
		} else {
			std::lock_guard<std::mutex> locker(gen_mutex_);
			processor_dirty_ = true;
		}
	}
}

void OCIOLutNode::config_changed()
{
	if (is_main_process()) {
		generate_processor();
	} else {
		std::lock_guard<std::mutex> locker(gen_mutex_);
		processor_dirty_ = true;
	}
}

void OCIOLutNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						NodeValueTable *table) const
{
	// Ensure the processor is up-to-date before the base class emits the color
	// transform job. This is especially important in the render worker, where
	// processor creation is deferred until the first render.
	ensure_processor();

	super::value(value, globals, table);
}

void OCIOLutNode::generate_processor()
{
	ensure_processor();

	// The processor has changed. In the main GUI process, refresh the viewer by
	// invalidating the cache and cancelling background cache jobs.
	// Invalidating first ensures any in-flight renders that complete afterwards
	// won't write stale frames back. The worker process has no
	// RenderManager/PreviewAutoCacher, so skip this step to avoid crashing.
	if (is_main_process()) {
		invalidate_all(k_texture_input);
		if (RenderManager *rm = RenderManager::instance()) {
			if (PreviewAutoCacher *cacher = rm->get_cacher()) {
				cacher->cancel_video_tasks(false);
			}
		}
	}
}

void OCIOLutNode::ensure_processor() const
{
	std::lock_guard<std::mutex> locker(gen_mutex_);

	if (!processor_dirty_ && last_processor_ &&
		get_standard_value(k_file_input).to_string() == last_path_ &&
		read_direction_input(this) == last_direction_) {
		return;
	}

	create_processor_from_inputs();
}

void OCIOLutNode::set_last_error(const std::string &error) const
{
	if (last_error_ == error) {
		return;
	}

	last_error_ = error;

	// The Qt version surfaced the error on the main window status bar through
	// EngineCore; that layer is out of oaknode, so the error is now only
	// recorded here and surfaced via last_error().
}

bool OCIOLutNode::create_processor_from_inputs() const
{
	if (!manager()) {
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		return false;
	}

	const std::string path = get_standard_value(k_file_input).to_string();
	const int direction = read_direction_input(this);

	if (path.empty()) {
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error(std::string());
		return false;
	}

	// Re-use the existing processor if the file and direction haven't changed.
	if (path == last_path_ && direction == last_direction_ && last_processor_) {
		processor_dirty_ = false;
		return false;
	}

	std::error_code fs_ec;
	const bool is_file =
		std::filesystem::is_regular_file(path, fs_ec) && !fs_ec;
	if (!is_file) {
		fprintf(stderr, "OCIO LUT file does not exist: %s\n", path.c_str());
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error("OCIO LUT: file does not exist: " + path);
		return false;
	}

	std::string suffix = std::filesystem::path(path).extension().string();
	if (!suffix.empty() && suffix.front() == '.') {
		suffix.erase(suffix.begin());
	}
	if (!LUTLibrary::is_supported_extension(suffix)) {
		fprintf(stderr, "Unsupported OCIO LUT file extension: %s\n",
				path.c_str());
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error("OCIO LUT: unsupported LUT file extension: " + path);
		return false;
	}

	ColorProcessorPtr processor;
	try {
		const bool forward = static_cast<ColorProcessor::Direction>(
								 direction) == ColorProcessor::k_normal;
		fprintf(stderr,
				"OCIOLutNode: creating processor for %s direction=%d "
				"ocio_dir=%s process=%s\n",
				path.c_str(), direction, forward ? "FORWARD" : "INVERSE",
				is_main_process() ? "main" : "worker");

		ocio::FileTransformRcPtr transform = ocio::FileTransform::Create();
		transform->setSrc(path.c_str());
		transform->setInterpolation(ocio::INTERP_LINEAR);
		transform->setDirection(forward ? ocio::TRANSFORM_DIR_FORWARD :
										  ocio::TRANSFORM_DIR_INVERSE);

		processor = ColorProcessor::create(
			manager()->get_config()->getProcessor(transform));
	} catch (const std::exception &e) {
		fprintf(stderr, "OCIO LUT processor error: %s\n", e.what());
		processor = nullptr;
	}

	if (!processor) {
		set_last_error("OCIO LUT: failed to load LUT file: " + path);
	} else {
		set_last_error(std::string());
	}

	last_path_ = path;
	last_direction_ = direction;
	last_processor_ = processor;
	const_cast<OCIOLutNode *>(this)->set_processor(processor);
	processor_dirty_ = false;

	return true;
}

} // namespace olive
