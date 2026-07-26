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

#ifndef OAK_SCOPE_PANEL_H
#define OAK_SCOPE_PANEL_H

#include <QComboBox>
#include <QStackedWidget>

#include "panel/panel.h"
#include "panel/viewer/viewerbase.h"
#include "widget/scope/histogram/histogram.h"
#include "widget/scope/vectorscope/vectorscope.h"
#include "widget/scope/waveform/waveform.h"

namespace olive
{

class ScopePanel : public PanelWidget {
	Q_OBJECT
public:
	enum Type {
		k_type_waveform,
		k_type_vectorscope,
		k_type_histogram,

		k_type_count
	};

	ScopePanel();

	void set_type(Type t);

	static QString type_to_name(Type t);

	void set_viewer_panel(ViewerPanelBase *vp);

	ViewerPanelBase *get_connected_viewer_panel() const
	{
		return viewer_;
	}

public slots:
	void set_reference_buffer(TexturePtr frame);

	void set_color_manager(OakEngineColorManager *manager);

protected:
	virtual void retranslate() override;

private:
	Type type_;

	QStackedWidget *stack_;

	QComboBox *scope_type_combobox_;

	WaveformScope *waveform_view_;

	VectorscopeScope *vectorscope_;

	HistogramScope *histogram_;

	ViewerPanelBase *viewer_;
};

}

#endif // OAK_SCOPE_PANEL_H
