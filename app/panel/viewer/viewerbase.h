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

#ifndef OAK_VIEWERPANELBASE_H
#define OAK_VIEWERPANELBASE_H

#include <memory>

#include "panel/pixelsampler/pixelsamplerpanel.h"
#include "panel/timebased/timebased.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "widget/viewer/viewer.h"

namespace olive
{

class ViewerPanelBase : public TimeBasedPanel {
	Q_OBJECT
public:
	ViewerPanelBase(const QString &object_name);

	ViewerWidget *get_viewer_widget() const
	{
		return static_cast<ViewerWidget *>(get_time_based_widget());
	}

	virtual void play_pause() override;

	virtual void play_in_to_out() override;

	virtual void shuttle_left() override;

	virtual void shuttle_stop() override;

	virtual void shuttle_right() override;

	void connect_time_based_panel(TimeBasedPanel *panel);

	void disconnect_time_based_panel(TimeBasedPanel *panel);

	/**
   * @brief Wrapper for ViewerWidget::SetFullScreen()
   */
	void set_full_screen(QScreen *screen = nullptr);

	OakEngineColorManager *get_color_manager()
	{
		return get_viewer_widget()->color_manager();
	}

	void update_texture_from_node()
	{
		get_viewer_widget()->update_texture_from_node();
	}

	void add_playback_device(ViewerDisplayWidget *vw)
	{
		get_viewer_widget()->add_playback_device(vw);
	}

	void set_timeline_selected_blocks(const QVector<Block *> &b)
	{
		get_viewer_widget()->set_timeline_selected_blocks(b);
	}

	void set_node_view_selections(const QVector<OakEngineNode *> &n)
	{
		get_viewer_widget()->set_node_view_selections(n);
	}

	void connect_multicam_widget(MulticamWidget *p)
	{
		get_viewer_widget()->connect_multicam_widget(p);
	}

public slots:
	void set_gizmos(OakEngineNode *node);

	void cache_entire_sequence();

	void cache_sequence_in_out();

	void request_start_editing_text()
	{
		get_viewer_widget()->request_start_editing_text();
	}

signals:
	/**
   * @brief Signal emitted when a new frame is loaded
   */
	void texture_changed(TexturePtr t);

	/**
   * @brief Wrapper for ViewerGLWidget::ColorProcessorChanged()
   */
	void color_processor_changed(ColorProcessorHandlePtr processor);

	/**
   * @brief Wrapper for ViewerGLWidget::ColorManagerChanged()
   */
	void color_manager_changed(OakEngineColorManager *color_manager);

protected:
	void set_viewer_widget(ViewerWidget *vw);

private slots:
	void focused_panel_changed(PanelWidget *panel);
};

}

#endif // OAK_VIEWERPANELBASE_H
