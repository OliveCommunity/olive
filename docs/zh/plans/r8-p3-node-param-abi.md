# R8-P3 执行手册：清除 app/ 对 node/node.h + node/param.h 的直接引用

> 本手册是**纯执行指令**。所有设计决策已经做出，不要重新设计、不要引入
> 本手册之外的改动。遇到与本手册矛盾的事实时停下来报告，不要自行变通。
>
> 目标：`grep -rn '#include "node/node.h"\|#include "node/param.h"' app/` 结果为 0。
> 原则：**engine/ 侧零改动**。所需的全部 C ABI 函数已存在于
> `engine/include/oakengine/node.h`（映射表见第 3 节），本阶段只在 app/ 内工作。

---

## 落地状态（2026-07-29 修订，取代 §2.1 的 AppNodeInput 方案）

本阶段最终按**双适配器**形态落地，与下文 §2.1 的原始决策不同：

- 消费侧不直接调 C ABI，统一经过 C++ wrapper 层
  `shared/include/oakutil/oaknode.h`（namespace `oak`）：
  `Node`/`Project`/`Footage`/`Input`/`Keyframe`/`KeyframeTrackRef`/`InputPair`
  + `NodeCategory` 枚举 + `NodeConnection`/`ContextNodeItem` 结构。
  wrapper 只做转发；owned/borrowed 语义见文件头注释。
- `app/common/nodeinputhandle.h`（`AppNodeInput`/`AppNodeInputPair`/
  `AppNodeKeyframeTrackReference`/`AppNodeCategory` 方案）**已废弃并删除**，
  全部消费点迁到 `oak::` 类型。
- app/ 中 node/param 相关裸 `oakengine_*` 调用已清零；保留的裸调用均属
  其它子系统，以 `// WRAPPER-GAP:` 注释登记（见下表），归后续批次建立
  对应 wrapper 时清理。

WRAPPER-GAP 登记（按归属批次分组）：

| 归属 | 函数族 | 位置（示例） |
|---|---|---|
| undo 批次 | `oakengine_undo_*`、`oakengine_node_*_command` | nodeview.cpp、nodeparamviewcontext.cpp、mainwindowundo.cpp |
| group 批次 | `oakengine_group_*`（passthrough/resolve/create） | nodeview.cpp、nodeparamview.cpp、nodeparamviewitem.cpp |
| traverse 批次 | `oakengine_traverse_*`、`oakengine_node_set_value_hint` | nodevaluetree.cpp、nodetableview.cpp |
| clipboard 批次 | `oakengine_clipboard_*`、`oakengine_nodes_delete_many` | nodeview.cpp |
| keyframe 查询 | 最早/最晚/最近 keyframe、`get_split_*` 等 | nodeparamviewkeyframecontrol.cpp |
| 杂项 | `Node::getPluginInstance`、`Node::get_input_property`、`oakengine_node_array_insert_at/remove_at`、`Node::brush()`、`Node::has_gizmos`、效果标志常量 | nodeparamviewitem.cpp、nodeparamview.cpp、nodeviewitem.cpp |

除原手册列出的文件外，本次一并完成了前半程未迁完的文件：
`nodeparamviewitem.h/.cpp`、`nodeparamview.h/.cpp`、`nodeparamviewcontext.cpp`、
`nodeparamviewwidgetbridge.cpp`、`nodevaluetree.h/.cpp`、`curvewidget.h/.cpp`、
`curveview.cpp`、`timebasedwidget.cpp`、`nodeviewscene.cpp`、`nodewidget.h`、
`panel/node/node.h`、`panel/param/param.*`、`toolbar.cpp`、`mainwindow.cpp`、
`mainwindowundo.h/.cpp`、`timelinewidget/tool/import.cpp`（仅一处）、
`hashstreamapp.cpp`、`tests/gtest/widget_panels_model_test.cpp`。

### 落地后已知遗留（2026-07-29 Wave3 收尾后）

