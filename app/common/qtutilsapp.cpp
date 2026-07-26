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

#include "common/qtutils.h"

namespace olive
{

int QtUtils::q_font_metrics_width(QFontMetrics fm, const QString &s)
{
    return fm.horizontalAdvance(s);
}

QFrame *QtUtils::create_horizontal_line()
{
    QFrame *horizontal_line = new QFrame();
    horizontal_line->setFrameShape(QFrame::HLine);
    horizontal_line->setFrameShadow(QFrame::Sunken);
    return horizontal_line;
}

QFrame *QtUtils::create_vertical_line()
{
    QFrame *l = create_horizontal_line();
    l->setFrameShape(QFrame::VLine);
    return l;
}

QString QtUtils::get_formatted_date_time(const QDateTime &dt)
{
    return dt.toString(Qt::TextDate);
}

QStringList QtUtils::word_wrap_string(const QString &s, const QFontMetrics &fm,
                                      int bounding_width)
{
    QStringList list;
    QStringList lines = s.split('\n');
    for (int i = 0; i < lines.size(); i++) {
        QString this_line = lines.at(i);
        while (this_line.size() > 1 &&
               q_font_metrics_width(fm, this_line) >= bounding_width) {
            int old_size = this_line.size();
            int hard_break = -1;
            for (int j = this_line.size() - 1; j >= 0; j--) {
                const QChar &char_test = this_line.at(j);
                if (char_test.isSpace() || char_test == '-') {
                    if (q_font_metrics_width(fm, this_line.left(j)) <
                        bounding_width) {
                        if (!char_test.isSpace()) j++;
                        list.append(this_line.left(j));
                        while (j < this_line.size() &&
                               this_line.at(j).isSpace()) j++;
                        this_line.remove(0, j);
                        break;
                    }
                } else if (hard_break == -1 &&
                           q_font_metrics_width(fm, this_line.left(j)) <
                               bounding_width) {
                    hard_break = j;
                }
            }
            if (old_size == this_line.size()) {
                if (hard_break != -1) {
                    list.append(this_line.left(hard_break));
                    this_line.remove(0, hard_break);
                } else {
                    break;
                }
            }
        }
        if (!this_line.isEmpty()) {
            list.append(this_line);
        }
    }
    return list;
}

Qt::KeyboardModifiers
QtUtils::flip_control_and_shift_modifiers(Qt::KeyboardModifiers e)
{
    if (e & Qt::ControlModifier & Qt::ShiftModifier) return e;
    if (e & Qt::ShiftModifier) {
        e |= Qt::ControlModifier;
        e &= ~Qt::ShiftModifier;
    } else if (e & Qt::ControlModifier) {
        e |= Qt::ShiftModifier;
        e &= ~Qt::ControlModifier;
    }
    return e;
}

void QtUtils::set_combo_box_data(QComboBox *cb, int data)
{
    for (int i = 0; i < cb->count(); i++) {
        if (cb->itemData(i).toInt() == data) {
            cb->setCurrentIndex(i);
            break;
        }
    }
}

void QtUtils::set_combo_box_data(QComboBox *cb, const QString &data)
{
    for (int i = 0; i < cb->count(); i++) {
        if (cb->itemData(i).toString() == data) {
            cb->setCurrentIndex(i);
            break;
        }
    }
}

QColor QtUtils::to_q_color(const core::Color &i)
{
    QColor c;

    // QColor only supports values from 0.0 to 1.0 and are only used for UI representations
    c.setRedF(std::clamp(i.red(), 0.0f, 1.0f));
    c.setGreenF(std::clamp(i.green(), 0.0f, 1.0f));
    c.setBlueF(std::clamp(i.blue(), 0.0f, 1.0f));
    c.setAlphaF(std::clamp(i.alpha(), 0.0f, 1.0f));

    return c;
}

}
