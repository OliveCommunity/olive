# 01 · 双层适配器模式（所有模块共用规范）

> 本规范是拆分能在"只拆不写"约束下成立的核心机制。任何模块的
> C API 设计与适配类实现都必须照此执行。命名、内存所有权、错误码、
> 线程与信号的处理在此**冻结**。

## 1. 提供侧：C API 层

对被消费的每个 C++ 类 `Clazz`，模块在 `include/oak<mod>/clazz.h`
里暴露一组 `extern "C"` 函数：

```c
/* 构造/析构 */
OAKMOD_API OakModClazz *oakmod_clazz_init(/* 与某个构造函数对应的参数 */);
OAKMOD_API void       oakmod_clazz_free(OakModClazz *self);

/* 普通成员函数：self 为第一参数，其余参数按 §3 POD 化 */
OAKMOD_API <ret>    oakmod_clazz_<func>(OakModClazz *self, ...);

/* 静态成员函数：无 self */
OAKMOD_API <ret>    oakmod_clazz_<func>_s(/* 参数 */);
```

规则：

1. `OakModClazz` 是**不透明类型**（`typedef struct OakModClazz OakModClazz;`），
   提供侧实现里它就是 `olive::Clazz*`（`reinterpret_cast`），消费侧
   永远无法解引用。
2. `init` 每个对应一个实际在用的构造函数重载；返回 NULL 表示失败。
   `free` 对 NULL 是 no-op。**所有权：init 创建的对象归调用方，必须
   配对 free**；借用指针（不转移所有权）在文档注释里写 `/* borrowed */`。
3. 多个构造重载用后缀区分：`oakmod_clazz_init`（默认）、
   `oakmod_clazz_init_from_file`、`oakmod_clazz_init_copy` 等。
4. 命名全小写，模块前缀 `oak<mod>_`（oakundo/oaknode/oaktimeline/
   oakcodec/oakrender/oaktask/oakaudio/oakplugin/oakcommon）。
5. 导出宏 `OAKMOD_API` 照 `oakengine/export.h` 样式
   （`__attribute__((visibility("default")))`），模块编译加
   `-fvisibility=hidden`——每个模块**出生即 visibility 干净**，
   R7-B 的全局收口变成顺水推舟。

## 2. 消费侧：同名 C++ 适配类

消费模块里放一个与原始类**同名**的适配类（放在消费侧私有头
`<mod>/adapter/clazz.h`，namespace 保持 `olive`）：

```cpp
namespace olive {

class Clazz {
public:
	Clazz(/* 原构造签名 */)
		: h_(oakmod_clazz_init(/* 相同参数 */)) {}
	~Clazz() { oakmod_clazz_free(h_); }

	// 原成员函数签名原样保留，体内转发一行
	Ret func(Arg a) { return oakmod_clazz_func(h_, a); }

	// 句柄逃生口（确实需要混用时的过渡手段，注释标注）
	OakModClazz *handle() const { return h_; }

private:
	OakModClazz *h_;
	// 拷贝语义与原类一致：原类不可拷贝就 = delete；
	// 原类可拷贝则 init_copy。
};

}
```

效果：消费模块里所有 `clazz.func(a)`、`new Clazz(...)`、
`clazz->func(a)` 调用点**零改动**——原来 include 提供侧 C++ 头的
地方，改 include 本模块的适配头即可。这就是"双层适配器"：
**C++ 调用点 → 同名适配类 → C ABI → 提供侧 C++ 实现**。

注意：

- 适配类是**逐模块私有的**：oakrender 消费的 Node 适配类和
  oaktimeline 消费的 Node 适配类是两份独立的小头文件，各自只有
  自己用到的方法子集。允许重复，禁止共享（共享就重新耦合了）。
- 适配类只转发**本模块实际用到**的方法（按 02 的依赖矩阵逐个
  grep 确认清单，写在模块手册里）。
- 原类有继承体系的（如 UndoCommand 子类族），见 §5。

## 3. 参数与返回值的 POD 化

C ABI 上只允许：整数、`double`、`int64_t`、指针、`const char *`、
以及 `include/oak<mod>/types.h` 里定义的纯 C POD（无构造/析构/
方法）。Qt/C++ 类型按下表映射：