- ~~`app/widget/viewer/viewerdisplay.h`、`app/widget/colorwheel/*.cpp` 仍直接
  include `node/node.h`~~（Wave2 已清）。
- ~~`app/widget/nodeparamview/nodeparamviewwidgetbridge.h` 公开构造签名仍用
  engine `NodeInput`~~（Wave3 已收敛到 `oak::Input`）。
- app/ 全树唯一保留的违规 include：`app/widget/timebased/timebasedwidget.h`
  的 engine `node/output/viewer/viewer.h`（`QPointer<ViewerOutput>` 需要完整
  QObject 类型，C ABI 无节点销毁事件，文件内有论证，待 facade 增加销毁通知后清理）。
- `tests/gtest/` 5 个文件白盒引用 engine C++ 头（engine 级测试，不在 P3 范围）。
- `nodeparamviewkeyframecontrol.cpp` 等仍经 `node/value.h` 传递使用 engine
  C++ keyframe 查询方法（见 WRAPPER-GAP 表 keyframe 查询行）。
- `app/core.cpp` 与 `app/dialog/otioproperties/otiopropertiesdialog.cpp` 的
  OTIO 适配已在 OTIO 必需化后本机验证编译通过（/opt/otio 0.19 与
  otio-install 0.16 双版本）。
- 全量对象编译 + 全量链接构建均 0 error（cmake-build-debug，含 OTIO）。


---

## 0. 前置条件（先确认再动手）

1. 工作树中 P2 的改动（node/value.h + node/keyframe.h 清理）应先已提交。
   P3 必须基于干净工作树开始，结束时单独成一个 commit。
2. 本机（macOS）验证环境说明，**不要被全量构建失败迷惑**：
   - `liboakengine.dylib` 链接依赖 `18aed979a` 的 `-U,__ZN5olive13k_app_versionE`
     修复，确认该 commit 已在历史中。
   - 本机未安装 OpenTimelineIO，`app/core.cpp` 与
     `app/dialog/otioproperties/otiopropertiesdialog.cpp` **永远无法在本机编译**，
     与本次改动无关，验证时跳过这两个文件。
   - 验证编译用对象级构建（绕过 dylib 依赖与 OTIO 文件）：

     ```bash
     cd cmake-build-debug
     ninja -t targets all | grep -oE 'app/CMakeFiles/libolive-editor\.dir/[^:]+\.o' \
       | sort -u | grep -vE 'dir/core\.cpp\.o|otiopropertiesdialog\.cpp\.o' > /tmp/app_objs.txt
     xargs ninja -j"$(sysctl -n hw.ncpu)" < /tmp/app_objs.txt
     # engine 侧：
     ninja engine/CMakeFiles/oakengine-obj.dir/all 2>/dev/null || ninja $(ninja -t targets all | grep -oE 'engine/CMakeFiles/oakengine-obj\.dir/[^:]+\.o' | sort -u)
     ```
   - 每完成下面一个步骤就跑一次上述 app 对象编译，保持可编译状态。

---

## 1. 现状清单（20 个文件，调查于 P2 完成后）

事实：`engine/node/node.h:40` 自己 include `node/param.h`，所以 include
node/node.h 等于同时引入两个头。

