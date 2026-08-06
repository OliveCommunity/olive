# oaknode 去 Qt 化约定（DEQT）

> 第一波（核心基建）确立的替换规则。后续波次处理叶子节点文件时**严格照此机械替换，不改任何行为逻辑**。
> 有疑问先查本文件；本文件没覆盖的 Qt 类型，在 `src/node/DEQT.md` 补一条规则再动手。

## 1. 替换映射表

| Qt | 替代 | 说明 |
|---|---|---|
| `QString` | `std::string` | 默认参数 `QString()` → `std::string()`；`isEmpty()` → `empty()`；`==/!=` 直接用 |
| `QStringList` | `olive::StringList`（= `std::vector<std::string>`，定义在 `node/variant.h`） | |
| `QByteArray` | `olive::ByteArray`（= `std::vector<char>`） | base64 用 `olive::byte_array_to_base64()` / `byte_array_from_base64()` |
| `QVector<T>` | `std::vector<T>` | `append`→`push_back`，`prepend`→`insert(begin(), x)`，`takeAt(i)`→取值后 `erase(begin()+i)`，`removeAt`→`erase`，`contains`→`std::find(...)!=end()`，`indexOf`→`std::find`-`begin()`（无则 -1），`first/last`→`front/back`，`count/size`→`size()`（必要时 `int(...)`），`isEmpty`→`empty` |
| `QList<T>` | `std::vector<T>` | 同上 |
| `QStringList::split` / `s.split(':')` | `olive::core::StringUtils::split(s, ':')`（`olive/core/util/stringutils.h`） | |
| `QStringLiteral("x")` | `"x"` | |
| `QString::number(x)` | `std::to_string(x)`（整数）；double/float 用 `snprintf("%g")`（Qt 默认 'g' 6 位有效数字，value.cpp 里有 `number_to_string()` 静态函数可复制） | |
| `QString("%1...").arg(a,b)` | 字符串拼接 | 保持参数顺序与原文一致 |
| `s.toStdString()` | 直接用（已是 std::string） | |
| `s.toFloat()/toDouble()/toLongLong()` | `strtof/strtod/strtoll(s.c_str(), nullptr, 10)` | 失败返回 0 的语义一致 |
| `QVariant` | `olive::Variant`（`node/variant.h`） | 见 §2 |
| `QVector2D/3D/4D` | `olive::Vector2D/3D/4D`（`node/mathtypes.h`） | float 存储，API 同名（`x()`、`set_x()` … snake_case） |
| `QMatrix4x4` | `olive::Matrix4x4`（`node/mathtypes.h`） | 行主序 `m[row][col]`，`inverted()`、`transposed()`、`operator()(r,c)` |
| `QPointF` | `olive::PointF`（`node/mathtypes.h`） | `x()/y()/set_x()/set_y()`，运算符齐全 |
| `QTransform` | `olive::Matrix4x4` | `translate(x,y)`、`scale(x,y)`、`rotate(deg)` 成员已提供（后乘语义，同 QTransform） |
| `QHash<K,V>` | `std::map<K,V>` 或 `std::unordered_map<K,V>` | 需要键排序/迭代稳定用 `std::map`；`value(k)`→查找后返回默认值，`contains`→`count(k)` |
| `QMap<K,V>` | `std::map<K,V>` | |
| `QMutex` + `QMutexLocker` | `std::mutex` + `std::lock_guard<std::mutex>` | |
| `QXmlStreamReader/Writer` | `olive::XmlStreamReader/XmlStreamWriter`（`xmlutils.h`，oakcommon） | API 形状对齐 Qt：`read_next()`→`is_start_element()` 循环；`attributes()` 返回 `std::vector<XmlStreamAttribute>`（`.name`/`.value` 都是 std::string）；`readElementText()`→`read_element_text()`；`skipCurrentElement()`→`skip_current_element()`；`xml_read_next_start_element(reader)` 自由函数同 Qt 版辅助 |
| `tr("...")` / `QCoreApplication::translate("Ctx","...")` | 直接留原文字符串字面量 `"..."` | 翻译由 app 层负责 |
| `Q_OBJECT` / `signals:` / `slots:` / `emit x(...)` | 全部删除 | 见 §4 |
| `Q_DECLARE_METATYPE(T)` | 删除 | Variant 不需要注册 |
| `qHash(...)` | 删除（改用 `std::map`/`std::unordered_map`，unordered 需要时写 `std::hash` 特化） | |
| `QObject::connect/disconnect/sender()` | 删除（事件订阅移出 oaknode） | 连接信号语句整体删除，无对应逻辑保留 |
| `foreach (const T &x, list)` | `for (const T &x : list)` | |
| `qWarning() << ...` | `fprintf(stderr, ...)` | |
| `QFont` | `std::string`（family 名） | k_font 类型值直接存 family 字符串 |
| `QDateTime::fromMSecsSinceEpoch(ms, tz).toString(fmt)` | `std::tm`（`gmtime_r`/`localtime_r`）+ 按 Qt 格式 token（`yyyy/MM/dd/hh/mm/ss/zzz` 等）展开的本地静态函数 | 仅 timeformat 节点用；UTC ↔ `QTimeZone::utc()`，local ↔ `QTimeZone::systemTimeZone()` |
| `QDateTime::currentMSecsSinceEpoch()` | `std::chrono::system_clock::now()` 转毫秒 | |
| `Q_UNUSED(x)` | `(void) x;` | |
| `Q_ASSERT(x)` | `assert(x)`（`<cassert>`） | |
| `qMax/qMin` | `std::max/std::min`（`<algorithm>`） | |
| `qFuzzyCompare(a,b)` | 内联展开 Qt 语义：`std::abs(a-b)*100000.0f <= std::min(std::abs(a),std::abs(b))` | |
| `qIsNull(f)` | `f == 0.0f` | |
| `Q_PROCESSOR_X86/ARM` | `OLIVE_PROCESSOR_X86/ARM` + `#include "olive/core/util/cpuoptimize.h"` | ARM 走内置 sse2neon |
| `QUuid`（render cache uuid 边界） | `std::string`：`set_uuid(text)` / `get_uuid()` 直接收发字符串 | M7 定型 cache 类时遵循 |
| `Qt::KeyboardModifiers` | `int`（gizmo_drag_move 参数） | gizmo 波次对齐 |
| `qEnvironmentVariableIsSet("X")` | `std::getenv("X") != nullptr` | |
| `quintptr` | `uintptr_t` | |
| `qAbs(x)` | `std::abs(x)`（`<cstdlib>`/`<cmath>`，按参数类型选头） | |
| `QPolygonF` | `std::vector<olive::PointF>` | `translate(d)` → 循环 `p += d`；`QPolygonF(QRectF(l,t,w,h))` 按 Qt 语义展开为 5 点闭包 `(l,t),(l+w,t),(l+w,t+h),(l,t+h),(l,t)` |
| `QTransform::map(QPointF)` / `QMatrix4x4::map(QPointF)` / `m.toTransform().map(p)` | `Matrix4x4::map(p)`（mathtypes.h 新增，`PointF map(const PointF&) const`） | 三者对 2D 点语义一致（z=0 透视除法）；`toTransform()` 直接去掉 |
| `QVector2D::toPointF()` | `Vector2D::to_point_f()`（mathtypes.h 新增） | |
| gizmo drag 回调里的 `sender()` | `Node::current_gizmo()`（node.h 新增；返回 `NodeGizmo*`，用法 `static_cast<DraggableGizmo*>(current_gizmo())`） | DraggableGizmo 由 gizmo 波次在直接调用回调前后 `set_current_gizmo()`；回调外为 nullptr |
| `QPainter/QBrush/QLinearGradient/QColor` 等 UI 绘制 | 删除，属 app 层 | 在 §4 清单记录 |
| `QPainterPath` | `olive::PainterPath`（`node/geometry.h`，最小记录型 POD：`move_to/line_to/cubic_to/translated/elements`） | 填充光栅化走 facade 安装的 `PathFillBackend` 钩子（同文件，inline 变量，默认 nullptr=不绘制），见 §7.10 |
| `QRectF` | `olive::RectF`（`node/gizmo/text.h`，gizmo 波次落地：仅 x/y/width/height 数据载体；translate/bounding 在使用点展开） | |
| `QPolygonF` | `std::vector<olive::PointF>` | |
| `QLineF` | `olive::LineF`（`node/gizmo/line.h`，gizmo 波次落地：两点 POD，`p1()/p2()`） | |
| `QTextDocument`/`QTextOption`/`QAbstractTextDocumentLayout`/`QFont`（文本节点排版+栅格化） | `TextLayoutRequest`/`TextLayoutSize`/`TextRenderTarget`/`TextRenderTransform` POD（`node/generator/text/textbackend.h`）+ `TextMeasureBackend`/`TextRenderBackend` 钩子 | k_font 输入值仍按上表存 family 字符串；钩子默认 nullptr=量测返回 0/不绘制，见 §7.10 |
| `Qt::Alignment`（gizmo 边界的对齐标志） | `int`，取值按 `TextGizmo::VerticalAlignment`（0=top/1=bottom/2=vcenter，gizmo 波次与 oakengine facade 对齐） | |
| `Qt::AltModifier`/`Qt::ShiftModifier`（gizmo_drag_move 位测试） | `int` 位测试，常量值同 Qt（`0x08000000`/`0x02000000`），用文件内 constexpr | gizmo 波次统一 |
| render 边界的字符串参数（`ShaderJob::insert`/`set_shader_id`、`AcceleratedJob::get`、`ShaderCode` 构造、`ShaderRequest::id` 等） | 直接传 `std::string`/字面量 | render 头当前仍是 Qt QString 版（M7 波次按 §6 stub 契约转 std::string），语法自查以 stub 为准 |
| `QLineF` | `olive::LineF`（`node/gizmo/line.h`，仅 p1()/p2() 数据载体） | 仅 gizmo 用 |
| `QRectF` | `olive::RectF`（`node/gizmo/text.h`，x()/y()/width()/height() 数据载体） | 仅文本 gizmo 矩形用 |
| `QPolygonF` | `std::vector<olive::PointF>` | `boundingRect()`/`containsPoint()` 由调用方（app/facade）自行实现 |
| `QPainterPath` | 删除 | 绘制图元，属 app 层；PathGizmo 只留类壳（层级/类型标识用） |
| `Qt::Alignment` | `int`（TextGizmo 垂直对齐：0=Top 1=Bottom 2=VCenter，与 oakengine facade 一致） | gizmo 波次对齐 |
| `QUuid`（Project/node uuid） | `std::string`，保留 QUuid 文本格式（带花括号 `{8-4-4-4-12}` 小写 hex）；`QUuid::createUuid()` → 本地随机生成同格式字符串（v4/variant 位照设） | 读写均按原文本，工程文件兼容 |
| `QFileInfo::completeBaseName()` | `std::filesystem::path(p).filename()` 截取到第一个 `.` 为止 | |
| `QFileInfo::exists(p)` | `std::filesystem::exists(p, ec)` | |
| `QFileInfo(f).lastModified().toMSecsSinceEpoch()` | `std::filesystem::last_write_time` + file clock→system_clock 换算（`t - file_clock::now() + system_clock::now()`） | |
| `QFile` 整文件读/写 | `std::ifstream`/`std::ofstream`（binary）+ `std::stringstream`；`XmlStreamReader` 直接吃 `std::string` | |
| `qCompress`/`qUncompress` | zlib `compress2`/`uncompress` + 4 字节大端未压缩长度头（Qt 格式原样） | `.ove` 的 OVEC 段逐字节兼容，见 §7.11 |
| `QStandardPaths::CacheLocation` | macOS `$HOME/Library/Caches/oak`，否则 `FileFunctions::get_configuration_location()+"/cache"` | 仅 footage 探针缓存目录；位置变化只导致重新探针 |
| `QTimer` 周期回调（footage `check_footage`） | 删除定时器，函数本体保留为 public，由 facade 周期调用 | `qApp->activeWindow()` 门槛一并移到 app 层 |

