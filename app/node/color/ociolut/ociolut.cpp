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

#include <QFileInfo>
#include <QMetaObject>

#include <QApplication>

#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "render/lutlibrary.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"

namespace olive
{

const QString OCIOLutNode::k_file_input = QStringLiteral("lut_file_in");
const QString OCIOLutNode::k_direction_input = QStringLiteral("lut_dir_in");

#define super OCIOBaseNode

namespace
{

bool is_main_process()
{
	return qobject_cast<QApplication *>(QCoreApplication::instance()) !=
		   nullptr;
}

int read_direction_input(const Node *node)
{
	QVariant v = node->get_standard_value(OCIOLutNode::k_direction_input);

	bool ok = false;
	int direction = v.toInt(&ok);
	if (ok) {
		return direction;
	}

	// Some old serializers stored the combo value as a string.
	const QString s = v.toString().toLower();
	if (s == QStringLiteral("forward") || s == QStringLiteral("0")) {
		return 0;
	}
	if (s == QStringLiteral("inverse") || s == QStringLiteral("1")) {
		return 1;
	}

	qWarning() << "OCIOLutNode: unexpected direction value" << v;
	return 0;
}

} // namespace

OCIOLutNode::OCIOLutNode()
{
	add_input(k_file_input, NodeValue::k_file, QString(),
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));
	const QString all_luts =
		QStringLiteral("*.") +
		LUTLibrary::supported_extensions().join(QStringLiteral(" *."));
	set_input_property(k_file_input, QStringLiteral("filter"),
					 tr("LUT Files (%1);;All Files (*)").arg(all_luts));
	set_input_property(k_file_input, QStringLiteral("placeholder"),
					 tr("Select a LUT file"));
	// Allow the UI to offer the global LUT library for this input
	set_input_property(k_file_input, QStringLiteral("lut_library"), true);

	add_input(k_direction_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_keyframable | k_input_flag_not_connectable));

	qRegisterMetaType<olive::ColorProcessorPtr>();
}

QString OCIOLutNode::name() const
{
	return tr("OCIO LUT");
}

QString OCIOLutNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.ociolut");
}

QVector<Node::CategoryID> OCIOLutNode::category() const
{
	return { k_category_color };
}

QString OCIOLutNode::description() const
{
	return tr("Applies a LUT file through OpenColorIO.");
}

void OCIOLutNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_file_input, tr("LUT File"));
	set_input_name(k_direction_input, tr("Direction"));
	set_combo_box_strings(k_direction_input, { tr("Forward"), tr("Inverse") });
}

void OCIOLutNode::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(element)

	if (input == k_file_input || input == k_direction_input) {
		// In the worker process, creating the OCIO processor can be slow and we
		// are often called from LoadGraph while the main process is blocked
		// waiting for a response. Defer generation to Value() time so the worker
		// can ack the graph load immediately.
		if (is_main_process()) {
			generate_processor();
		} else {
			QMutexLocker locker(&gen_mutex_);
			processor_dirty_ = true;
		}
	}
}

void OCIOLutNode::config_changed()
{
	if (is_main_process()) {
		generate_processor();
	} else {
		QMutexLocker locker(&gen_mutex_);
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
	// won't write stale frames back. The worker process uses QGuiApplication and
	// has no RenderManager/PreviewAutoCacher, so skip this step to avoid crashing.
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
	QMutexLocker locker(&gen_mutex_);

	if (!processor_dirty_ && last_processor_ &&
		get_standard_value(k_file_input).toString() == last_path_ &&
		read_direction_input(this) == last_direction_) {
		return;
	}

	create_processor_from_inputs();
}

void OCIOLutNode::set_last_error(const QString &error) const
{
	if (last_error_ == error) {
		return;
	}

	last_error_ = error;

	// Make the error visible to the user instead of failing silently, but only
	// from the main process (the render worker has no status bar)
	if (!error.isEmpty() && is_main_process() && Core::instance()) {
		Core::instance()->show_status_bar_message(error, 10000);
	}
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

	const QString path = get_standard_value(k_file_input).toString();
	const int direction = read_direction_input(this);

	if (path.isEmpty()) {
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error(QString());
		return false;
	}

	// Re-use the existing processor if the file and direction haven't changed.
	if (path == last_path_ && direction == last_direction_ && last_processor_) {
		processor_dirty_ = false;
		return false;
	}

	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		qWarning() << "OCIO LUT file does not exist:" << path;
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error(tr("OCIO LUT: file does not exist: %1").arg(path));
		return false;
	}

	const QString suffix = info.suffix();
	if (!LUTLibrary::is_supported_extension(suffix)) {
		qWarning() << "Unsupported OCIO LUT file extension:" << path;
		const_cast<OCIOLutNode *>(this)->set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		processor_dirty_ = false;
		set_last_error(tr("OCIO LUT: unsupported LUT file extension: %1")
						   .arg(path));
		return false;
	}

	ColorProcessorPtr processor;
	try {
		const bool forward = static_cast<ColorProcessor::Direction>(
								 direction) == ColorProcessor::k_normal;
		qDebug() << "OCIOLutNode: creating processor for" << path
				 << "direction=" << direction
				 << "ocio_dir=" << (forward ? "FORWARD" : "INVERSE")
				 << "process=" << (is_main_process() ? "main" : "worker");

		ocio::FileTransformRcPtr transform = ocio::FileTransform::Create();
		transform->setSrc(path.toUtf8().constData());
		transform->setInterpolation(ocio::INTERP_LINEAR);
		transform->setDirection(forward ? ocio::TRANSFORM_DIR_FORWARD :
										  ocio::TRANSFORM_DIR_INVERSE);

		processor = ColorProcessor::create(
			manager()->get_config()->getProcessor(transform));
	} catch (const std::exception &e) {
		qWarning() << "OCIO LUT processor error:" << e.what();
		processor = nullptr;
	}

	if (!processor) {
		set_last_error(tr("OCIO LUT: failed to load LUT file: %1").arg(path));
	} else {
		set_last_error(QString());
	}

	last_path_ = path;
	last_direction_ = direction;
	last_processor_ = processor;
	const_cast<OCIOLutNode *>(this)->set_processor(processor);
	processor_dirty_ = false;

	return true;
}

} // namespace olive