| 分组 | 文件 | 直接 include | 关键用法 |
|---|---|---|---|
| A 琐碎 | `app/widget/nodecombobox/nodecombobox.h` | node/node.h | **完全未使用任何符号**，直接删 |
| A 琐碎 | `app/dialog/preferences/tabs/preferencesappearancetab.cpp` | node/node.h | 仅用 `Node::k_category_count`（:72） |
| A 琐碎 | `app/widget/menu/factorymenu.h` | node/node.h | `Node::CategoryID`/`k_category_unknown`（:44）、`Node*` 返回值（:52） |
| B 指针替换 | `app/widget/nodeparamview/nodeparamviewitembase.h` | node/node.h | `get_title_bar_text_from_node(Node*)`（:52）一处 |
| B 指针替换 | `app/widget/nodetableview/nodetableview.h` | node/node.h | `QVector<Node*>` 参数、`QMap<Node*, QTreeWidgetItem*>`（:44） |
| B 指针替换 | `app/widget/nodevaluetree/nodevaluetree.h` | node/node.h | `set_node(const NodeInput&, Rational)`（:35）一处 |
| C nodeview | `app/widget/nodeview/nodeviewedge.h` | node/node.h | `Node* output_`、`NodeInput input_` 值成员（:125/127） |
| C nodeview | `app/widget/nodeview/nodeviewitem.h` | node/node.h | `Node::Position`（:60/63）、`get_input()` 按值返回 NodeInput（:75） |
| C nodeview | `app/widget/nodeview/nodeviewcontext.h` | node/node.h | `Node*` 成员/参数、`NodeInput` const 引用参数 |
| D TrackRef | `app/widget/nodetreeview/nodetreeview.h` | node/node.h | `NodeKeyframeTrackReference` 值存储、QHash key、signal 签名（:69-73） |
| D TrackRef | `app/widget/keyframeview/keyframeviewinputconnection.h` | node/node.h + node/param.h | `NodeKeyframe*`、`Node::get_keyframe_tracks()`（:55-61） |
| E paramview | `app/widget/nodeparamview/nodeparamview.h` | node/node.h | `QVector<Node*>`、`QHash<Node*,Node*>`、slot 签名带 NodeInput |
| E paramview | `app/widget/nodeparamview/nodeparamviewarraywidget.h/.cpp` | param.h / node.h | `Node* node_`、`node_->input_array_size()`（cpp:53） |
| E paramview | `app/widget/nodeparamview/nodeparamviewconnectedlabel.h/.cpp` | param.h / node.h | `NodeInput input_` 值成员、NodeInput 成员调用 |
| E paramview | `app/widget/nodeparamview/nodeparamviewitem.h` | node/node.h | `QHash<NodeInput,…>`、`QHash<NodeInputPair,…>`、signal 签名 |
| E paramview | `app/widget/nodeparamview/nodeparamviewkeyframecontrol.h` | node/param.h | `NodeInput input_` 值成员、slot 签名 |
| F 特殊 | `app/common/hashstreamapp.cpp` | node/param.h | NodeInput/NodeInputPair/NodeKeyframeTrackReference 的 qHash |
| F 特殊 | `app/widget/viewer/viewerdisplay.h` | node/node.h | `Node*` 参数/成员、`NodeValueRow`（P5 遗留见 4.8） |

`Node::Position`、`Node::get_keyframe_tracks` 的真实用法比预想少：
`Node::Position` 仅 nodeviewitem.h 两处且**纯 UI 聚合，不触 engine**；
`NodeKeyframeTrackReference` 仅 nodetreeview.h + keyframeviewinputconnection.h。

---

## 2. 核心决策（已定，照做即可）

### 2.1 新建 `app/common/nodeinputhandle.h`（本阶段唯一新增文件）

这是 P2 的 `nodevaluehandle.h` 的姊妹文件：app 侧值类型镜像 + 薄封装。
**完整内容如下，直接照抄创建**（许可证头与 P2 文件保持一致）：