### 虚函数命名约定

Node 的虚函数（含事件钩子 `InputValueChangedEvent`/`InputConnectedEvent`/`InputDisconnectedEvent`/
`OutputConnectedEvent`/`LoadFinishedEvent`/`AddedToGraphEvent` 等）**保持原 CamelCase 名字不变**
（虚函数 API 形状保持，仅换参数/返回类型）。子类 override 同理，不要 snake_case 化。

### NodeValueRow 访问

`NodeValueRow = std::map<std::string, NodeValue>`，const 引用无 `operator[]`：
`value[k]` → `value.at(k)`（键必须存在，语义同 const `QHash::operator[]`）。

### QObject 父子机制的替代（keyframe / node 生命周期）

- `NodeKeyframe` 持有 `Node *parent_`：构造参数传入，或 `set_parent(Node*)`。
  `Node::add_keyframe(key)` / `remove_keyframe(key)` 会自动 `set_parent(this)` / `set_parent(nullptr)`——
  替代原 childEvent 的 ChildAdded/ChildRemoved 分支。**不要再对 keyframe 调 setParent()**。
- `NodeInputImmediate::delete_all_keyframes(std::vector<NodeKeyframe*> *reclaimed = nullptr)`：
  传 nullptr 即删除；传指针则把 keyframe 收回向量（替代原"reparent 到 memory_manager 续命"）。
