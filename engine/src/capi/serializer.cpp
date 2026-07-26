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

#include "oakengine/serializer.h"

#include <cstring>

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "node/keyframe.h"
#include "node/node.h"
#include "node/project.h"
#include "node/project/serializer/serializer.h"
#include "timeline/timelinemarker.h"

namespace
{

int write_string(const QString &s, char *buf, int buf_size)
{
    const QByteArray utf8 = s.toUtf8();
    const int len = int(utf8.size());
    if (buf && buf_size > 0) {
        const int n = qMin(len, buf_size - 1);
        std::memcpy(buf, utf8.constData(), size_t(n));
        buf[n] = '\0';
    }
    return len;
}

olive::ProjectSerializer::LoadType c_load_type_to_cpp(int load_type)
{
    switch (load_type) {
        case OAKENGINE_CLIPBOARD_PROJECT:
            return olive::ProjectSerializer::k_project;
        case OAKENGINE_CLIPBOARD_NODES:
            return olive::ProjectSerializer::k_only_nodes;
        case OAKENGINE_CLIPBOARD_CLIPS:
            return olive::ProjectSerializer::k_only_clips;
        case OAKENGINE_CLIPBOARD_MARKERS:
            return olive::ProjectSerializer::k_only_markers;
        case OAKENGINE_CLIPBOARD_KEYFRAMES:
            return olive::ProjectSerializer::k_only_keyframes;
        default:
            return olive::ProjectSerializer::k_only_nodes;
    }
}

int cpp_result_code_to_c(olive::ProjectSerializer::ResultCode code)
{
    switch (code) {
        case olive::ProjectSerializer::k_success:
            return OAKENGINE_SERIALIZER_OK;
        case olive::ProjectSerializer::k_project_too_old:
            return OAKENGINE_SERIALIZER_TOO_OLD;
        case olive::ProjectSerializer::k_project_too_new:
            return OAKENGINE_SERIALIZER_TOO_NEW;
        case olive::ProjectSerializer::k_unknown_version:
            return OAKENGINE_SERIALIZER_UNKNOWN_VERSION;
        case olive::ProjectSerializer::k_file_error:
            return OAKENGINE_SERIALIZER_FILE_ERROR;
        case olive::ProjectSerializer::k_xml_error:
            return OAKENGINE_SERIALIZER_XML_ERROR;
        case olive::ProjectSerializer::k_overwrite_error:
            return OAKENGINE_SERIALIZER_OVERWRITE_ERROR;
        case olive::ProjectSerializer::k_no_data:
            return OAKENGINE_SERIALIZER_NO_DATA;
        default:
            return OAKENGINE_SERIALIZER_NO_DATA;
    }
}

struct ClipboardCtx {
    olive::ProjectSerializer::LoadType load_type;
    olive::Project *project;
    QString filename;
    olive::ProjectSerializer::SaveData save_data;
    olive::ProjectSerializer::LoadData load_data;
    QString xml_output;

    ClipboardCtx(int lt, olive::Project *p, const QString &fn)
        : load_type(c_load_type_to_cpp(lt))
        , project(p)
        , filename(fn)
        , save_data(load_type, project, filename)
    {
    }
};

ClipboardCtx *ctx(OakEngineClipboard *cb)
{
    return reinterpret_cast<ClipboardCtx *>(cb);
}

} // namespace

extern "C" int oakengine_serializer_check_compressed(const char *filename)
{
    if (!filename || std::strlen(filename) == 0) {
        return 0;
    }
    QFile file(QString::fromUtf8(filename));
    if (!file.open(QFile::ReadOnly)) {
        return 0;
    }
    return olive::ProjectSerializer::check_compressed_id(&file) ? 1 : 0;
}

extern "C" OakEngineClipboard *oakengine_clipboard_create(
    int load_type, OakEngineProject *project, const char *filename)
{
    return reinterpret_cast<OakEngineClipboard *>(new ClipboardCtx(
        load_type, reinterpret_cast<olive::Project *>(project),
        filename ? QString::fromUtf8(filename) : QString()));
}