```cpp
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

#ifndef OAK_NODEINPUTHANDLE_H
#define OAK_NODEINPUTHANDLE_H

#include <QHash>
#include <QString>

#include "oakengine/node.h"

namespace olive
{

/**
 * @brief App-local mirror of engine's olive::NodeInput (node/param.h).
 *
 * Value type identifying one input (or array element) on a node. The C ABI
 * identifies inputs by (node, input_id, element), so this struct interoperates
 * with oakengine_node_input_*() directly. Semantics must stay identical to the
 * engine type: default-constructed is {nullptr, QString(), -1}.
 */
struct AppNodeInput {
	AppNodeInput() : node(nullptr), element(-1) {}
	AppNodeInput(OakEngineNode *n, const QString &i, int e = -1)
		: node(n), input(i), element(e)
	{
	}

	bool operator==(const AppNodeInput &rhs) const
	{
		return node == rhs.node && input == rhs.input &&
			   element == rhs.element;
	}
	bool operator!=(const AppNodeInput &rhs) const { return !(*this == rhs); }
	bool operator<(const AppNodeInput &rhs) const
	{
		if (node != rhs.node) return node < rhs.node;
		if (input != rhs.input) return input < rhs.input;
		return element < rhs.element;
	}

	bool is_valid() const { return node != nullptr; }

	OakEngineNode *node;
	QString input;
	int element;
};

inline uint qHash(const AppNodeInput &i, uint seed = 0)
{
	return ::qHash(i.node, seed) ^ ::qHash(i.input, seed) ^
		   ::qHash(i.element, seed);
}

/**
 * @brief App-local mirror of engine's olive::NodeInputPair.
 *
 * NOTE: engine's NodeInputPair is {Node *node; QString input;} — NO element.
 * Keep the same shape; it is used as the array-widget key.
 */
struct AppNodeInputPair {
	bool operator==(const AppNodeInputPair &rhs) const
	{
		return node == rhs.node && input == rhs.input;
	}

	OakEngineNode *node = nullptr;
	QString input;
};

inline uint qHash(const AppNodeInputPair &p, uint seed = 0)
{
	return ::qHash(p.node, seed) ^ ::qHash(p.input, seed);
}

/**
 * @brief App-local mirror of engine's olive::NodeKeyframeTrackReference.
 */
struct AppNodeKeyframeTrackReference {
	AppNodeKeyframeTrackReference() : track(-1) {}
	AppNodeKeyframeTrackReference(const AppNodeInput &i, int t = 0)
		: input(i), track(t)
	{
	}

	bool operator==(const AppNodeKeyframeTrackReference &rhs) const
	{
		return input == rhs.input && track == rhs.track;
	}
	bool operator!=(const AppNodeKeyframeTrackReference &rhs) const
	{
		return !(*this == rhs);
	}

	AppNodeInput input;
	int track;
};

inline uint qHash(const AppNodeKeyframeTrackReference &r, uint seed = 0)
{
	return qHash(r.input, seed) ^ ::qHash(r.track, seed);
}

/**
 * @brief App-local mirror of engine's Node::CategoryID ordinals
 * (engine/node/node.h). Values must stay in sync with the engine enum;
 * oakengine_node_category_name() takes these ordinals directly.
 */
enum AppNodeCategory {
	k_app_category_unknown = -1,
	k_app_category_output,
	k_app_category_generator,
	k_app_category_math,
	k_app_category_keying,
	k_app_category_filter,
	k_app_category_color,
	k_app_category_time,
	k_app_category_timeline,
	k_app_category_transition,
	k_app_category_distort,
	k_app_category_project,
	k_app_category_open_fx,
	k_app_category_count
};

/* Thin wrappers over the C ABI replacing the NodeInput member functions that
 * app code actually called. */

inline bool app_input_is_connected(const AppNodeInput &i)
{
	return oakengine_node_input_is_connected(
			   i.node, i.input.toUtf8().constData()) != 0;
}

inline OakEngineNode *app_input_get_connected_node(const AppNodeInput &i)
{
	return oakengine_node_input_get_connected_node(
		i.node, i.input.toUtf8().constData(), i.element);
}

} // namespace olive

Q_DECLARE_METATYPE(olive::AppNodeInput)
Q_DECLARE_METATYPE(olive::AppNodeKeyframeTrackReference)

#endif // OAK_NODEINPUTHANDLE_H
```

要点：
- `AppNodeInput` 字段公开（node/input/element），替换代码里 `input_.node()`
  → `input_.node`、`input_.input()` → `input_.input`、`input_.element()`
  → `input_.element`。
