/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "manageddisplay.h"

#include <QHBoxLayout>
#include <QMessageBox>

#include "panel/panelmanager.h"
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#endif
#include "render/opengl/openglrenderer.h"
#include "render/rendermanager.h"

namespace olive
{

#define super QWidget

ManagedDisplayWidget::ManagedDisplayWidget(QWidget *parent)
	: QWidget(parent)
	, color_manager_(nullptr)
	, color_service_(nullptr)
	, is_backend_neutral_(false)
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	// Create renderer
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	{
		auto *dynamic_renderer = new DynamicRenderer(
			RenderManager::backend_to_string(
				RenderManager::instance()->requested_backend()),
			this);
		if (!dynamic_renderer->load()) {
			qWarning()
				<< "Failed to load dynamic render backend for viewer, falling back to OpenGL";
			delete dynamic_renderer;
			attached_renderer_ = new OpenGLRenderer(this);
		} else {
			attached_renderer_ = dynamic_renderer;
		}
	}
#else
	attached_renderer_ = new OpenGLRenderer(this);
#endif

	if (attached_renderer_->is_open_gl()) {
		// OpenGL path
		inner_widget_ = new ManagedDisplayWidgetOpenGL();
		inner_widget_->setAttribute(Qt::WA_TranslucentBackground, false);
		connect(static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_),
				&ManagedDisplayWidgetOpenGL::on_init, this,
				&ManagedDisplayWidget::on_init, Qt::DirectConnection);
		connect(static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_),
				&ManagedDisplayWidgetOpenGL::on_destroy, this,
				&ManagedDisplayWidget::on_destroy, Qt::DirectConnection);
		connect(static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_),
				&ManagedDisplayWidgetOpenGL::on_paint, this,
				&ManagedDisplayWidget::on_paint, Qt::DirectConnection);
		connect(static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_),
				&ManagedDisplayWidgetOpenGL::frameSwapped, this,
				&ManagedDisplayWidget::frame_swapped, Qt::DirectConnection);

		inner_widget_->installEventFilter(this);

		// Create widget wrapper for OpenGL window
#ifdef USE_QOPENGLWINDOW
		wrapper_ = QWidget::createWindowContainer(
			static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_));
#else
		wrapper_ = inner_widget_;
#endif
		layout->addWidget(wrapper_);
	} else {
		// Backend-neutral path (Vulkan, etc.)
		is_backend_neutral_ = true;
		auto *bn_widget = new ManagedDisplayWidgetBackendNeutral(this);
		inner_widget_ = bn_widget;
		inner_widget_->setAttribute(Qt::WA_OpaquePaintEvent);
		inner_widget_->installEventFilter(this);
		connect(bn_widget, &ManagedDisplayWidgetBackendNeutral::on_paint, this,
				&ManagedDisplayWidget::on_paint, Qt::DirectConnection);
		wrapper_ = inner_widget_;
		layout->addWidget(wrapper_);
	}
}

ManagedDisplayWidget::~ManagedDisplayWidget()
{
	if (!is_backend_neutral_) {
		MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR_INNER;

		disconnect(static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_),
				   &ManagedDisplayWidgetOpenGL::on_destroy, this,
				   &ManagedDisplayWidget::on_destroy);
	} else {
		on_destroy();
	}
}

void ManagedDisplayWidget::connect_color_manager(ColorManager *color_manager)
{
	if (color_manager_ == color_manager) {
		return;
	}

	if (color_manager_ != nullptr) {
		disconnect(color_manager_, &ColorManager::config_changed, this,
				   &ManagedDisplayWidget::color_config_changed);
		disconnect(color_manager_, &ColorManager::reference_space_changed, this,
				   &ManagedDisplayWidget::color_config_changed);
	}

	color_manager_ = color_manager;

	if (color_manager_ != nullptr) {
		connect(color_manager_, &ColorManager::config_changed, this,
				&ManagedDisplayWidget::color_config_changed);
		connect(color_manager_, &ColorManager::reference_space_changed, this,
				&ManagedDisplayWidget::color_config_changed);
	}

	color_config_changed();
	emit color_manager_changed(color_manager_);
}

ColorManager *ManagedDisplayWidget::color_manager() const
{
	return color_manager_;
}

void ManagedDisplayWidget::disconnect_color_manager()
{
	connect_color_manager(nullptr);
}

const ColorTransform &ManagedDisplayWidget::get_color_transform() const
{
	return color_transform_;
}