| C++ 类型 | C ABI 形态 |
|---|---|
| `QString` | `const char *`（入，UTF-8）；出：buf/size 两段式（先 NULL 查长度） |
| `olive::core::Rational` | 两个 `int64_t`（num, den）；时间戳用 `int64_t` 帧戳 |
| `TimeRange` | 两个 `int64_t`（in_ts, out_ts） |
| `QVariant`/`NodeValue` | `oak_node_value` POD（oakengine/node.h 已有，各模块复用该定义或复制同构 POD） |
| `VideoParams`/`AudioParams` | `oak_video_params`/POD 字段拍平 |
| `QList<T>`/`QVector<T>` | 指针 + count；返回集合用 `count` + `at(i)` 访问器对 |
| `Color` | 4 × double |
| `Qt::enum`/内部枚举 | `int`（取值表写进手册，两侧枚举**序数一致性**用 static_assert 或测试钉死） |
| `std::shared_ptr<T>` | 不透明句柄 + retain/free（协议见 display.h R7-A 的先例） |

## 4. 信号、回调与线程

Qt 信号不许跨模块。处理优先级：

1. **回调注册**：`oakmod_clazz_set_<event>_cb(self, fn, userdata)`，
   提供侧在发信号处调 `fn(event_pod, userdata)`。userdata 所有权归
   注册方，适配类析构时先 `set_*_cb(self, NULL, NULL)` 反注册。
2. **事件总线**：模块级通知（非单对象）用
   `oakmod_subscribe(event_id, fn, userdata)` → 返回订阅 id，
   `oakmod_unsubscribe(id)`——照 `oakengine/events.h` 的现成模式。
3. 线程语义照现状：提供侧在发射线程同步调回调（DirectConnection
   等价），需要跨线程排队是消费侧适配类自己的事（它可以用
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`）。
4. **适配类可以把 C 回调再转回 Qt 信号**：适配类继承 QObject、
   静态 trampoline 里 `emit` 同名信号——消费侧原有 `connect()` 全部
   零改动。这是大多数 widget 侧适配的默认做法。

## 5. 继承与虚函数

原类的继承体系**不出模块**。跨界时：

- 基类引用（如 `UndoCommand *`）→ 基类句柄（`OakUndoCommand *`）。
- 消费侧不构造具体子类，只经提供侧的工厂函数：
  `oakmod_clazz_create_<variant>(...)`（返回基类句柄）。
- 消费侧确实需要子类行为的（极少数，需逐个人工论证），在提供侧
  加专用函数，不放虚函数跨界。

## 6. 错误码

沿用 facade 约定：`OAKMOD_OK=0`，负值错误
（`OAKMOD_E_INVALID/E_STATE/E_NOT_FOUND/E_FAILED`），在各模块
`types.h` 里统一定义（值与 oakengine 的现有值对齐）。会失败的
`init` 返回 NULL，错误细节经 `oakmod_last_error(buf, size)` 取
（线程局部，照 facade 的 `set_error` 模式）。

## 7. 一个完整例子（UndoStack，M2 照此落地）

```c
/* oakundo/include/oakundo/undostack.h */
typedef struct OakUndoStack OakUndoStack;
OAKUNDO_API OakUndoStack *oakundo_undostack_init(void *parent_qobject);
OAKUNDO_API void oakundo_undostack_free(OakUndoStack *self);
OAKUNDO_API void oakundo_undostack_push(OakUndoStack *self,
	OakUndoCommand *cmd, const char *name);
OAKUNDO_API int  oakundo_undostack_can_undo(const OakUndoStack *self);
/* ... 完整表见 M2 手册 ... */
OAKUNDO_API int64_t oakundo_undostack_subscribe(OakUndoStack *self,
	int event_id, oakundo_event_fn fn, void *userdata);
OAKUNDO_API void oakundo_unsubscribe(int64_t id);
```

```cpp
// 消费侧 adapter/undostack.h —— app/other 模块里 connect() 零改动
class UndoStack : public QObject {
	Q_OBJECT
public:
	explicit UndoStack(QObject *p = nullptr)
		: QObject(p), h_(oakundo_undostack_init(p)) {
		sub_ = oakundo_undostack_subscribe(h_, OAKUNDO_EVENT_INDEX_CHANGED,
			&UndoStack::tramp, this);
	}
	~UndoStack() override { oakundo_unsubscribe(sub_); oakundo_undostack_free(h_); }
	void push(UndoCommand *c, const QString &n) {
		oakundo_undostack_push(h_, c->handle(), n.toUtf8().constData());
	}
	bool canUndo() const { return oakundo_undostack_can_undo(h_) != 0; }
signals:
	void index_changed(int);
private:
	static void tramp(int event_id, int64_t a, int64_t b, void *ud) {
		if (event_id == OAKUNDO_EVENT_INDEX_CHANGED)
			emit static_cast<UndoStack *>(ud)->index_changed(int(a));
	}
	OakUndoStack *h_;
	int64_t sub_;
};
```