- `Q_DECLARE_METATYPE` 使这两个类型可以进 Qt signal/slot 签名和
  `QVariant::fromValue`（item data）。这与 `OakEngineNode*` 进 MOC 的既有
  先例一致；**不要**再把它们改成非 slot 规避。

### 2.2 `Node::Position` → nodeviewitem 本地纯 UI 结构

`NodeViewItem::get_node_position_data()` 只是把 item 自身的
`pos()+is_expanded()` 打包（nodeviewitem.cpp:120-123），`set_node_position`
只是解包（:137-141），完全不触 engine。决策：在 `nodeviewitem.h` 内定义
局部结构，不进公共头：

```cpp
struct NodeViewItemPosition {
	QPointF position;
	bool expanded = false;
};
```

签名改为 `NodeViewItemPosition get_node_position_data() const` /
`void set_node_position(const NodeViewItemPosition &pos)`。调用点
（nodeview 相关 .cpp）同名替换即可。

### 2.3 `Node*` → `OakEngineNode*`

所有成员变量、参数、返回值、容器 key 机械替换。两者是同一指针的不同
opaque 类型，边界处用 `reinterpret_cast`（既有先例：
nodeparamview.h:152、connectedlabel.cpp 等）。EngineEventBridge 的信号
全部已经是 `OakEngineNode*` + `(QString input, int element)`，lambda 里
把旧的 `NodeInput(reinterpret_cast<Node*>(source), input, element)` 改为
`AppNodeInput(source, input, element)`。

### 2.4 `Node::CategoryID` → `AppNodeCategory`

`preferencesappearancetab.cpp:72` 的 `Node::k_category_count` →
`k_app_category_count`；`factorymenu.h:44` 的参数类型与默认值
`Node::k_category_unknown` → `AppNodeCategory` / `k_app_category_unknown`。
`oakengine_node_category_name(i, …)` 的调用已经用 int 序数，不用动。

### 2.5 `NodeKeyframe*` / `Node::get_keyframe_tracks` → C ABI 循环

keyframeviewinputconnection.h:55-61 目前内联调用
`input_.input().node()->get_keyframe_tracks(input_.input()).at(input_.track())`
返回 `const QVector<NodeKeyframe*>&`。决策：
- 成员类型改为 `AppNodeKeyframeTrackReference input_;`（含 track）。
- `get_keyframes()` 改为**按值返回** `QVector<OakEngineKeyframe*>`，实现
  移到 .cpp，用 C ABI 拼装：

```cpp
QVector<OakEngineKeyframe *> KeyframeViewInputConnection::get_keyframes() const
{
	QVector<OakEngineKeyframe *> keys;
	const int n = oakengine_node_keyframe_count_on_track(
		input_.input.node, input_.input.input.toUtf8().constData(),
		input_.input.element, input_.track);
	keys.reserve(n);
	for (int i = 0; i < n; i++) {
		keys.append(oakengine_node_keyframe_handle_on_track(
			input_.input.node, input_.input.input.toUtf8().constData(),
			input_.input.element, input_.track, i));
	}
	return keys;
}
```

- 完成后 grep `get_keyframes()` 的全部调用方（keyframeview.cpp 等），
  把 `NodeKeyframe*` 改为 `OakEngineKeyframe*`，属性访问改走 P2 已建立的
  keyframe C ABI（`oakengine_node_keyframe_*` / `OakEngineKeyframe` 访问器）。
  若调用方用到某个没有 C ABI 对应的 keyframe 成员函数，**停下来报告**，
  不要自己在 engine 加函数。

### 2.6 hashstreamapp.cpp 瘦身

删除 :31-44 的三个 qHash（NodeInput/NodeInputPair/
NodeKeyframeTrackReference）——App 类型的 qHash 已由新头内联提供。
保留 Track::Reference 的 qHash 与 QDataStream 运算符（来自
`node/output/track/track.h`，属 P6 范围），删除
`#include "node/param.h"`，保留 `#include "node/output/track/track.h"`。

