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

#include "nodeparamviewarraywidget.h"

#include <QEvent>
#include <QHBoxLayout>

#include "core.h"

namespace olive
{

NodeParamViewArrayWidget::NodeParamViewArrayWidget(oak::Node node,
												   const QString &input,
												   QWidget *parent)
	: QWidget(parent)
	, node_(node)
	, input_(input)
	, bridge_(new EngineEventBridge(this))
{
	QHBoxLayout *layout = new QHBoxLayout(this);

	count_lbl_ = new QLabel();
	layout->addWidget(count_lbl_);

	bridge_->subscribe(reinterpret_cast<void *>(node_.handle()),
					   OAKENGINE_EVENT_NODE_INPUT_ARRAY_SIZE_CHANGED);
	connect(bridge_, &EngineEventBridge::node_input_array_size_changed, this,
			[this](OakEngineNode *, const QString &input_id, int old_size,
				   int new_size) {
				update_counter(input_id, old_size, new_size);
			});

	// Issue 12: reuse the issue 7 undo signal so array size changes replayed
	// from the undo stack update the counter.
	connect(Core::instance(), &Core::undo_index_changed, this, [this](int) {
		update_counter(input_, 0, oak::Input(node_.handle(), input_).array_size());
	});

	update_counter(input_, 0, oak::Input(node_.handle(), input_).array_size());
}

void NodeParamViewArrayWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);

	emit double_clicked();
}

void NodeParamViewArrayWidget::update_counter(const QString &changed_input, int old_size,
											 int new_size)
{
	Q_UNUSED(old_size)
	if (changed_input == input_) {
		count_lbl_->setText(tr("%n element(s)", nullptr, new_size));
	}
}

NodeParamViewArrayButton::NodeParamViewArrayButton(
	NodeParamViewArrayButton::Type type, QWidget *parent)
	: QPushButton(parent)
	, type_(type)
{
	retranslate();

	int sz = sizeHint().height() / 3 * 2;
	setFixedSize(sz, sz);
}

void NodeParamViewArrayButton::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange) {
		retranslate();
	}

	QPushButton::changeEvent(event);
}

void NodeParamViewArrayButton::retranslate()
{
	if (type_ == k_add) {
		setText(tr("+"));
	} else {
		setText(tr("-"));
	}
}

}
