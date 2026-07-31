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

#ifndef MANAGEDDISPLAYOBJECT_H
#define MANAGEDDISPLAYOBJECT_H

//#define USE_QOPENGLWINDOW

#include <QMouseEvent>
#include <QOpenGLContext>
#ifdef USE_QOPENGLWINDOW
#include <QOpenGLWindow>
#else
#include <QOpenGLWidget>
#endif

#include "oakengine/color.h"
#include "oakengine/display.h"
#include "oakengine/events.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "widget/menu/menu.h"

namespace olive
{

class ManagedDisplayWidgetOpenGL
#ifdef USE_QOPENGLWINDOW
	: public QOpenGLWindow
#else
	: public QOpenGLWidget
#endif
{
	Q_OBJECT
public:
	ManagedDisplayWidgetOpenGL() = default;

	virtual ~ManagedDisplayWidgetOpenGL() override
	{
		if (context()) {
			DestroyListener();
			disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
					   &ManagedDisplayWidgetOpenGL::DestroyListener);
		}
	}

signals:
	// Render signals
	void on_init();
	void on_paint();
	void on_destroy();

protected:
	virtual void initializeGL() override
	{
		connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
				&ManagedDisplayWidgetOpenGL::DestroyListener,
				Qt::DirectConnection);

		emit on_init();
	}

	virtual void paintGL() override
	{
		emit on_paint();
	}

private slots:
	void DestroyListener()
	{
		makeCurrent();

		emit on_destroy();

		doneCurrent();
	}
};

#define MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR_INNER \
	make_current();                                    \
	on_destroy();                                      \
	done_current()
#define MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR(x)     \
	virtual ~x() override                              \
	{                                                  \
		MANAGEDDISPLAYWIDGET_DEFAULT_DESTRUCTOR_INNER; \
	}

/**
 * @brief Inner widget for backend-neutral rendering paths (e.g. Vulkan).
 *
 * It does not own a GL context; instead it forwards Qt paint events to the
 * ManagedDisplayWidget so that the viewer can render via QPainter.
 */
class ManagedDisplayWidgetBackendNeutral : public QWidget {
	Q_OBJECT
public:
	ManagedDisplayWidgetBackendNeutral(QWidget *parent = nullptr)
		: QWidget(parent)
	{
	}

signals:
	void on_paint();

protected:
	virtual void paintEvent(QPaintEvent *event) override
	{
		QWidget::paintEvent(event);

		emit on_paint();
	}
};

class ManagedDisplayWidget : public QWidget {
	Q_OBJECT
public:
	ManagedDisplayWidget(QWidget *parent = nullptr);

	virtual ~ManagedDisplayWidget() override;

	/**
   * @brief Disconnect a ColorManager (equivalent to ConnectColorManager(nullptr))
   */
	void disconnect_color_manager();

	/**
   * @brief Access currently connected ColorManager (nullptr if none)
   */
	OakEngineColorManager *color_manager() const;

	/**
   * @brief Get current color transform
   */
	const oak::ColorTransform &get_color_transform() const;

	/**
   * @brief Get menu that can be used to select the colorspace
   */
	Menu *get_color_space_menu(QMenu *parent, bool auto_connect = true);

	/**
   * @brief Get menu that can be used to select the display transform
   */
	Menu *get_display_menu(QMenu *parent, bool auto_connect = true);

	/**
   * @brief Get menu that can be used to select the view transform
   */
	Menu *get_view_menu(QMenu *parent, bool auto_connect = true);

	/**
   * @brief Get menu that can be used to select the look transform
   */
	Menu *get_look_menu(QMenu *parent, bool auto_connect = true);

	/**
   * @brief Passes update signal through to inner widget
   */
	void update();

	virtual bool eventFilter(QObject *o, QEvent *e) override;

public slots:
	/**
   * @brief Replaces the color transform with a new one
   */
	void set_color_transform(const oak::ColorTransform &transform);

	/**
   * @brief Connect a ColorManager (ColorManagers usually belong to the Project)
   */
	void connect_color_manager(OakEngineColorManager *color_manager);

signals:
	/**
   * @brief Emitted when the color processor changes
   */
	void color_processor_changed(ColorProcessorHandlePtr processor);

	/**
   * @brief Emitted when a new color manager is connected
   */
	void color_manager_changed(OakEngineColorManager *color_manager);

	void frame_swapped();

protected:
	/**
   * @brief Provides access to the color processor (nullptr if none is set)
   */
	ColorProcessorHandlePtr color_service();

	/**
   * @brief Enables a context menu that allows simple access to the DVL pipeline
   */
	void enable_default_context_menu();

	/**
   * @brief Function called whenever the processor changes
   *
   * Default functionality is just to call update()
   */
	virtual void color_processor_changed_event();

	void *renderer() const
	{
		return attached_renderer_;
	}

	void make_current();

	void done_current();

#ifdef USE_QOPENGLWINDOW
	QWindow *
#else
	QWidget *
#endif
	inner_widget() const
	{
		return inner_widget_;
	}

	/**
   * @brief Get inner widget as paint device for QPainter
   *
   * NOTE: This will be incompatible with QVulkanWindow so functions using it
   *       will need to be replaced soon.
   */
	QPaintDevice *paint_device() const;

	void set_inner_mouse_tracking(bool e);

	bool is_backend_neutral() const
	{
		return is_backend_neutral_;
	}

	QRect get_inner_rect() const
	{
		return wrapper_ ? wrapper_->rect() : QRect();
	}

	oak_video_params get_viewport_params() const;

protected slots:
	/**
   * @brief Called whenever the internal rendering context has been created
   */
	virtual void on_init();

	/**
   * @brief Called while the internal rendering context is being rendered
   */
	virtual void on_paint() = 0;

	/**
   * @brief Called just before the internal rendering context is destroyed
   */
	virtual void on_destroy();

private:
	/**
   * @brief Call this if this user has selected a different display/view/look to recreate the processor
   */
	void setup_color_processor();

	/**
   * @brief Cleanup function
   */
	void clear_ocio_lut_texture();

	/**
   * @brief Main drawing surface abstraction
   */
#ifdef USE_QOPENGLWINDOW
	QWindow *inner_widget_;
#else
	QWidget *inner_widget_;
#endif
	QWidget *wrapper_;

	/**
   * @brief Renderer abstraction
   */
	void *attached_renderer_;

	/**
   * @brief Connected color manager
   */
	OakEngineColorManager *color_manager_;

	/**
   * @brief Color management service
   */
	ColorProcessorHandlePtr color_service_;

	/**
   * @brief Internal color transform storage
   */
	oak::ColorTransform color_transform_;

	bool is_backend_neutral_ = false;

	QVector<int64_t> color_subs_;

private slots:
	/**
   * @brief Sets all color settings to the defaults pertaining to this configuration
   */
	void color_config_changed();

	/**
   * @brief The default context menu shown
   */
	void show_default_context_menu();

	/**
   * @brief If GetDisplayMenu() is called with `auto_connect` set to true, it will be connected to this
   */
	void menu_display_select(QAction *action);

	/**
   * @brief If GetViewMenu() is called with `auto_connect` set to true, it will be connected to this
   */
	void menu_view_select(QAction *action);

	/**
   * @brief If GetLookMenu() is called with `auto_connect` set to true, it will be connected to this
   */
	void menu_look_select(QAction *action);

	/**
   * @brief If GetColorSpaceMenu() is called with `auto_connect` set to true, it will be connected to this
   */
	void menu_colorspace_select(QAction *action);
};

}

#endif // MANAGEDDISPLAYOBJECT_H