### 2.7 nodetreeview 顺带修一个既有 bug

nodetreeview.h:96-97 `k_item_input_reference` 与 `k_item_node_pointer`
都定义为 `Qt::UserRole + 1`。改为 `Qt::UserRole + 1` / `Qt::UserRole + 2`。
item data 中存的引用类型随之改为
`QVariant::fromValue(AppNodeKeyframeTrackReference)`。

### 2.8 viewerdisplay.h 只做最小改动

该头还有 `node/gizmo/text.h`、`node/output/track/tracklist.h`、
`node/color/colormanager/colormanager.h` 三处直接 include，属 P5/P6 范围，
**本阶段不动**。本阶段只做：
- 删除 `#include "node/node.h"`；
- `set_gizmos(Node*)`、`generate_gizmo_transform(Node*, Node*, …)`、
  成员 `Node *gizmos_` 改为 `OakEngineNode*`；
- `NodeValueRow`、`NodeGizmo*`、`TextGizmo*` 维持现状（它们经 gizmo/text.h
  链传递引入，P5 处理），.cpp 中把 `OakEngineNode*` 传给 gizmo C++ API 的
  边界处加 `reinterpret_cast<Node*>`。

---

## 3. C ABI 映射表（证明 engine 零改动）

| 旧调用 | 替换 |
|---|---|
| `input.is_connected()` | `app_input_is_connected()`（新头，包装 `oakengine_node_input_is_connected`） |
| `input.get_connected_output()` | `app_input_get_connected_node()`（包装 `oakengine_node_input_get_connected_node`） |
| `input.node()/.input()/.element()` | `AppNodeInput` 公开字段 |
| `node->input_array_size(id)` | `oakengine_node_input_array_size` |
| `node->name()` | `oakengine_node_get_name` |
| `Node::disconnect` 等 | 已迁过（`oakengine_node_disconnect_ex` 等），不在本阶段 |
| `Node::get_keyframe_tracks` | `oakengine_node_keyframe_count_on_track` + `oakengine_node_keyframe_handle_on_track`（见 2.5） |
| `Node::k_category_count` / `CategoryID` | `AppNodeCategory`（2.4） |
| `Node::Position` | 本地 `NodeViewItemPosition`（2.2） |
| `NodeInputPair` | `AppNodeInputPair`（注意：无 element 字段） |
| `NodeKeyframeTrackReference` | `AppNodeKeyframeTrackReference`（字段 input/track 公开） |

---

## 4. 执行步骤（按序，每步后可编译）

### 4.1 步骤 1：新建头 + hashstreamapp 瘦身
- 按 2.1 创建 `app/common/nodeinputhandle.h`。
- 按 2.6 改 `app/common/hashstreamapp.cpp`。
- 编译验证（此时还没有使用方，只验证新头自身可编译：随便一个已改文件
  include 它即可，或等到步骤 2 一起验证）。

### 4.2 步骤 2：A 组（琐碎）
- `nodecombobox.h`：删除 `#include "node/node.h"`。
- `preferencesappearancetab.cpp`：include 换 `common/nodeinputhandle.h`，
  :72 `Node::k_category_count` → `k_app_category_count`。
- `factorymenu.h`：include 换 `common/nodeinputhandle.h`；:44 参数类型
  `Node::CategoryID` → `AppNodeCategory`，默认值 → `k_app_category_unknown`；
  :52 返回值 `Node*` → `OakEngineNode*`。检查 factorymenu.cpp 及调用方
  （`create_node_from_menu_action` 的使用处）同步改类型。

### 4.3 步骤 3：B 组（纯指针/引用替换）
- `nodeparamviewitembase.h`：`Node*` → `OakEngineNode*`；检查对应
  .cpp 实现内部（如用 `n->name()`/`n->GetLabel()` 改 C ABI
  `oakengine_node_get_name`/`oakengine_node_get_label`）。