extern "C" int oakengine_clipboard_set_nodes(OakEngineClipboard *cb,
                                             const OakEngineNode *const *nodes,
                                             int count)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    QVector<olive::Node *> list;
    if (nodes && count > 0) {
        list.reserve(count);
        for (int i = 0; i < count; i++) {
            list.append(reinterpret_cast<olive::Node *>(
                const_cast<OakEngineNode *>(nodes[i])));
        }
    }
    c->save_data.set_only_serialize_nodes_and_resolve_groups(list);
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_set_markers(
    OakEngineClipboard *cb, const OakEngineMarker *const *markers, int count)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    std::vector<olive::TimelineMarker *> list;
    if (markers && count > 0) {
        list.reserve(size_t(count));
        for (int i = 0; i < count; i++) {
            list.push_back(reinterpret_cast<olive::TimelineMarker *>(
                const_cast<OakEngineMarker *>(markers[i])));
        }
    }
    c->save_data.set_only_serialize_markers(list);
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_set_keyframes(
    OakEngineClipboard *cb, const OakEngineKeyframe *const *keyframes,
    int count)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    std::vector<olive::NodeKeyframe *> list;
    if (keyframes && count > 0) {
        list.reserve(size_t(count));
        for (int i = 0; i < count; i++) {
            list.push_back(reinterpret_cast<olive::NodeKeyframe *>(
                const_cast<OakEngineKeyframe *>(keyframes[i])));
        }
    }
    c->save_data.set_only_serialize_keyframes(list);
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_set_property(OakEngineClipboard *cb,
                                                  OakEngineNode *node,
                                                  const char *key,
                                                  const char *value)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || !node || !key) {
        return OAKENGINE_E_INVALID;
    }
    olive::ProjectSerializer::SerializedProperties props =
        c->save_data.get_properties();
    props[reinterpret_cast<olive::Node *>(node)][QString::fromUtf8(key)] =
        value ? QString::fromUtf8(value) : QString();
    c->save_data.set_properties(props);
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_copy(OakEngineClipboard *cb)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    olive::ProjectSerializer::Result res =
        olive::ProjectSerializer::copy(c->save_data);
    return (res == olive::ProjectSerializer::k_success) ? OAKENGINE_OK
                                                        : OAKENGINE_E_FAILED;
}

extern "C" int oakengine_clipboard_save_to_xml(OakEngineClipboard *cb,
                                               char *buf, int buf_size)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    c->xml_output.clear();
    QXmlStreamWriter writer(&c->xml_output);
    olive::ProjectSerializer::Result res =
        olive::ProjectSerializer::save(&writer, c->save_data);
    if (res != olive::ProjectSerializer::k_success) {
        return OAKENGINE_E_FAILED;
    }
    return write_string(c->xml_output, buf, buf_size);
}

namespace
{

int do_paste(OakEngineClipboard *cb, int load_type,
             olive::Project *project,
             int (*map_fn)(OakEngineNode *, OakEngineNode *, void *),
             void *userdata, int *result_code, char *details_buf,
             int details_buf_size)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || !result_code) {
        return OAKENGINE_E_INVALID;
    }

    olive::ProjectSerializer::Result res = olive::ProjectSerializer::paste(
        c_load_type_to_cpp(load_type), project);

    *result_code = cpp_result_code_to_c(res.code());

    if (res == olive::ProjectSerializer::k_success) {
        c->load_data = res.get_load_data();

        if (map_fn && !c->load_data.node_ptrs.isEmpty()) {
            for (auto it = c->load_data.node_ptrs.cbegin();
                 it != c->load_data.node_ptrs.cend(); ++it) {
                const int stop = map_fn(
                    reinterpret_cast<OakEngineNode *>(it.key()),
                    reinterpret_cast<OakEngineNode *>(it.value()), userdata);
                if (stop != 0) {
                    break;
                }
            }
        }

        return OAKENGINE_OK;
    }

    if (details_buf && details_buf_size > 0) {
        write_string(res.get_details(), details_buf, details_buf_size);
    }
    return OAKENGINE_E_FAILED;
}

} // namespace

extern "C" int oakengine_clipboard_paste(OakEngineClipboard *cb,
                                         int load_type,
                                         OakEngineProject *project,
                                         int *result_code,
                                         char *details_buf,
                                         int details_buf_size)
{
    return do_paste(cb, load_type, reinterpret_cast<olive::Project *>(project),
                    nullptr, nullptr, result_code, details_buf,
                    details_buf_size);
}

extern "C" int oakengine_clipboard_paste_with_map(
    OakEngineClipboard *cb, int load_type, OakEngineProject *project,
    int (*map_fn)(OakEngineNode *old, OakEngineNode *new_node, void *userdata),
    void *userdata, int *result_code, char *details_buf,
    int details_buf_size)
{
    return do_paste(cb, load_type, reinterpret_cast<olive::Project *>(project),
                    map_fn, userdata, result_code, details_buf,
                    details_buf_size);
}

extern "C" void oakengine_clipboard_free(OakEngineClipboard *cb)
{
    delete ctx(cb);
}

