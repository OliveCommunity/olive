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

#include "node/color/colormanager/colormanager.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"

namespace olive
{

const QString OCIOLutNode::kFileInput = QStringLiteral("lut_file_in");
const QString OCIOLutNode::kDirectionInput = QStringLiteral("lut_dir_in");

#define super OCIOBaseNode

namespace {

bool IsSupportedLutExtension(const QString &suffix)
{
	const QString lower = suffix.toLower();
	return lower == QStringLiteral("cube") || lower == QStringLiteral("3dl");
}

} // namespace

OCIOLutNode::OCIOLutNode()
{
	AddInput(kFileInput, NodeValue::kFile, QString(),
			 InputFlags(kInputFlagNotKeyframable | kInputFlagNotConnectable));
	SetInputProperty(
		kFileInput, QStringLiteral("filter"),
		tr("LUT Files (*.cube *.3dl);;Cube LUT (*.cube);;3DL LUT (*.3dl);;All Files (*)"));
	SetInputProperty(kFileInput, QStringLiteral("placeholder"),
					 tr("Select a .cube or .3dl LUT file"));

	AddInput(kDirectionInput, NodeValue::kCombo, 0,
			 InputFlags(kInputFlagNotKeyframable | kInputFlagNotConnectable));

	qRegisterMetaType<olive::ColorProcessorPtr>();
}

QString OCIOLutNode::Name() const
{
	return tr("OCIO LUT");
}

QString OCIOLutNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.ociolut");
}

QVector<Node::CategoryID> OCIOLutNode::Category() const
{
	return { kCategoryColor };
}

QString OCIOLutNode::Description() const
{
	return tr("Applies a LUT file through OpenColorIO.");
}

void OCIOLutNode::Retranslate()
{
	super::Retranslate();

	SetInputName(kTextureInput, tr("Input"));
	SetInputName(kFileInput, tr("LUT File"));
	SetInputName(kDirectionInput, tr("Direction"));
	SetComboBoxStrings(kDirectionInput, { tr("Forward"), tr("Inverse") });
}

void OCIOLutNode::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(element)

	if (input == kFileInput || input == kDirectionInput) {
		GenerateProcessor();
	}
}

void OCIOLutNode::ConfigChanged()
{
	GenerateProcessor();
}

void OCIOLutNode::GenerateProcessor()
{
	QMutexLocker locker(&gen_mutex_);

	if (!manager()) {
		set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		return;
	}

	const QString path = GetStandardValue(kFileInput).toString();
	const int direction = GetStandardValue(kDirectionInput).toInt();

	if (path.isEmpty()) {
		set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		return;
	}

	// Re-use the existing processor if the file and direction haven't changed.
	if (path == last_path_ && direction == last_direction_ && last_processor_) {
		return;
	}

	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		qWarning() << "OCIO LUT file does not exist:" << path;
		set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		return;
	}

	const QString suffix = info.suffix();
	if (!IsSupportedLutExtension(suffix)) {
		qWarning() << "Unsupported OCIO LUT file extension:" << path;
		set_processor(nullptr);
		last_processor_.reset();
		last_path_.clear();
		last_direction_ = -1;
		return;
	}

	pending_path_ = path;
	pending_direction_ = direction;
	pending_generation_++;

	// Synchronous generation: for typical 33^3 .cube files this is well under
	// 100ms and avoids the complexity and potential deadlocks of background
	// generation + cache invalidation.
	ColorProcessorPtr processor;
	try {
		OCIO::FileTransformRcPtr transform = OCIO::FileTransform::Create();
		transform->setSrc(path.toUtf8().constData());
		transform->setInterpolation(OCIO::INTERP_LINEAR);
		transform->setDirection(
			static_cast<ColorProcessor::Direction>(direction) ==
					ColorProcessor::kNormal
				? OCIO::TRANSFORM_DIR_FORWARD
				: OCIO::TRANSFORM_DIR_INVERSE);

		processor = ColorProcessor::Create(manager()->GetConfig()->getProcessor(transform));
	} catch (const std::exception &e) {
		qWarning() << "OCIO LUT processor error:" << e.what();
		processor = nullptr;
	}

	last_path_ = path;
	last_direction_ = direction;
	last_processor_ = processor;
	set_processor(processor);

	// The processor has changed. Cancel any background caching first so the
	// preview autocacher doesn't try to re-cache the entire timeline, then
	// invalidate the cache so the current frame re-renders with the new LUT.
	PreviewAutoCacher *cacher = RenderManager::instance()->GetCacher();
	if (cacher) {
		cacher->CancelVideoTasks(true);
	}
	InvalidateAll(kTextureInput);
}

} // namespace olive