- `nodetableview.h`：`QVector<Node*>` → `QVector<OakEngineNode*>`，
  `QMap<Node*, QTreeWidgetItem*>` → `QMap<OakEngineNode*, …>`。
  该头用到的 `Rational` 来自 core（`olive/core/...`），确保 include
  core 头而不是靠 node.h 传递。
- `nodevaluetree.h`：`set_node(const NodeInput&, const Rational&)` →
  `set_node(const AppNodeInput&, const Rational&)`；.cpp 内
  `input.node()/input()/element()` 改字段访问 + C ABI。

### 4.4 步骤 4：C 组（nodeview 簇）
顺序：nodeviewedge.h → nodeviewitem.h → nodeviewcontext.h → 各自 .cpp。
- `nodeviewedge.h`：`Node* output_` → `OakEngineNode*`；
  `NodeInput input_` → `AppNodeInput input_`；`output()`/`input()` 返回类型
  同步。构造函数参数同步。
- `nodeviewitem.h`：按 2.2 加 `NodeViewItemPosition`；`Node*` →
  `OakEngineNode*`；`NodeInput get_input()` → `AppNodeInput get_input()`
  （返回 `AppNodeInput(node_, input_, element_)`）；
  `get_item_for_input(NodeInput)` → `AppNodeInput`。
- `nodeviewcontext.h`：`Node*` → `OakEngineNode*`（含 `QMap`、`QHash` key、
  `context_subs_`）；`const NodeInput &` 参数 → `const AppNodeInput &`。
- 各 .cpp：桥接 lambda 按 2.3 组装 `AppNodeInput`；其它 Node 成员调用改
  C ABI（绝大多数之前已迁，只剩类型改名）。

### 4.5 步骤 5：D 组（TrackRef）
- `nodetreeview.h`：include 换 `common/nodeinputhandle.h`（保留已有的
  `oakengine/node.h`）；`NodeKeyframeTrackReference` →
  `AppNodeKeyframeTrackReference`（signals、成员、QHash key 全部）；
  按 2.7 修 UserRole bug；`Node*` → `OakEngineNode*`。
- `nodetreeview.cpp`：`ref.input()` → `ref.input`，`ref.track()` →
  `ref.track`；item data 读写改 `QVariant::fromValue(...)`/`value<...>()`。
- `keyframeviewinputconnection.h/.cpp`：按 2.5。注意 `get_reference()`
  返回类型改为 `AppNodeKeyframeTrackReference`。

### 4.6 步骤 6：E 组（nodeparamview 簇，最大）
顺序：connectedlabel → keyframecontrol → arraywidget → item → view。
- `nodeparamviewconnectedlabel.h/.cpp`：
  - 成员 `NodeInput input_` → `AppNodeInput input_`；
    `Node *connected_node_` → `OakEngineNode *connected_node_`；
  - slots `input_connected/input_disconnected(OakEngineNode*, const NodeInput&)`
    第二个参数改 `const AppNodeInput &`；
  - cpp:85-89 `input_.is_connected()`/`get_connected_output()` →
    `app_input_is_connected(input_)`/`app_input_get_connected_node(input_)`；
  - cpp:95-108 lambda 里 `NodeInput(reinterpret_cast<Node*>(source), …)` →
    `AppNodeInput(source, …)`；
  - cpp:180-181 的 `oakengine_node_disconnect_ex` 调用已合规，只需把
    `input_.node()`/`input_.input()`/`input_.element()` 改字段访问；
  - cpp:199 `connected_node_->name()` → `oakengine_node_get_name`；
  - `value_tree_->set_node(input_, …)` 随 4.3 的 nodevaluetree 改后自然兼容。