Menu *ManagedDisplayWidget::get_color_space_menu(QMenu *parent, bool auto_connect)
{
	QStringList colorspaces = color_manager()->list_available_colorspaces();

	Menu *ocio_colorspace_menu = new Menu(tr("Color Space"), parent);

	if (auto_connect) {
		connect(ocio_colorspace_menu, &Menu::triggered, this,
				&ManagedDisplayWidget::menu_colorspace_select);
	}

	foreach (const QString &c, colorspaces) {
		QAction *action = ocio_colorspace_menu->addAction(c);
		action->setCheckable(true);
		action->setChecked(color_transform_.output() == c);
		action->setData(c);
	}

	return ocio_colorspace_menu;
}

void ManagedDisplayWidget::color_config_changed()
{
	if (!color_manager_) {
		color_service_ = nullptr;
		return;
	}

	// When no explicit transform has been chosen, default to the config's
	// display/view transform so the viewer shows a sensible image. Otherwise
	// an empty transform falls back to the project's default input colorspace,
	// which is usually a scene-referred space (e.g. ACEScg / Linear) and makes
	// the picture look raw/wrong on a monitor.
	if (color_transform_.output().isEmpty()) {
		QString display = color_manager_->get_default_display();
		QString view = color_manager_->get_default_view(display);
		set_color_transform(color_manager_->get_compliant_color_space(
			ColorTransform(display, view, QString()), true));
	} else {
		set_color_transform(
			color_manager_->get_compliant_color_space(color_transform_, false));
	}
}

ColorProcessorPtr ManagedDisplayWidget::color_service()
{
	return color_service_;
}

void ManagedDisplayWidget::show_default_context_menu()
{
	Menu m(this);

	if (color_manager_) {
		m.addMenu(get_color_space_menu(&m));
		m.addSeparator();
		m.addMenu(get_display_menu(&m));
		m.addMenu(get_view_menu(&m));
		m.addMenu(get_look_menu(&m));
	} else {
		QAction *a = m.addAction(tr("No color manager connected"));
		a->setEnabled(false);
	}

	m.exec(QCursor::pos());
}

