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

#ifndef OAKVALUEHELPER_H
#define OAKVALUEHELPER_H

#include <QVariant>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "node/keyframe.h"
#include "node/value.h"
#include "oakengine/node.h"
#include "olive/core/util/color.h"

namespace olive {

/**
 * @brief Convert a per-track component QVariant into the C ABI oak_node_value POD.
 *
 * `type` is the declared input data type (e.g. k_float/k_color).  For split-track
 * types the component is the track-0 scalar (float for k_color's red channel, etc.).
 * Returns false for types that have no POD representation.
 */
static inline bool QVariantToOakNodeValue(NodeValue::Type type, const QVariant &v,
                                          oak_node_value *out)
{
  memset(out, 0, sizeof(*out));
  switch (type) {
  case NodeValue::k_int:
  case NodeValue::k_combo:
    out->type = (type == NodeValue::k_combo) ? OAK_NODE_VALUE_COMBO
                                             : OAK_NODE_VALUE_INT;
    out->num = v.toLongLong();
    return true;
  case NodeValue::k_float:
    out->type = OAK_NODE_VALUE_FLOAT;
    out->f[0] = v.toDouble();
    return true;
  case NodeValue::k_boolean:
    out->type = OAK_NODE_VALUE_BOOL;
    out->num = v.toBool() ? 1 : 0;
    return true;
  case NodeValue::k_rational:
    out->type = OAK_NODE_VALUE_RATIONAL;
    {
      const Rational r = v.value<Rational>();
      out->num = r.numerator();
      out->den = r.denominator();
    }
    return true;
  case NodeValue::k_color:
    out->type = OAK_NODE_VALUE_COLOR;
    {
      const core::Color c = v.value<core::Color>();
      out->f[0] = c.red();
      out->f[1] = c.green();
      out->f[2] = c.blue();
      out->f[3] = c.alpha();
    }
    return true;
  case NodeValue::k_vec2:
    out->type = OAK_NODE_VALUE_VEC2;
    {
      const QVector2D vec = v.value<QVector2D>();
      out->f[0] = vec.x();
      out->f[1] = vec.y();
    }
    return true;
  case NodeValue::k_vec3:
    out->type = OAK_NODE_VALUE_VEC3;
    {
      const QVector3D vec = v.value<QVector3D>();
      out->f[0] = vec.x();
      out->f[1] = vec.y();
      out->f[2] = vec.z();
    }
    return true;
  case NodeValue::k_vec4:
    out->type = OAK_NODE_VALUE_VEC4;
    {
      const QVector4D vec = v.value<QVector4D>();
      out->f[0] = vec.x();
      out->f[1] = vec.y();
      out->f[2] = vec.z();
      out->f[3] = vec.w();
    }
    return true;
  default:
    return false;
  }
}

/**
 * @brief Convert a per-track component QVariant into the C ABI oak_node_value POD.
 *
 * Unlike QVariantToOakNodeValue() which takes a full normal value, this takes a
 * single track's component (e.g. one float for a k_color channel). The resulting
 * POD has the input's declared type with the component in f[0]/num, exactly what
 * the per-track facade commands expect.
 */
static inline bool NodeTrackComponentToOakNodeValue(NodeValue::Type type,
                                                    const QVariant &v,
                                                    oak_node_value *out)
{
  memset(out, 0, sizeof(*out));
  switch (type) {
  case NodeValue::k_int:
  case NodeValue::k_combo:
    out->type = (type == NodeValue::k_combo) ? OAK_NODE_VALUE_COMBO
                                             : OAK_NODE_VALUE_INT;
    out->num = v.toLongLong();
    return true;
  case NodeValue::k_float:
  case NodeValue::k_bezier:
    out->type = OAK_NODE_VALUE_FLOAT;
    out->f[0] = v.toDouble();
    return true;
  case NodeValue::k_boolean:
    out->type = OAK_NODE_VALUE_BOOL;
    out->num = v.toBool() ? 1 : 0;
    return true;
  case NodeValue::k_rational:
    out->type = OAK_NODE_VALUE_RATIONAL;
    {
      const Rational r = v.value<Rational>();
      out->num = r.numerator();
      out->den = r.denominator();
    }
    return true;
  case NodeValue::k_color:
    out->type = OAK_NODE_VALUE_COLOR;
    out->f[0] = v.toFloat();
    return true;
  case NodeValue::k_vec2:
    out->type = OAK_NODE_VALUE_VEC2;
    out->f[0] = v.toFloat();
    return true;
  case NodeValue::k_vec3:
    out->type = OAK_NODE_VALUE_VEC3;
    out->f[0] = v.toFloat();
    return true;
  case NodeValue::k_vec4:
    out->type = OAK_NODE_VALUE_VEC4;
    out->f[0] = v.toFloat();
    return true;
  default:
    return false;
  }
}

/**
 * @brief Convert a full C ABI oak_node_value POD back into a QVariant.
 *
 * Mirrors QVariantToOakNodeValue(). String/binary/bezier are not represented
 * in the POD and return an invalid QVariant; use the dedicated string/binary/
 * bezier facade getters for those.
 */
static inline QVariant OakNodeValueToQVariant(const oak_node_value &v)
{
  switch (v.type) {
  case OAK_NODE_VALUE_INT:
    return QVariant::fromValue<qlonglong>(v.num);
  case OAK_NODE_VALUE_FLOAT:
    return QVariant::fromValue(v.f[0]);
  case OAK_NODE_VALUE_BOOL:
    return QVariant::fromValue(v.num != 0);
  case OAK_NODE_VALUE_RATIONAL:
    return QVariant::fromValue(
      Rational(int(v.num), int(v.den)));
  case OAK_NODE_VALUE_COLOR:
    return QVariant::fromValue(core::Color(
      float(v.f[0]), float(v.f[1]), float(v.f[2]), float(v.f[3])));
  case OAK_NODE_VALUE_VEC2:
    return QVariant::fromValue(
      QVector2D(float(v.f[0]), float(v.f[1])));
  case OAK_NODE_VALUE_VEC3:
    return QVariant::fromValue(
      QVector3D(float(v.f[0]), float(v.f[1]), float(v.f[2])));
  case OAK_NODE_VALUE_VEC4:
    return QVariant::fromValue(
      QVector4D(float(v.f[0]), float(v.f[1]), float(v.f[2]), float(v.f[3])));
  case OAK_NODE_VALUE_COMBO:
    return QVariant::fromValue<int>(int(v.num));
  default:
    return QVariant();
  }
}

/**
 * @brief Map an engine NodeKeyframe::Type to the facade easing type.
 */
static inline int NodeKeyframeTypeToFacade(NodeKeyframe::Type type)
{
  switch (type) {
  case NodeKeyframe::k_bezier:
    return 1;
  case NodeKeyframe::k_hold:
    return 2;
  case NodeKeyframe::k_linear:
  default:
    return 0;
  }
}

}  // namespace olive

#endif  // OAKVALUEHELPER_H