- nodeundo 的命令用 `std::unique_ptr<Node>` / `std::vector<std::unique_ptr<Node>>` 替代
  `QObject memory_manager_`（析构即删未交出的节点，undo 重新入图前 `release()`）。
- 节点入图/出图：`graph_->add_node(node)` / `graph_->remove_node(node)`（Project 波次提供；
  remove 是"摘出不删除"）。
- gizmo：`Node::add_gizmo()/remove_gizmo()` 替代 childEvent 的 gizmo 分支（gizmo 波次对齐）。

### include 路径写法（src/node/src 为根）

- 本模块内：`"node/value.h"`、`"node/keyframe.h"` 等（CMake include root = `src/node/src`）。
- oakcommon：`"xmlutils.h"`、`"debug.h"`、`"define.h"`（include dir = `src/common/src`，**没有** `common/` 前缀）。
- oakundo：`"undocommand.h"`、`"undostack.h"`（include dir = `src/undo/src`）。
- oakcore C++ 封装：`"olive/core/util/color.h"`、`"olive/core/util/bezier.h"`、`"olive/core/util/rational.h"`、`"olive/core/util/timerange.h"`、`"olive/core/util/stringutils.h"`。
- `render/...`、`codec/...`、`pluginSupport/...`（OpenFX）include **原样保留**（M7/M9 处理），禁止新增。

