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

#include "oakengine/color.h"

#include <QString>

#include <cstring>

#include "colorinternal.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "render/job/colortransformjob.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"

// The OakEngineColorProcessor handle layout is shared with the other capi
// translation units via colorinternal.h.
struct OakEngineColorConfig {
	ocio::ConstConfigRcPtr ptr;
};

namespace
{

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

thread_local QString g_last_error;

void set_error(const QString &error)
{
	g_last_error = error;
}

olive::ColorManager *impl(const OakEngineColorManager *h)
{
	return reinterpret_cast<olive::ColorManager *>(
		const_cast<OakEngineColorManager *>(h));
}

olive::ColorTransform to_cpp(const oak_color_transform &t)
{
	if (t.is_display) {
		return olive::ColorTransform(
			t.output ? QString::fromUtf8(t.output) : QString(),
			t.view ? QString::fromUtf8(t.view) : QString(),
			t.look ? QString::fromUtf8(t.look) : QString());
	}
	return olive::ColorTransform(t.output ? QString::fromUtf8(t.output) :
											QString());
}

// The engine's list accessors dereference the config unconditionally;
// guard here so a manager whose config failed to load yields empty lists
// instead of crashing.
bool has_config(const olive::ColorManager *mgr)
{
	return mgr && mgr->get_config();
}

QString list_at(const QStringList &l, int index)
{
	return (index >= 0 && index < l.size()) ? l.at(index) : QString();
}

} // namespace

extern "C" {

int oakengine_color_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

OakEngineColorManager *
oakengine_color_manager_from_project(OakEngineProject *project)
{
	if (!project) {
		return nullptr;
	}
	auto *p = reinterpret_cast<olive::Project *>(project);
	return reinterpret_cast<OakEngineColorManager *>(p->color_manager());
}

int oakengine_color_manager_get_config_filename(
	const OakEngineColorManager *mgr, char *buf, int buf_size)
{
	if (!mgr) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(mgr)->get_config_filename(), buf, buf_size);
}

int oakengine_color_manager_set_config_filename(OakEngineColorManager *mgr,
												const char *filename)
{
	if (!mgr || !filename) {
		return OAKENGINE_E_INVALID;
	}
	impl(mgr)->set_config_filename(QString::fromUtf8(filename));
	return OAKENGINE_OK;
}

int oakengine_color_manager_colorspace_count(const OakEngineColorManager *mgr)
{
	if (!has_config(impl(mgr))) {
		return 0;
	}
	return impl(mgr)->list_available_colorspaces().size();
}

int oakengine_color_manager_colorspace_at(const OakEngineColorManager *mgr,
										  int index, char *buf, int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	const QString s =
		list_at(impl(mgr)->list_available_colorspaces(), index);
	if (s.isNull()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s, buf, buf_size);
}

int oakengine_color_manager_display_count(const OakEngineColorManager *mgr)
{
	if (!has_config(impl(mgr))) {
		return 0;
	}
	return impl(mgr)->list_available_displays().size();
}

int oakengine_color_manager_display_at(const OakEngineColorManager *mgr,
									   int index, char *buf, int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	const QString s = list_at(impl(mgr)->list_available_displays(), index);
	if (s.isNull()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s, buf, buf_size);
}

int oakengine_color_manager_view_count(const OakEngineColorManager *mgr,
									   const char *display)
{
	if (!has_config(impl(mgr))) {
		return 0;
	}
	return impl(mgr)
		->list_available_views(display ? QString::fromUtf8(display) : QString())
		.size();
}

int oakengine_color_manager_view_at(const OakEngineColorManager *mgr,
									const char *display, int index, char *buf,
									int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	const QString s = list_at(
		impl(mgr)->list_available_views(display ? QString::fromUtf8(display) :
												  QString()),
		index);
	if (s.isNull()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s, buf, buf_size);
}

int oakengine_color_manager_look_count(const OakEngineColorManager *mgr)
{
	if (!has_config(impl(mgr))) {
		return 0;
	}
	return impl(mgr)->list_available_looks().size();
}

int oakengine_color_manager_look_at(const OakEngineColorManager *mgr,
									int index, char *buf, int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	const QString s = list_at(impl(mgr)->list_available_looks(), index);
	if (s.isNull()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s, buf, buf_size);
}

int oakengine_color_manager_default_display(const OakEngineColorManager *mgr,
											char *buf, int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(mgr)->get_default_display(), buf, buf_size);
}

int oakengine_color_manager_default_view(const OakEngineColorManager *mgr,
										 const char *display, char *buf,
										 int buf_size)
{
	if (!has_config(impl(mgr))) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(
		impl(mgr)->get_default_view(display ? QString::fromUtf8(display) :
											  QString()),
		buf, buf_size);
}

int oakengine_color_manager_default_input_color_space(
	const OakEngineColorManager *mgr, char *buf, int buf_size)
{
	if (!mgr) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(mgr)->get_default_input_color_space(), buf,
						 buf_size);
}

int oakengine_color_manager_set_default_input_color_space(
	OakEngineColorManager *mgr, const char *colorspace)
{
	if (!mgr || !colorspace) {
		return OAKENGINE_E_INVALID;
	}
	impl(mgr)->set_default_input_color_space(QString::fromUtf8(colorspace));
	return OAKENGINE_OK;
}

int oakengine_color_manager_reference_color_space(
	const OakEngineColorManager *mgr, char *buf, int buf_size)
{
	if (!mgr) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(mgr)->get_reference_color_space(), buf,
						 buf_size);
}