- `nodeparamviewkeyframecontrol.h`：`NodeInput input_` → `AppNodeInput`；
  slot `keyframe_enable_changed(const NodeInput&, bool)` → `AppNodeInput`；
  `get_connected_input()` 返回类型同步。
- `nodeparamviewarraywidget.h/.cpp`：`Node* node_` → `OakEngineNode*`；
  cpp:53 `node_->input_array_size(input_)` →
  `oakengine_node_input_array_size(node_, input_.toUtf8().constData())`；
  cpp:45 的 `reinterpret_cast<void*>(node_)` 订阅保持不变。
- `nodeparamviewitem.h`：`QHash<NodeInput, InputUI>` →
  `QHash<AppNodeInput, InputUI>`；`NodeInputPair` → `AppNodeInputPair`；
  signal `input_checked_changed(const NodeInput&, bool)` 与
  slot `edge_changed(OakEngineNode*, const NodeInput&)` 改 `AppNodeInput`；
  `get_element_y(NodeInput)` → `AppNodeInput`；`Node*` → `OakEngineNode*`。
  对应 .cpp（nodeparamviewitem.cpp）内 NodeInput 构造/比较改 AppNodeInput。
- `nodeparamview.h`：`QVector<Node*>` ×4 → `QVector<OakEngineNode*>`；
  `QHash<Node*, Node*>`（paste 映射、generate_existing_paste_map）→
  `QHash<OakEngineNode*, OakEngineNode*>`；
  `QHash<Node*, QPair<int64_t,int64_t>> context_subs_` → key 换类型；
  slots `input_check_box_changed / group_input_passthrough_added/removed`
  的 `const NodeInput &` → `const AppNodeInput &`；
  `get_snap_ignore_keyframes()` 返回 `std::vector<NodeKeyframe*>*` →
  `std::vector<OakEngineKeyframe*>*`（NodeKeyframe 经 node.h 传递而来，
  随 include 删除必须一并处理；调用点 grep `get_snap_ignore_keyframes`
  逐一适配，keyframe 属性访问走 P2 的 C ABI）。

### 4.7 步骤 7：F 组（viewerdisplay.h）
按 2.8 做最小改动。

### 4.8 步骤 8：验证 + 提交
```bash
# 1. 直接 include 清零
grep -rn '#include "node/node.h"\|#include "node/param.h"' app/
# 期望：无输出

# 2. engine 符号泄漏检查（app 对象中不应出现 olive::Node/NodeInput 等未定义符号）
#    对象编译全部通过后即视为通过

# 3. app 对象编译（命令见第 0 节）
# 4. engine 未动：git diff --stat -- engine/ 应为空

# 5. 提交（单独一个 commit）
git add app/ docs/
git commit -m "R8 phase 3: replace node/node.h + node/param.h in app/ with C ABI and app-local value types"
```

---

## 5. 明令禁止

- 不要改 engine/ 下任何文件（包括 oakengine/node.h）。
- 不要把 `NodeInput` 等 engine 类型换成 `using` 别名指回 engine 头。
- 不要在本阶段处理 P4-P9 的范围（viewerdisplay.h 的 gizmo/tracklist/
  colormanager include、nodeviewcontext.cpp 的 block.h/track.h/project.h、
  hashstreamapp.cpp 的 track.h）。
- 不要"顺手"重构无关代码；唯一允许的顺手修复是 2.7 的 UserRole bug。
- 发现映射表覆盖不到的 engine 成员调用时，停下来报告，不要自行在
  engine 加 C ABI 函数。

## 6. 已知遗留（写进提交说明/计划文档状态）

- `app/widget/viewer/viewerdisplay.h/.cpp` 仍有 gizmo/text.h、tracklist.h、
  colormanager.h 直接 include（P5/P6）。
- `app/widget/nodeview/nodeviewcontext.cpp` 有 block.h、track.h、project.h、
  sequence.h 直接 include（P3/P4 后续批）。
- `app/common/hashstreamapp.cpp` 保留 track.h（P6）。