### olive 命名空间别名（value.h 已建立，直接可用）

```cpp
namespace olive {
using core::Bezier;   // olive::core::Bezier
using core::Color;    // olive::core::Color (float RGBA, red()/green()/blue()/alpha())
using core::Rational; // olive::core::Rational (to_string()/from_string() 是 std::string)
}
```

`olive::core::TimeRange` 在 node.h 以 `using core::TimeRange;` 引入（见 node.h）。

## 2. Variant（QVariant 替代）速查

```cpp
#include "node/variant.h"   // olive::Variant, olive::StringList, olive::ByteArray

Variant v;                          // null，v.is_null() == true
Variant a = 42;                     // int（有符号统一存 int64_t）
Variant b = 3.14;                   // double（float 也存 double）
Variant c = std::string("x");
Variant d = Vector2D(1, 2);         // 任意可复制类型，类型擦除存储（原 Q_DECLARE_METATYPE 场景）

v.value<int64_t>();                 // 取数（QVariant::value<T>()）
v.value<Vector2D>();                // 自定义类型必须类型精确匹配，否则返回 T()
v.to_double(); v.to_float();        // QVariant 风格转换（数值互通、字符串解析）
v.to_int(); v.to_uint();
v.to_long_long(); v.to_u_long_long();
v.to_bool();
v.to_string();                      // double 按 %g 格式化（同 QString::number）
v.to_string_list();                 // QStringList
v.to_byte_array();                  // QByteArray
v.can_convert<Vector2D>();          // QVariant::canConvert<T>()
Variant::from_value(x);             // QVariant::fromValue
v == w;                             // 数值跨 kind 按值比较；自定义类型用其 operator==
```

- `value<QString>()` → `value<std::string>()` 或 `to_string()`。
- 原来 `QVariant::fromValue(Color(...))` → `Variant::from_value(Color(...))`。
- 函数返回 `QVariant` 的（如 `Node::get_standard_value()`）→ 返回 `Variant`，调用处照旧 `Variant v = ...; v.to_double()`。

