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

#include "core.h"
#include "panel/panelmanager.h"
#include "oakengine/videoparams.h"
#include "oakengine/renderer.h"
#include "oakengine/display.h"
#include "widget/viewer/vieweroutpututils.h"
#include "common/configwrapper.h"

namespace olive
{

#define super QWidget

ManagedDisplayWidget::ManagedDisplayWidget(QWidget *parent)
	: QWidget(parent)
	, color_manager_(nullptr)
	, color_service_(nullptr)
	, is_backend_neutral_(false)
	, bridge_(new EngineEventBridge(this))
{
	QHBoxLayout *layout = new QHBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	// Create renderer
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	{
		const QString backend = oak_query_string([](char *buf, int sz) {
			int backend_id = oakengine_render_manager_requested_backend();
			return oakengine_render_manager_backend_to_string(backend_id, buf,
															  sz);
		});
		attached_renderer_ =
			oakengine_display_renderer_create_dynamic(
				backend.toUtf8().constData(), this);
		if (!attached_renderer_) {
			qWarning()
				<< "Failed to load dynamic render backend for viewer, falling back to OpenGL";
			attached_renderer_ =
				oakengine_display_renderer_create_opengl(this);
		}
	}
#else
	attached_renderer_ =
		oakengine_display_renderer_create_opengl(this);
#endif

	if (oakengine_display_renderer_is_open_gl(attached_renderer_)) {
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

	// Issue 20: connect color-manager bridge signals once in the constructor
	// so switching the observed color manager doesn't duplicate them.
	connect(bridge_, &EngineEventBridge::color_manager_config_changed, this,
			&ManagedDisplayWidget::color_config_changed);
	connect(bridge_,
			&EngineEventBridge::color_manager_reference_space_changed, this,
			&ManagedDisplayWidget::color_config_changed);

	// Issue 20: reuse the issue 7 undo signal so color-manager config changes
	// replayed from the undo stack refresh the color processor.
	connect(Core::instance(), &Core::undo_index_changed, this,
			&ManagedDisplayWidget::color_config_changed);
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

void ManagedDisplayWidget::connect_color_manager(OakEngineColorManager *color_manager)
{
	if (color_manager_ == color_manager) {
		return;
	}

	// Tear down previous color-manager subscriptions via the bridge.
	bridge_->unsubscribe_all();

	color_manager_ = color_manager;

	if (color_manager_ != nullptr) {
		// Subscribe through the bridge. Corresponding Qt signal connections
		// live in the constructor (issue 20).
		bridge_->subscribe(color_manager_,
						   OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED);
		bridge_->subscribe(
			color_manager_,
			OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED);
	}

	color_config_changed();
	emit color_manager_changed(color_manager_);
}

OakEngineColorManager *ManagedDisplayWidget::color_manager() const
{
	return color_manager_;
}

void ManagedDisplayWidget::disconnect_color_manager()
{
	connect_color_manager(nullptr);
}

const oak::ColorTransform &ManagedDisplayWidget::get_color_transform() const
{
	return color_transform_;
}

Menu *ManagedDisplayWidget::get_color_space_menu(QMenu *parent, bool auto_connect)
{
	QStringList colorspaces = oak_query_string_list(
		[this]() {
			return oakengine_color_manager_colorspace_count(color_manager_);
		},
		[this](int i, char *buf, int size) {
			return oakengine_color_manager_colorspace_at(color_manager_, i, buf,
														 size);
		});

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
		QString display = oak_query_string([this](char *buf, int size) {
			return oakengine_color_manager_default_display(color_manager_, buf,
														   size);
		});
		QString view = oak_query_string([this, &display](char *buf, int size) {
			QByteArray d = display.toUtf8();
			return oakengine_color_manager_default_view(color_manager_,
														d.constData(), buf,
														size);
		});
		set_color_transform(oak_compliant_transform(
			reinterpret_cast<olive::ColorManager *>(color_manager_),
			oak::ColorTransform(display, view, QString()), true));
	} else {
		set_color_transform(oak_compliant_transform(
			reinterpret_cast<olive::ColorManager *>(color_manager_),
			color_transform_, false));
	}
}

ColorProcessorHandlePtr ManagedDisplayWidget::color_service()
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
	const oak::ColorTransform &old_transform = get_color_transform();

	oak::ColorTransform new_transform = oak_compliant_transform(
		reinterpret_cast<olive::ColorManager *>(color_manager_),
		oak::ColorTransform(action->data().toString(), old_transform.view(),
					   old_transform.look()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_view_select(QAction *action)
{
	const oak::ColorTransform &old_transform = get_color_transform();

	oak::ColorTransform new_transform = oak_compliant_transform(
		reinterpret_cast<olive::ColorManager *>(color_manager_),
		oak::ColorTransform(old_transform.display(), action->data().toString(),
					   old_transform.look()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_look_select(QAction *action)
{
	const oak::ColorTransform &old_transform = get_color_transform();

	oak::ColorTransform new_transform = oak_compliant_transform(
		reinterpret_cast<olive::ColorManager *>(color_manager_),
		oak::ColorTransform(old_transform.display(), old_transform.view(),
					   action->data().toString()));

	set_color_transform(new_transform);
}

void ManagedDisplayWidget::menu_colorspace_select(QAction *action)
{
	set_color_transform(oak_compliant_transform(
		reinterpret_cast<olive::ColorManager *>(color_manager_),
		oak::ColorTransform(action->data().toString())));
}

void ManagedDisplayWidget::on_destroy()
{
	oakengine_display_renderer_destroy(attached_renderer_);
}

void ManagedDisplayWidget::set_color_transform(const oak::ColorTransform &transform)
{
	color_transform_ = transform;

	setup_color_processor();

	color_processor_changed_event();
}

void ManagedDisplayWidget::on_init()
{
	if (!is_backend_neutral_) {
		QOpenGLContext *context =
			static_cast<ManagedDisplayWidgetOpenGL *>(inner_widget_)->context();
		oakengine_display_renderer_init(attached_renderer_, context);
	} else {
		oakengine_display_renderer_init(attached_renderer_, nullptr);
	}
}

void ManagedDisplayWidget::enable_default_context_menu()
{
	connect(this, &ManagedDisplayWidget::customContextMenuRequested, this,
			&ManagedDisplayWidget::show_default_context_menu);
}

void ManagedDisplayWidget::color_processor_changed_event()
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

oak_video_params ManagedDisplayWidget::get_viewport_params() const
{
	int device_width = width() * devicePixelRatioF();
	int device_height = height() * devicePixelRatioF();
	int device_format = OAK_CONFIG("OfflinePixelFormat").toInt();
	oak_video_params pod = {};
	pod.width = device_width;
	pod.height = device_height;
	pod.format = device_format;
	return pod;
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
	QStringList displays = oak_query_string_list(
		[this]() {
			return oakengine_color_manager_display_count(color_manager_);
		},
		[this](int i, char *buf, int size) {
			return oakengine_color_manager_display_at(color_manager_, i, buf,
													  size);
		});

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
	QByteArray disp = color_transform_.display().toUtf8();
	QStringList views = oak_query_string_list(
		[this, &disp]() {
			return oakengine_color_manager_view_count(color_manager_,
													  disp.constData());
		},
		[this, &disp](int i, char *buf, int size) {
			return oakengine_color_manager_view_at(color_manager_,
												   disp.constData(), i, buf,
												   size);
		});

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
	QStringList looks = oak_query_string_list(
		[this]() {
			return oakengine_color_manager_look_count(color_manager_);
		},
		[this](int i, char *buf, int size) {
			return oakengine_color_manager_look_at(color_manager_, i, buf,
												   size);
		});

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
		// (Re)create color processor. The facade never throws: OCIO failures
		// are caught inside the engine and surface as an invalid processor.
		QString ref_cs = oak_query_string([this](char *buf, int size) {
			return oakengine_color_manager_reference_color_space(
				color_manager_, buf, size);
		});
		color_service_ = oak_make_color_processor(
			reinterpret_cast<olive::ColorManager *>(color_manager_),
			ref_cs, color_transform_);
	} else {
		color_service_ = nullptr;
	}

	emit color_processor_changed(color_service_);
}

}