extern "C" int oakengine_clipboard_get_loaded_node_count(
    OakEngineClipboard *cb)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    return c->load_data.nodes.size();
}

extern "C" OakEngineNode *oakengine_clipboard_get_loaded_node_at(
    OakEngineClipboard *cb, int index)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || index < 0 || index >= c->load_data.nodes.size()) {
        return nullptr;
    }
    return reinterpret_cast<OakEngineNode *>(c->load_data.nodes.at(index));
}

extern "C" int oakengine_clipboard_get_loaded_marker_count(
    OakEngineClipboard *cb)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    return static_cast<int>(c->load_data.markers.size());
}

extern "C" OakEngineMarker *oakengine_clipboard_get_loaded_marker_at(
    OakEngineClipboard *cb, int index)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || index < 0 ||
        index >= static_cast<int>(c->load_data.markers.size())) {
        return nullptr;
    }
    return reinterpret_cast<OakEngineMarker *>(c->load_data.markers.at(index));
}

extern "C" int oakengine_clipboard_get_loaded_keyframe_count(
    OakEngineClipboard *cb)
{
    ClipboardCtx *c = ctx(cb);
    if (!c) {
        return OAKENGINE_E_INVALID;
    }
    int total = 0;
    for (auto it = c->load_data.keyframes.cbegin();
         it != c->load_data.keyframes.cend(); ++it) {
        total += it.value().size();
    }
    return total;
}

extern "C" OakEngineKeyframe *oakengine_clipboard_get_loaded_keyframe_at(
    OakEngineClipboard *cb, int index)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || index < 0) {
        return nullptr;
    }
    int current = 0;
    for (auto it = c->load_data.keyframes.cbegin();
         it != c->load_data.keyframes.cend(); ++it) {
        const QVector<olive::NodeKeyframe *> &vec = it.value();
        if (index < current + vec.size()) {
            return reinterpret_cast<OakEngineKeyframe *>(
                vec.at(index - current));
        }
        current += vec.size();
    }
    return nullptr;
}

extern "C" int oakengine_clipboard_foreach_property(
    OakEngineClipboard *cb,
    int (*fn)(OakEngineNode *node, const char *key, const char *value,
              void *userdata),
    void *userdata)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || !fn) {
        return OAKENGINE_E_INVALID;
    }

    for (auto it = c->load_data.properties.cbegin();
         it != c->load_data.properties.cend(); ++it) {
        OakEngineNode *node = reinterpret_cast<OakEngineNode *>(it.key());
        for (auto jt = it.value().cbegin(); jt != it.value().cend(); ++jt) {
            const QByteArray key = jt.key().toUtf8();
            const QByteArray value = jt.value().toUtf8();
            const int stop = fn(node, key.constData(), value.constData(),
                                userdata);
            if (stop != 0) {
                return OAKENGINE_OK;
            }
        }
    }
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_foreach_keyframe(
    OakEngineClipboard *cb,
    int (*fn)(const char *node_id, OakEngineKeyframe *keyframe,
              void *userdata),
    void *userdata)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || !fn) {
        return OAKENGINE_E_INVALID;
    }

    for (auto it = c->load_data.keyframes.cbegin();
         it != c->load_data.keyframes.cend(); ++it) {
        const QByteArray node_id = it.key().toUtf8();
        for (olive::NodeKeyframe *key : it.value()) {
            const int stop = fn(node_id.constData(),
                                reinterpret_cast<OakEngineKeyframe *>(key),
                                userdata);
            if (stop != 0) {
                return OAKENGINE_OK;
            }
        }
    }
    return OAKENGINE_OK;
}

extern "C" int oakengine_clipboard_foreach_connection(
    OakEngineClipboard *cb,
    int (*fn)(OakEngineNode *output_node, OakEngineNode *input_node,
              const char *input_id, int element, void *userdata),
    void *userdata)
{
    ClipboardCtx *c = ctx(cb);
    if (!c || !fn) {
        return OAKENGINE_E_INVALID;
    }

    for (const olive::Node::OutputConnection &oc :
         c->load_data.promised_connections) {
        OakEngineNode *output_node =
            reinterpret_cast<OakEngineNode *>(oc.first);
        OakEngineNode *input_node =
            reinterpret_cast<OakEngineNode *>(oc.second.node());
        const QByteArray input_id = oc.second.input().toUtf8();
        const int stop = fn(output_node, input_node, input_id.constData(),
                            oc.second.element(), userdata);
        if (stop != 0) {
            return OAKENGINE_OK;
        }
    }
    return OAKENGINE_OK;
}