## 3. 核心类新 API 形态

### NodeValue（`node/value.h`）

```cpp
NodeValue v(NodeValue::k_float, 1.5, from_node);          // 构造（模板，不变）
v.type();                                                 // NodeValue::Type
v.value<double>(); v.to_double(); v.to_string();          // 取数
v.data();                                                 // const Variant &
v.set_value(x);
NodeValue::value_to_string(type, variant, is_key_track);  // std::string
NodeValue::string_to_value(type, str, is_key_track);      // Variant
NodeValue::split_normal_value_into_track_values(type, v); // std::vector<Variant>
NodeValue::combine_track_values_into_normal_value(type, split); // Variant
v.to_split_value();                                       // SplitValue = std::vector<Variant>
```

### NodeValueTable

`push/prepend/at/take_at/count/has/remove/clear/is_empty/get(type, tag)/merge(std::vector<NodeValueTable>)`。
`NodeValueRow = std::map<std::string, NodeValue>`。

### Node（`node/node.h`）

- 不再继承 QObject；纯虚 `name()/id()` 返回 `std::string`，`category()` 返回 `std::vector<CategoryID>`，
  `description()` 返回 `std::string`，`sub_category()` 返回 `std::string`。
- 输入遍历：`for (const std::string &id : node->inputs())`。
- `Position`：`PointF position; bool expanded;`，`load(XmlStreamReader*)` / `save(XmlStreamWriter*)`。
- 序列化：`load(XmlStreamReader*, SerializedData*)` / `save(XmlStreamWriter*)`。
- gizmo/undo 等签名里的 `MultiUndoCommand` 来自 oakundo（`undocommand.h`）。

### NodeKeyframe（`node/keyframe.h`）

- `bezier_control_in()/out()` 返回 `const PointF &`；不再有任何 signal。
- 值类型：`Variant value()` / `set_value(const Variant&)`。

## 4. 被删除的东西（第一波）

### Node 的 signals（整组删除，facade 层经 oakengine_event 发通知）

label_changed、color_changed、value_changed、input_connected、input_disconnected、
output_connected、output_disconnected、input_value_hint_changed、input_property_changed、
links_changed、input_array_size_changed、keyframe_added、keyframe_removed、
keyframe_time_changed、message_count_changed、keyframe_type_changed、
keyframe_value_changed、keyframe_enable_changed、input_added、input_removed、
input_name_changed、input_data_type_changed、added_to_graph、removed_from_graph、
node_added_to_context、node_position_in_context_changed、node_removed_from_context、
input_flags_changed。

### NodeKeyframe 的 signals

value_changed、time_changed、type_changed、bezier_control_in_changed、
bezier_control_out_changed（删除理由同上）。

### UI 绘制（属 app 层）

`Node::gradient_color()`、`Node::brush()`（QLinearGradient/QBrush）；`Node::color()` 保留（返回 olive::core::Color 数据）。
`Node::gizmo_transformation()` 的 QTransform 改 Matrix4x4（数据类型，不是绘制）。

### 其他

- `childEvent(QChildEvent*)`（QObject 事件机制）——keyframe 分支变 `Node::add_keyframe()/remove_keyframe()`，
  gizmo 分支变 `Node::add_gizmo()/remove_gizmo()`。
- `Q_DECLARE_METATYPE`、`qHash()` 重载。
- nodeundo 的 23 处 `get_relevant_project()` override（modified 语义由 oakundo 回调承担）。
- `NodeAddCommand::push_to_thread(QThread*)`（QObject 线程亲和）。
- render cache 的 `QUuid` uuid：改 `std::string`（见映射表）。
- keyframe 失效通知链：原 keyframe signal→Node slot 的 5 条（invalidate_from_keyframe_*）随 signal 删除，
  函数本体保留为 public 成员（带 `NodeKeyframe *key` 参数替代 sender()），**调用方由 facade/keyframe 波次接**。

## 5. undo（oakundo）适配

