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

#ifndef OAK_COLORPROCESSORHANDLE_H
#define OAK_COLORPROCESSORHANDLE_H

#include <memory>

#include <QString>
#include <QStringList>

#include <oakengine/color.h>
#include <olive/core/core.h>

#include "render/colortransform.h"

namespace olive
{

class ColorManager;

/**
 * @brief App-side replacement for the engine's former ManagedColor class.
 *
 * The engine-side class was removed during the C ABI migration; this
 * header-only equivalent lives entirely in application code (every method
 * is inline), so no olive::ManagedColor symbol is imported from the engine
 * shared library. It pairs an RGBA color with the color-space input id and
 * the output transform, exactly like the original.
 */
class ManagedColor : public Color {
public:
	ManagedColor() = default;
	ManagedColor(const double &r, const double &g, const double &b,
				 const double &a = 1.0)
		: Color(r, g, b, a)
	{
	}
	ManagedColor(const char *data, const core::PixelFormat &format,
				 int channel_layout)
		: Color(data, format, channel_layout)
	{
	}
	ManagedColor(const Color &c)
		: Color(c)
	{
	}

	const QString &color_input() const
	{
		return color_input_;
	}
	void set_color_input(const QString &color_input)
	{
		color_input_ = color_input;
	}

	const ColorTransform &color_output() const
	{
		return color_transform_;
	}
	void set_color_output(const ColorTransform &color_output)
	{
		color_transform_ = color_output;
	}

private:
	QString color_input_;

	ColorTransform color_transform_;
};

/**
 * @brief Opaque-handle replacement for ColorProcessorPtr
 *
 * The color processor object lives behind the engine's C ABI
 * (oakengine/color.h); application code only ever holds this shared
 * handle, so no olive::ColorProcessor symbol is imported from the engine
 * library.
 */
struct ColorProcessorHandleDeleter {
	void operator()(OakEngineColorProcessor *p) const
	{
		oakengine_color_processor_free(p);
	}
};

using ColorProcessorHandlePtr = std::shared_ptr<OakEngineColorProcessor>;

/**
 * @brief Reinterpret an engine ColorManager pointer as its borrowed facade
 * handle (the same reinterpret pattern the other facade families use).
 */
inline OakEngineColorManager *oak_color_manager(ColorManager *mgr)
{
	return reinterpret_cast<OakEngineColorManager *>(mgr);
}

inline const OakEngineColorManager *oak_color_manager(const ColorManager *mgr)
{
	return reinterpret_cast<const OakEngineColorManager *>(mgr);
}

/**
 * @brief Run a buf/size facade string getter and return the QString.
 */
template <typename Fn> QString oak_query_string(Fn &&fn)
{
	const int len = fn(nullptr, 0);
	if (len <= 0) {
		return QString();
	}
	QByteArray buf(len + 1, 0);
	fn(buf.data(), buf.size());
	return QString::fromUtf8(buf.constData(), len);
}

/**
 * @brief Run a count/at facade list getter pair and return the QStringList.
 */
template <typename CountFn, typename AtFn>
QStringList oak_query_string_list(CountFn &&count_fn, AtFn &&at_fn)
{
	QStringList list;
	const int count = count_fn();
	list.reserve(count);
	for (int i = 0; i < count; i++) {
		list.append(oak_query_string(
			[&](char *buf, int size) { return at_fn(i, buf, size); }));
	}
	return list;
}

/**
 * @brief Convert an olive::ColorTransform to the facade POD. The QByteArray
 * outputs back the POD's pointers and must outlive its use.
 */
inline oak_color_transform oak_to_transform(const ColorTransform &t,
											QByteArray *output,
											QByteArray *view,
											QByteArray *look)
{
	*output = t.output().toUtf8();
	*view = t.view().toUtf8();
	*look = t.look().toUtf8();
	oak_color_transform pod;
	pod.is_display = t.is_display() ? 1 : 0;
	pod.output = output->constData();
	pod.view = view->constData();
	pod.look = look->constData();
	return pod;
}

/**
 * @brief ColorManager::get_compliant_color_space(ColorTransform) through
 * the facade.
 */
inline ColorTransform oak_compliant_transform(ColorManager *mgr,
											  const ColorTransform &in,
											  bool force_display = false)
{
	QByteArray o, v, l;
	const oak_color_transform pod = oak_to_transform(in, &o, &v, &l);
	int is_display = 0;
	char out[256], view[256], look[256];
	if (oakengine_color_manager_compliant_transform(
			oak_color_manager(mgr), &pod, force_display ? 1 : 0, &is_display,
			out, sizeof(out), view, sizeof(view), look, sizeof(look)) !=
		OAKENGINE_OK) {
		return in;
	}
	if (is_display) {
		return ColorTransform(QString::fromUtf8(out), QString::fromUtf8(view),
							  QString::fromUtf8(look));
	}
	return ColorTransform(QString::fromUtf8(out));
}

/**
 * @brief ColorManager::get_compliant_color_space(QString) through the
 * facade.
 */
inline QString oak_compliant_color_space(ColorManager *mgr, const QString &s)
{
	const QByteArray name = s.toUtf8();
	return oak_query_string([&](char *buf, int size) {
		return oakengine_color_manager_compliant_color_space(
			oak_color_manager(mgr), name.constData(), buf, size);
	});
}

/**
 * @brief ColorProcessor::create() through the facade (never throws; an
 * OCIO failure yields a handle for which IsValid() is false).
 */
inline ColorProcessorHandlePtr
oak_make_color_processor(ColorManager *mgr, const QString &input,
						 const ColorTransform &dest,
						 int direction = OAKENGINE_COLOR_PROCESSOR_NORMAL)
{
	QByteArray o, v, l;
	const oak_color_transform pod = oak_to_transform(dest, &o, &v, &l);
	const QByteArray in = input.toUtf8();
	return ColorProcessorHandlePtr(
		oakengine_color_processor_create(oak_color_manager(mgr),
										 in.constData(), &pod, direction),
		ColorProcessorHandleDeleter());
}

/**
 * @brief ColorProcessor::convert_color() through the facade. A null or
 * invalid processor passes the color through unchanged (matching the
 * engine's behavior).
 */
inline Color oak_convert_color(const ColorProcessorHandlePtr &proc,
							   const Color &in)
{
	double rgba[4] = { in.red(), in.green(), in.blue(), in.alpha() };
	double out[4];
	if (oakengine_color_processor_convert_color(proc.get(), rgba, out) ==
		OAKENGINE_OK) {
		return Color(out[0], out[1], out[2], out[3]);
	}
	return in;
}

} // namespace olive

#endif // OAK_COLORPROCESSORHANDLE_H
