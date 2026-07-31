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

#ifndef OAK_NODECOMBOBOX_H
#define OAK_NODECOMBOBOX_H

#include <QComboBox>

namespace olive
{

class NodeComboBox : public QComboBox {
	Q_OBJECT
public:
	NodeComboBox(QWidget *parent = nullptr);

	virtual void showPopup() override;

	const QString &get_selected_node() const;

public slots:
	void set_node(const QString &id);

protected:
	virtual void changeEvent(QEvent *e) override;

signals:
	void node_changed(const QString &id);

private:
	void update_text();

	void set_node_internal(const QString &id, bool emit_signal);

	QString selected_id_;
};

}

#endif // FOOTAGECOMBOBOX_H