- `#include "undocommand.h"`，`olive::UndoCommand` / `olive::MultiUndoCommand`。
- **没有** `get_relevant_project()`：原来 `get_relevant_project()->set_modified(true)` 的语义由
  `UndoCommand::set_modified_callbacks(is_modified, set_modified)` +
  `redo_and_set_modified()/undo_and_set_modified()` 承担。nodeundo 的命令类不再碰 Project 的
  modified 标记，回调由 facade 层装配。
- `prepare()` 仍是 protected virtual；`UndoStack::push(cmd)` 语义不变（见 oakundo undostack.h）。

## 6. 语法自检

整库编译本波必然失败（叶子未改），但每个改过的 .cpp 必须过 `-fsyntax-only`（缺失的 render/ 等头用 stub 垫）：

```bash
# stub 头在 /tmp/oakstub（render/texture.h 等，仅语法检查用，不进仓库）
cd src/node/src/node && c++ -std=c++17 -fsyntax-only -Wall \
  -I. -I.. -I/tmp/oakstub \
  -I$OAK/src/common/src -I$OAK/core/include -I$OAK/src/undo/src \
  -I/opt/homebrew/include -I/opt/homebrew/include/Imath \
  -I$OAK/third_party/openfx/include -I$OAK/third_party/openfx/HostSupport/include \
  <file.cpp>
```

（`$OAK` = /Users/sunyu/Projects/oak）

## 7. 已知行为注意点与后续波次依赖

1. `NodeValueDatabase::merge()`：原 `QHash::values()` 无序，现按 key 字典序收集后合并——
   merge 是按优先级覆盖语义，若调用方依赖原哈希序需注意。
2. `NodeValueTable` 新增了 `operator==/!=`（Variant 存 `NodeValueTableArray` 需要）。
3. `FrameHashCache(this)` 等 cache 构造仍传 `this`（现在是 `Node *` 而非 `QObject *`）——
   M7 定型 render cache 类时构造参数需接受 `Node *`（或届时改 nullptr，需裁决）。
4. `traverser.h` 新增 `TimeRangeLess` 比较器（`QHash<TimeRange,…>`→`std::map` 需要严格弱序，
   按 `(in(), out())` 排序）。
5. nodeundo 假定 `Project::add_node/remove_node/is_being_cleared/
   get_number_of_contexts_node_is_in(node,bool)` 存在（project 波次提供，M3 手册 §2 已冻结
   add/remove_node 函数族）。
6. `factory.cpp` 的 `pluginSupport/` include、`traverser.h` 的 `"common/cancelableobject.h"`
   原样保留（后者 oakcommon 没有，留 M 系列裁决）。
7. ~~`node.h` 仍 include `config/config.h`~~ —— config 波次已处理：`configaccessor.h`
   （src/node/src）替代 engine Qt 头与 transition stub，`OAK_CONFIG*` 宏在消费侧本地重定义为
   `oakcommon_config_*` C 调用，调用点零改动；`ui/colorcoding.h` 仍走 stub（UI 波次）。
8. `Node::gizmo_drag_move` 的 modifiers 参数为 `int`（原 `Qt::KeyboardModifiers`）。
9. timeformat 的 `format_date_time()` 不实现 `MMMM`（月名）/`dddd`（星期名）本地化 token
   （原默认格式 `hh:mm:ss` 用不到）；负 epoch 毫秒的 `zzz` 取模与 Qt 有边界差异（实际输入不会触发）。
10. generator 波次（matrix/noise/polygon/shape/solid/text/multicam）：
    - `gizmo_drag_move` 原 slot 用 `sender()` 取被拖 gizmo；gizmo 波次定型为
      `Node::current_gizmo()`（DraggableGizmo 直调 3 参虚函数期间设置），本波照此收口。
    - `new PathGizmo(this)`/`new TextGizmo(this)` 后必须显式 `add_gizmo(...)`
      （NodeGizmo 构造不再自注册；`add_draggable_gizmo<>()` 内部已含）。
    - PathGizmo 不再存储路径（gizmo 波次：绘制数据归 app 层），polygon 原
      `poly_gizmo_->set_path(...)` 调用删除；`generate_path()` 仍供 generate_frame 使用。
    - polygon/text 的 `generate_frame()` 光栅化（QPainter/QTextDocument）委托给
      `PathFillBackend`/`TextMeasureBackend`+`TextRenderBackend` 钩子（geometry.h /
      textbackend.h）；facade 未安装后端前输出为空白（被迫行为差异，facade 波次恢复）。
      像素缓冲清零、alpha 移植循环等纯数据逻辑原样保留。
    - textv2 非 SSE 死分支里的 `VideoParams::kRGBAChannelCount` 拼写在任何 Qt 头中都不存在
      （原代码靠 Q_PROCESSOR_X86/ARM 宏永远不编译该分支），改为 `k_rgba_channel_count`。
