/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Modifications Copyright (C) 2025 mikesolar
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtWidgets>

#include "flowlayout.h"
FlowLayout::FlowLayout(QWidget *parent, int margin, int h_spacing, int v_spacing)
	: QLayout(parent)
	, m_hSpace_(h_spacing)
	, m_vSpace_(v_spacing)
{
	setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int h_spacing, int v_spacing)
	: m_hSpace_(h_spacing)
	, m_vSpace_(v_spacing)
{
	setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
	QLayoutItem *item;
	while ((item = takeAt(0)))
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	itemList_.append(item);
}

int FlowLayout::horizontal_spacing() const
{
	if (m_hSpace_ >= 0) {
		return m_hSpace_;
	} else {
		return smart_spacing(QStyle::PM_LayoutHorizontalSpacing);
	}
}

int FlowLayout::vertical_spacing() const
{
	if (m_vSpace_ >= 0) {
		return m_vSpace_;
	} else {
		return smart_spacing(QStyle::PM_LayoutVerticalSpacing);
	}
}

int FlowLayout::count() const
{
	return itemList_.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return itemList_.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	if (index >= 0 && index < itemList_.size())
		return itemList_.takeAt(index);
	else
		return 0;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
	return Qt::Horizontal | Qt::Vertical;
}

bool FlowLayout::hasHeightForWidth() const
{
	return true;
}

int FlowLayout::heightForWidth(int width) const
{
	int height = do_layout(QRect(0, 0, width, 0), true);
	return height;
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	do_layout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
	return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	QLayoutItem *item;
	foreach (item, itemList_)
		size = size.expandedTo(item->minimumSize());

	size += QSize(2 * contentsMargins().left(), 2 * contentsMargins().top());
	return size;
}

int FlowLayout::do_layout(const QRect &rect, bool test_only) const
{
	int left, top, right, bottom;
	getContentsMargins(&left, &top, &right, &bottom);
	QRect effective_rect = rect.adjusted(+left, +top, -right, -bottom);
	int x = effective_rect.x();
	int y = effective_rect.y();
	int line_height = 0;

	QLayoutItem *item;
	foreach (item, itemList_) {
		QWidget *wid = item->widget();
		int space_x = horizontal_spacing();
		if (space_x == -1)
			space_x = wid->style()->layoutSpacing(QSizePolicy::PushButton,
												 QSizePolicy::PushButton,
												 Qt::Horizontal);
		int space_y = vertical_spacing();
		if (space_y == -1)
			space_y = wid->style()->layoutSpacing(
				QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
		int next_x = x + item->sizeHint().width() + space_x;
		if (next_x - space_x > effective_rect.right() && line_height > 0) {
			x = effective_rect.x();
			y = y + line_height + space_y;
			next_x = x + item->sizeHint().width() + space_x;
			line_height = 0;
		}

		if (!test_only)
			item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

		x = next_x;
		line_height = qMax(line_height, item->sizeHint().height());
	}
	return y + line_height - rect.y() + bottom;
}
int FlowLayout::smart_spacing(QStyle::PixelMetric pm) const
{
	QObject *parent = this->parent();
	if (!parent) {
		return -1;
	} else if (parent->isWidgetType()) {
		QWidget *pw = static_cast<QWidget *>(parent);
		return pw->style()->pixelMetric(pm, 0, pw);
	} else {
		return static_cast<QLayout *>(parent)->spacing();
	}
}