int oakengine_color_manager_default_luma_coefs(
	const OakEngineColorManager *mgr, double *rgb)
{
	if (!mgr || !rgb) {
		return OAKENGINE_E_INVALID;
	}
	impl(mgr)->get_default_luma_coefs(rgb);
	return OAKENGINE_OK;
}

int oakengine_color_manager_compliant_color_space(
	const OakEngineColorManager *mgr, const char *name, char *buf,
	int buf_size)
{
	if (!has_config(impl(mgr)) || !name) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(
		impl(mgr)->get_compliant_color_space(QString::fromUtf8(name)), buf,
		buf_size);
}

int oakengine_color_manager_compliant_transform(
	const OakEngineColorManager *mgr, const oak_color_transform *in,
	int force_display, int *out_is_display, char *out_output,
	int output_size, char *out_view, int view_size, char *out_look,
	int look_size)
{
	if (!has_config(impl(mgr)) || !in) {
		return OAKENGINE_E_INVALID;
	}
	const olive::ColorTransform compliant =
		impl(mgr)->get_compliant_color_space(to_cpp(*in), force_display != 0);
	if (out_is_display) {
		*out_is_display = compliant.is_display() ? 1 : 0;
	}
	string_to_buf(compliant.output(), out_output, output_size);
	string_to_buf(compliant.view(), out_view, view_size);
	string_to_buf(compliant.look(), out_look, look_size);
	return OAKENGINE_OK;
}

OakEngineColorConfig *oakengine_color_config_load_default(void)
{
	try {
		ocio::ConstConfigRcPtr c = olive::ColorManager::get_default_config();
		if (!c) {
			set_error(QStringLiteral("no default OCIO config available"));
			return nullptr;
		}
		set_error(QString());
		return new OakEngineColorConfig{std::move(c)};
	} catch (ocio::Exception &e) {
		set_error(QString::fromUtf8(e.what()));
		return nullptr;
	}
}

OakEngineColorConfig *oakengine_color_config_load_file(const char *filename)
{
	if (!filename) {
		set_error(QStringLiteral("no filename given"));
		return nullptr;
	}
	try {
		ocio::ConstConfigRcPtr c =
			olive::ColorManager::create_config_from_file(
				QString::fromUtf8(filename));
		set_error(QString());
		return new OakEngineColorConfig{std::move(c)};
	} catch (ocio::Exception &e) {
		set_error(QString::fromUtf8(e.what()));
		return nullptr;
	}
}

void oakengine_color_config_free(OakEngineColorConfig *config)
{
	delete config;
}

int oakengine_color_config_colorspace_count(const OakEngineColorConfig *config)
{
	if (!config || !config->ptr) {
		return 0;
	}
	return olive::ColorManager::list_available_colorspaces(config->ptr).size();
}

int oakengine_color_config_colorspace_at(const OakEngineColorConfig *config,
										 int index, char *buf, int buf_size)
{
	if (!config || !config->ptr) {
		return OAKENGINE_E_INVALID;
	}
	const QString s = list_at(
		olive::ColorManager::list_available_colorspaces(config->ptr), index);
	if (s.isNull()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(s, buf, buf_size);
}

OakEngineColorProcessor *oakengine_color_processor_create(
	const OakEngineColorManager *mgr, const char *input,
	const oak_color_transform *dest, int direction)
{
	if (!mgr || !input || !dest ||
		(direction != OAKENGINE_COLOR_PROCESSOR_NORMAL &&
		 direction != OAKENGINE_COLOR_PROCESSOR_INVERSE)) {
		return nullptr;
	}
	// ColorProcessor catches OCIO failures internally and leaves the
	// processor null (see engine/render/colorprocessor.cpp), so this never
	// throws; validity is reported through is_valid().
	auto *proc = new OakEngineColorProcessor;
	proc->ptr = olive::ColorProcessor::create(
		impl(mgr), QString::fromUtf8(input), to_cpp(*dest),
		direction == OAKENGINE_COLOR_PROCESSOR_INVERSE ?
			olive::ColorProcessor::k_inverse :
			olive::ColorProcessor::k_normal);
	return proc;
}

void oakengine_color_processor_free(OakEngineColorProcessor *proc)
{
	delete proc;
}

int oakengine_color_processor_is_valid(const OakEngineColorProcessor *proc)
{
	return (proc && proc->ptr && proc->ptr->get_processor()) ? 1 : 0;
}

int oakengine_color_processor_convert_color(
	const OakEngineColorProcessor *proc, const double *in_rgba,
	double *out_rgba)
{
	if (!proc || !proc->ptr || !in_rgba || !out_rgba) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Color out = proc->ptr->convert_color(
		olive::Color(in_rgba[0], in_rgba[1], in_rgba[2], in_rgba[3]));
	out_rgba[0] = out.red();
	out_rgba[1] = out.green();
	out_rgba[2] = out.blue();
	out_rgba[3] = out.alpha();
	return OAKENGINE_OK;
}

int oakengine_color_processor_id(const OakEngineColorProcessor *proc,
								 char *buf, int buf_size)
{
	if (!proc || !proc->ptr) {
		return OAKENGINE_E_INVALID;
	}
	const char *id = proc->ptr->id();
	const int len = id ? int(strlen(id)) : 0;
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", id ? id : "");
	}
	return len;
}

int oakengine_color_transform_job_set_processor(
	void *job, const OakEngineColorProcessor *proc)
{
	if (!job) {
		return OAKENGINE_E_INVALID;
	}
	auto *j = reinterpret_cast<olive::ColorTransformJob *>(job);
	j->set_color_processor(proc ? proc->ptr : olive::ColorProcessorPtr());
	return OAKENGINE_OK;
}

} // extern "C"