10. 叶子效果波次（audio/distort/effect/filter/keying）新增约定（见映射表新增行）：
    `Matrix4x4::map(PointF)`、`Vector2D::to_point_f()` 为 mathtypes.h 增量补充；
    `Node::current_gizmo()`/`set_current_gizmo()` 为 node.h 增量补充，替代 gizmo 回调中的
    `sender()`；gizmo 波次须在 DraggableGizmo 直接调用 drag 回调时包一层
    `set_current_gizmo(this)` / `set_current_gizmo(nullptr)`。
    gizmo 头（`node/gizmo/polygon.h` 的 `set_polygon(QPolygonF)`、`point.h`/`text.h` 的
    QRectF/QTransform 接口）仍属 gizmo 波次，叶子文件按新类型（`std::vector<PointF>`）调用，
    签名对齐由 gizmo 波次完成。
10. gizmo 波次：`NodeGizmo` 构造**不自动**向 parent 注册（`Node::add_draggable_gizmo()` 已显式
    `add_gizmo()`）；析构时若持有 parent 则 `remove_gizmo()`（对应原 dtor 的 `setParent(nullptr)`）。
    直接 `new XGizmo(node)` 的旧调用点（polygon/textv3 等）改完信号后需补 `add_gizmo()`。
    `DraggableGizmo` 的 handle_start/handle_movement 信号改为直调
    `parent_node()->gizmo_drag_start()/gizmo_drag_move()`。各 gizmo 的 `draw(QPainter*)`、
    `PointGizmo::get_clicking_rect()/get_drawing_rect()/get_standard_radius()`
    （依赖 `QFontMetrics(qApp->font())`）删除，归 app 层（facade 的 hit-test 需在 app 侧重实现）。
    `TextGizmo` 的 4 个信号（activated/deactivated/rect_changed/vertical_alignment_changed）删除，
    由 oakengine 事件机制承担。
11. project 波次（project/folder/footage/sequence/serializer）：
    - `Project` 不再继承 QObject：`childEvent` 的 ChildAdded/ChildRemoved 分支变
      `Project::add_node(node)` / `remove_node(node)`（remove 摘出不删除）；节点所有权归
      Project（`clear()`/析构删除）。`ColorManager` 由 `std::unique_ptr` 持有（原 QObject 父子）。
    - `Project`/`Folder`/`Sequence`/`Footage` 的信号整组删除（含 Folder 的
      begin/end_insert/remove_item、Sequence 的 track_added/removed/subtitles_changed、
      Footage 的 proxy_settings_changed、Project 的 name_changed/modified_changed/
      setting_changed/node_added/node_removed 等），通知由 facade 层承担。
    - `Sequence::update_track_cache()` 变 public 成员；TrackList 波次须在原
      track_list_changed/length_changed 发射点直调 `sequence->update_track_cache()` /
      `verify_length()`。`Footage::check_footage()/default_color_space_changed()/
      proxy_ready()/proxy_finished()` 同理保留为 public 待 facade/ProxyManager 波次接线。
    - 序列化 XML 元素/属性名与读写顺序逐字节保持不变；`XmlStreamWriter` 不再输出
      XML 声明与自动缩进（紧凑 XML），新旧 reader 均兼容。OVEC 压缩段为
      qCompress 兼容格式（zlib + 4 字节大端长度）。
    - `Footage::generate_frame()` 的离线媒体警示帧：QImage/QPainter 光栅化改为纯像素
      循环（深红底+斜纹），文字叠层（"Media Offline"）与抗锯齿丢失（被迫行为差异）。
    - timeline 边界（M4）：`TimelineMarker`/`TimelineWorkArea`/`TimelineMarkerList` 调用按
      去 Qt 形态书写（std::string、XmlStreamReader/Writer），签名对齐由 M4 完成。
