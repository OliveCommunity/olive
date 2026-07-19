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

#ifndef OAK_VIEWERTEXTEDITOR_H
#define OAK_VIEWERTEXTEDITOR_H

#include <QApplication>
#include <QDebug>
#include <QFontComboBox>
#include <QTextEdit>
#include <QPushButton>

#include "widget/slider/floatslider.h"
#include "widget/slider/integerslider.h"

namespace olive
{

class ViewerTextEditorToolBar : public QWidget {
	Q_OBJECT
public:
	ViewerTextEditorToolBar(QWidget *parent = nullptr);

	QString get_font_family() const
	{
		return font_combo_->currentText();
	}

	QString get_font_style_name() const
	{
		return style_combo_->currentText();
	}

public slots:
	void set_font_family(QString s)
	{
		font_combo_->blockSignals(true);
		font_combo_->setCurrentFont(s);
		update_font_style_list(s);
		font_combo_->blockSignals(false);
	}

	void set_style(QString style)
	{
		style_combo_->blockSignals(true);
		style_combo_->setCurrentText(style);
		style_combo_->blockSignals(false);
	}

	void set_font_size(double d)
	{
		font_sz_slider_->set_value(d);
	}
	void set_underline(bool e)
	{
		underline_btn_->setChecked(e);
	}
	void set_strikethrough(bool e)
	{
		strikethrough_btn_->setChecked(e);
	}
	void set_alignment(Qt::Alignment a);
	void set_vertical_alignment(Qt::Alignment a);
	void set_color(const QColor &c);
	void set_small_caps(bool e)
	{
		small_caps_btn_->setChecked(e);
	}
	void set_stretch(int i)
	{
		stretch_slider_->set_value(i);
	}
	void set_kerning(qreal i)
	{
		kerning_slider_->set_value(i);
	}
	void set_line_height(qreal i)
	{
		line_height_slider_->set_value(i);
	}

signals:
	void family_changed(const QString &s);
	void size_changed(double d);
	void style_changed(const QString &s);
	void underline_changed(bool e);
	void strikethrough_changed(bool e);
	void alignment_changed(Qt::Alignment alignment);
	void vertical_alignment_changed(Qt::Alignment alignment);
	void color_changed(const QColor &c);
	void small_caps_changed(bool e);
	void stretch_changed(int i);
	void kerning_changed(qreal i);
	void line_height_changed(qreal i);

	void first_paint();

protected:
	virtual void mousePressEvent(QMouseEvent *event) override;

	virtual void mouseMoveEvent(QMouseEvent *event) override;

	virtual void mouseReleaseEvent(QMouseEvent *event) override;

	virtual void closeEvent(QCloseEvent *event) override;

	virtual void paintEvent(QPaintEvent *event) override;

private:
	void add_spacer(QLayout *l);

	QPoint drag_anchor_;

	QFontComboBox *font_combo_;

	FloatSlider *font_sz_slider_;

	QComboBox *style_combo_;

	QPushButton *underline_btn_;
	QPushButton *strikethrough_btn_;

	QPushButton *align_left_btn_;
	QPushButton *align_center_btn_;
	QPushButton *align_right_btn_;
	QPushButton *align_justify_btn_;

	QPushButton *align_top_btn_;
	QPushButton *align_middle_btn_;
	QPushButton *align_bottom_btn_;

	IntegerSlider *stretch_slider_;
	FloatSlider *kerning_slider_;
	FloatSlider *line_height_slider_;
	QPushButton *small_caps_btn_;

	QPushButton *color_btn_;

	bool painted_;

	bool drag_enabled_;

private slots:
	void update_font_style_list(const QString &family);

	void update_font_style_list_and_emit_family_changed(const QString &family);
};

class ViewerTextEditor : public QTextEdit {
	Q_OBJECT
public:
	ViewerTextEditor(double scale, QWidget *parent = nullptr);

	void connect_tool_bar(ViewerTextEditorToolBar *toolbar);

	void paint(QPainter *p, Qt::Alignment valign);

	virtual void dragEnterEvent(QDragEnterEvent *e) override
	{
		return QTextEdit::dragEnterEvent(e);
	}
	virtual void dragMoveEvent(QDragMoveEvent *e) override
	{
		return QTextEdit::dragMoveEvent(e);
	}
	virtual void dragLeaveEvent(QDragLeaveEvent *e) override
	{
		return QTextEdit::dragLeaveEvent(e);
	}
	virtual void dropEvent(QDropEvent *e) override
	{
		return QTextEdit::dropEvent(e);
	}

protected:
	virtual void paintEvent(QPaintEvent *event) override;

private:
	static void update_tool_bar(ViewerTextEditorToolBar *toolbar,
							  const QTextCharFormat &f,
							  const QTextBlockFormat &b,
							  Qt::Alignment alignment);

	void merge_char_format(const QTextCharFormat &fmt);

	void apply_style(QTextCharFormat *format, const QString &family,
					const QString &style);

	QVector<ViewerTextEditorToolBar *> toolbars_;

	QImage dpi_force_;

	QTextDocument *transparent_clone_;

	bool block_update_toolbar_signal_;

	bool forced_default_;
	QTextCharFormat default_fmt_;

private slots:
	void format_changed(const QTextCharFormat &f);

	void set_family(const QString &s);

	void set_style(const QString &s);

	void set_font_strikethrough(bool e);

	void set_small_caps(bool e);

	void set_font_stretch(int i);

	void set_font_kerning(qreal i);

	void set_line_height(qreal i);

	void lock_scroll_bar_maximum_to_zero();

	void document_changed();
};

}

#endif // OAK_VIEWERTEXTEDITOR_H