void ManagedDisplayWidget::menu_display_select(QAction *action)
{
	const ColorTransform &old_transform = get_color_transform();

	ColorTransform new_transform = color_manager()->get_compliant_color_space(
		ColorTransform(action->data().toString(), old_transform.view(),
					   old_transform.look()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_view_select(QAction *action)
{
	const ColorTransform &old_transform = get_color_transform();

	ColorTransform new_transform = color_manager()->get_compliant_color_space(
		ColorTransform(old_transform.display(), action->data().toString(),
					   old_transform.look()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_look_select(QAction *action)
{
	const ColorTransform &old_transform = get_color_transform();

	ColorTransform new_transform = color_manager()->get_compliant_color_space(
		ColorTransform(old_transform.display(), old_transform.view(),
					   action->data().toString()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_colorspace_select(QAction *action)
{
	set_color_transform(color_manager()->get_compliant_color_space(
		ColorTransform(action->data().toString())));
}

void ManagedDisplayWidget::on_destroy()
{
	attached_renderer_->destroy();
	attached_renderer_->post_destroy();
}

void ManagedDisplayWidget::set_color_transform(const ColorTransform &transform)
{
	color_transform_ = transform;

	setup_color_processor();

	ColorProcessorChangedEvent();
}

void ManagedDisplayWidget::on_init()
{
	if (!is_backend_neutral_) {
		QOpenGLContext *context =
			static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_)->context();
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
		if (auto *dynamic_renderer =
				dynamic_cast<DynamicRenderer *>(attached_renderer_)) {
			dynamic_renderer->init_with_open_gl_context(context);
			dynamic_renderer->post_init();
			return;
		}
#endif
		static_cast<OpenGLRenderer *>(attached_renderer_)->init(context);
		static_cast<OpenGLRenderer *>(attached_renderer_)->post_init();
	} else {
		attached_renderer_->init();
		attached_renderer_->post_init();
	}
}

void ManagedDisplayWidget::enable_default_context_menu()
{
	connect(this, &ManagedDisplayWidget::customContextMenuRequested, this,
			&ManagedDisplayWidget::show_default_context_menu);
}

void ManagedDisplayWidget::ColorProcessorChangedEvent()
{
	update();
}

void ManagedDisplayWidget::make_current()
{
	if (!is_backend_neutral_) {
		static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_)->makeCurrent();
	}
}

void ManagedDisplayWidget::done_current()
{
	if (!is_backend_neutral_) {
		static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_)->doneCurrent();
	}
}

QPaintDevice *ManagedDisplayWidget::paint_device() const
{
	return inner_widget_;
}

void ManagedDisplayWidget::set_inner_mouse_tracking(bool e)
{
	if (wrapper_) {
		wrapper_->setMouseTracking(e);
	}
}

VideoParams ManagedDisplayWidget::get_viewport_params() const
{
	int device_width = width() * devicePixelRatioF();
	int device_height = height() * devicePixelRatioF();
	PixelFormat device_format = static_cast<PixelFormat::Format>(
		OAK_CONFIG("OfflinePixelFormat").toInt());
	return VideoParams(device_width, device_height, device_format,
					   VideoParams::k_internal_channel_count);
}

void ManagedDisplayWidget::update()
{
	if (inner_widget_) {
		inner_widget_->update();
	}
}

bool ManagedDisplayWidget::eventFilter(QObject *o, QEvent *e)
{
	if (o != inner_widget_) {
		return super::eventFilter(o, e);
	}

	switch (e->type()) {
	case QEvent::FocusIn:
		// HACK: QWindow focus isn't accounted for in QApplication::focusChanged, so we handle it
		//       manually here.
		PanelManager::instance()->focus_changed(nullptr, this);
		break;
	case QEvent::ContextMenu: {
		QContextMenuEvent *ctx = static_cast<QContextMenuEvent *>(e);
		emit customContextMenuRequested(ctx->pos());
		return true;
	}
	case QEvent::MouseButtonPress: {
		// HACK: QWindows don't seem to receive ContextMenu events on right click (only when pressing
		//       the menu button on the keyboard) so we handle it manually here
		/*QMouseEvent *ev = static_cast<QMouseEvent*>(e);
    if (ev->button() == Qt::RightButton) {
      emit customContextMenuRequested(ev->pos());
      return true;
    }*/
		break;
	}
	default:
		break;
	}

	return super::eventFilter(o, e);
}

Menu *ManagedDisplayWidget::get_display_menu(QMenu *parent, bool auto_connect)
{
	QStringList displays = color_manager()->list_available_displays();

	Menu *ocio_display_menu = new Menu(tr("Display"), parent);

	if (auto_connect) {
		connect(ocio_display_menu, &Menu::triggered, this,
				&ManagedDisplayWidget::menu_display_select);
	}

	foreach (const QString &d, displays) {
		QAction *action = ocio_display_menu->addAction(d);
		action->setCheckable(true);
		action->setChecked(color_transform_.display() == d);
		action->setData(d);
	}

	return ocio_display_menu;
}

Menu *ManagedDisplayWidget::get_view_menu(QMenu *parent, bool auto_connect)
{
	QStringList views =
		color_manager()->list_available_views(color_transform_.display());

	Menu *ocio_view_menu = new Menu(tr("View"), parent);

	if (auto_connect) {
		connect(ocio_view_menu, &Menu::triggered, this,
				&ManagedDisplayWidget::menu_view_select);
	}

	foreach (const QString &v, views) {
		QAction *action = ocio_view_menu->addAction(v);
		action->setCheckable(true);
		action->setChecked(color_transform_.view() == v);
		action->setData(v);
	}

	return ocio_view_menu;
}

Menu *ManagedDisplayWidget::get_look_menu(QMenu *parent, bool auto_connect)
{
	QStringList looks = color_manager()->list_available_looks();

	Menu *ocio_look_menu = new Menu(tr("Look"), parent);

	if (auto_connect) {
		connect(ocio_look_menu, &Menu::triggered, this,
				&ManagedDisplayWidget::menu_look_select);
	}

	// Setup "no look" action
	QAction *no_look_action = ocio_look_menu->addAction(tr("(None)"));
	no_look_action->setCheckable(true);
	no_look_action->setChecked(color_transform_.look().isEmpty());
	no_look_action->setData(QString());

	// Set up the rest of the looks
	foreach (const QString &l, looks) {
		QAction *action = ocio_look_menu->addAction(l);
		action->setCheckable(true);
		action->setChecked(color_transform_.look() == l);
		action->setData(l);
	}

	return ocio_look_menu;
}

void ManagedDisplayWidget::setup_color_processor()
{
	color_service_ = nullptr;

	if (color_manager_) {
		// (Re)create color processor
		try {
			color_service_ = ColorProcessor::create(
				color_manager_, color_manager_->get_reference_color_space(),
				color_transform_);
		} catch (ocio::Exception &e) {
			QMessageBox::critical(
				this, tr("OpenColorIO Error"),
				tr("Failed to set color configuration: %1").arg(e.what()),
				QMessageBox::Ok);
		}
	} else {
		color_service_ = nullptr;
	}

	emit color_processor_changed(color_service_);
}

}
