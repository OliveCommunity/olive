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

#include "oakengine/gizmo.h"

#include <QPainter>
#include <QString>

#include "node/gizmo/draggable.h"
#include "node/gizmo/path.h"
#include "node/gizmo/point.h"
#include "node/gizmo/polygon.h"
#include "node/gizmo/screen.h"
#include "node/gizmo/text.h"
#include "node/globals.h"
#include "node/generator/text/textv3.h"
#include "node/node.h"
#include "render/loopmode.h"
#include "render/videoparams.h"

extern "C" {

int oakengine_text_gizmo_get(OakEngineNode *node,
    int64_t time_num, int64_t time_den, oakengine_text_gizmo *out)
{
    if (!node || !out) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    QRectF r = gizmo->get_rect();
    out->rect_x = r.x();
    out->rect_y = r.y();
    out->rect_w = r.width();
    out->rect_h = r.height();

    // Map Qt::Alignment to our simple enum
    Qt::Alignment va = gizmo->get_vertical_alignment();
    if (va & Qt::AlignBottom) {
        out->vertical_alignment = 1;
    } else if (va & Qt::AlignVCenter) {
        out->vertical_alignment = 2;
    } else {
        out->vertical_alignment = 0; // AlignTop
    }

    return OAKENGINE_OK;
}

int oakengine_text_gizmo_get_html(OakEngineNode *node,
    int64_t time_num, int64_t time_den, char *buf, int buf_size)
{
    if (!node) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    QByteArray html = gizmo->get_html().toUtf8();
    int needed = html.size() + 1; // include NUL

    if (buf && buf_size > 0) {
        int copy = qMin(needed, buf_size);
        memcpy(buf, html.constData(), copy - 1);
        buf[copy - 1] = '\0';
    }

    return needed;
}

int oakengine_text_gizmo_update_html(OakEngineNode *node,
    const char *html, int64_t time_num, int64_t time_den)
{
    if (!node || !html) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    gizmo->update_input_html(QString::fromUtf8(html),
        olive::core::Rational(time_num, time_den));
    return OAKENGINE_OK;
}

int oakengine_text_gizmo_set_vertical_alignment(
    OakEngineNode *node, int alignment)
{
    if (!node) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    Qt::Alignment va;
    switch (alignment) {
    case 1:
        va = Qt::AlignBottom;
        break;
    case 2:
        va = Qt::AlignVCenter;
        break;
    default:
        va = Qt::AlignTop;
        break;
    }

    gizmo->set_vertical_alignment(va);
    return OAKENGINE_OK;
}

int oakengine_text_gizmo_activated(OakEngineNode *node)
{
    if (!node) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    emit gizmo->activated();
    return OAKENGINE_OK;
}

int oakengine_text_gizmo_deactivated(OakEngineNode *node)
{
    if (!node) {
        return OAKENGINE_E_INVALID;
    }

    auto *textv3 = dynamic_cast<olive::TextGeneratorV3 *>(
        reinterpret_cast<olive::Node *>(node));
    if (!textv3) {
        return OAKENGINE_E_INVALID;
    }

    olive::TextGizmo *gizmo = textv3->text_gizmo();
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    emit gizmo->deactivated();
    return OAKENGINE_OK;
}

int oakengine_gizmo_get_drag_value_behavior(void *gizmo)
{
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    auto *dg = dynamic_cast<olive::DraggableGizmo *>(
        static_cast<olive::NodeGizmo *>(gizmo));
    if (!dg) {
        return OAKENGINE_E_INVALID;
    }
    return static_cast<int>(dg->get_drag_value_behavior());
}

int oakengine_gizmo_drag_start(void *gizmo,
    void *row, double abs_x, double abs_y, int64_t time_num,
    int64_t time_den)
{
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    auto *dg = dynamic_cast<olive::DraggableGizmo *>(
        static_cast<olive::NodeGizmo *>(gizmo));
    if (!dg) {
        return OAKENGINE_E_INVALID;
    }

    olive::NodeValueRow empty_row;
    olive::NodeValueRow &row_ref = row
        ? *static_cast<olive::NodeValueRow *>(row)
        : empty_row;

    dg->drag_start(row_ref, abs_x, abs_y,
        olive::core::Rational(time_num, time_den));
    return OAKENGINE_OK;
}

int oakengine_gizmo_drag_move(void *gizmo,
    double x, double y, int qt_keyboard_modifiers)
{
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    auto *dg = dynamic_cast<olive::DraggableGizmo *>(
        static_cast<olive::NodeGizmo *>(gizmo));
    if (!dg) {
        return OAKENGINE_E_INVALID;
    }
    dg->drag_move(x, y, Qt::KeyboardModifiers(qt_keyboard_modifiers));
    return OAKENGINE_OK;
}

int oakengine_gizmo_drag_end(void *gizmo, void *command)
{
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }

    auto *dg = dynamic_cast<olive::DraggableGizmo *>(
        static_cast<olive::NodeGizmo *>(gizmo));
    if (!dg) {
        return OAKENGINE_E_INVALID;
    }
    dg->drag_end(static_cast<olive::MultiUndoCommand *>(command));
    return OAKENGINE_OK;
}

int oakengine_gizmo_is_visible(void *gizmo)
{
    if (!gizmo) {
        return 0;
    }
    return static_cast<olive::NodeGizmo *>(gizmo)->is_visible() ? 1 : 0;
}

int oakengine_gizmo_draw(void *gizmo, void *painter)
{
    if (!gizmo || !painter) {
        return OAKENGINE_E_INVALID;
    }
    static_cast<olive::NodeGizmo *>(gizmo)->draw(
        static_cast<QPainter *>(painter));
    return OAKENGINE_OK;
}

int oakengine_gizmo_set_globals(void *gizmo,
    int video_width, int video_height,
    int64_t time_num, int64_t time_den)
{
    if (!gizmo) {
        return OAKENGINE_E_INVALID;
    }
    olive::VideoParams vp;
    if (video_width > 0 && video_height > 0) {
        vp.set_width(video_width);
        vp.set_height(video_height);
    }
    olive::NodeGlobals globals(
        vp, olive::AudioParams(),
        olive::Rational(time_num, time_den),
        olive::LoopMode::k_loop_mode_off);
    static_cast<olive::NodeGizmo *>(gizmo)->set_globals(globals);
    return OAKENGINE_OK;
}

int oakengine_gizmo_hit_test(void *gizmo,
    const double *transform6, double px, double py)
{
    if (!gizmo) {
        return 0;
    }
    auto *g = static_cast<olive::NodeGizmo *>(gizmo);
    if (!g->is_visible()) {
        return 0;
    }

    const QPointF p(px, py);

    if (auto *point = dynamic_cast<olive::PointGizmo *>(g)) {
        if (!transform6) {
            return 0;
        }
        const QTransform t(transform6[0], transform6[1], transform6[2],
                           transform6[3], transform6[4], transform6[5]);
        return point->get_clicking_rect(t).contains(p) ? 1 : 0;
    }
    if (auto *poly = dynamic_cast<olive::PolygonGizmo *>(g)) {
        return poly->get_polygon().containsPoint(p, Qt::OddEvenFill) ? 1 : 0;
    }
    if (auto *path = dynamic_cast<olive::PathGizmo *>(g)) {
        return path->get_path().contains(p) ? 1 : 0;
    }
    if (dynamic_cast<olive::ScreenGizmo *>(g)) {
        // Screen gizmos are hittable anywhere (mirrors the viewer logic).
        return 1;
    }
    return 0;
}

} // extern "C"
