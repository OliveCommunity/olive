# RenderManager 多线程视频渲染问题分析

## 概述

本文档分析了 `app/render/rendermanager.h/cpp` 中多线程视频渲染系统的设计和实现问题。该系统实现了一个自定义的工作窃取（Work Stealing）线程池，用于分配视频/音频渲染任务。以下按严重程度从高到低列出发现的问题。

---

## 🔴 严重问题（Critical）

### 1. 所有渲染线程的 GPU 上下文为 nullptr —— 无法实际渲染

**文件**: `rendermanager.cpp`

**问题**:
`RenderManager` 构造函数中创建了一个 `OpenGLRenderer` 实例存储在 `context_` 成员变量中，但调用 `CreateThread()` 时没有传递任何参数：

```cpp
RenderManager::RenderManager(QObject *parent) {
    context_ = new OpenGLRenderer();       // 创建了 GPU 上下文
    // ...
    for (int i = 0; i < video_thread_count; i++) {
        video_threads_.append(
            std::shared_ptr<RenderThread>(CreateThread()));  // 没传 renderer！
    }
    dry_run_thread_ = CreateThread();  // 没传 renderer！
    audio_thread_ = CreateThread();    // 没传 renderer！
}
```

`CreateThread()` 的默认参数是 `nullptr`：
```cpp
RenderThread *RenderManager::CreateThread(Renderer *renderer = nullptr);
```

导致每个 `RenderThread` 的 `context_` 都是 `nullptr`。在 `RenderProcessor::Run()` 中：
```cpp
case RenderManager::kTypeVideo: {
    TexturePtr texture = GenerateTexture(time, frame_length);
    if (!render_ctx_) {
        ticket->Finish();  // 无渲染上下文，直接结束，没有返回任何帧！
    }
    // ...
}
```

**影响**: 所有视频渲染任务都会因为没有 GPU 上下文而静默失败，`ticket->Finish()` 不带结果返回。测试中使用 `kNull` 返回类型所以没有暴露问题，但在实际渲染视频帧时不会产出任何画面。

**根本原因分析**: 这个设计可能源自一个架构问题——OpenGL 上下文只能绑定到一个线程，不可能让多个线程共享同一个 OpenGL 上下文并发渲染。在原始 Olive 代码中，每个渲染线程可能有自己独立的 OpenGL 上下文（通过共享上下文 share context），但当前实现遗漏了这一点。

**修复建议**:
- 为每个视频渲染线程创建独立的 `OpenGLRenderer`（使用 OpenGL shared context 机制共享纹理/着色器）。
- 或者改为单线程渲染 + 多线程解码/预处理的架构。

---

### 2. `run()` 方法中存在死锁（Deadlock）

**文件**: `rendermanager.cpp`，`RenderThread::run()`

**问题**: 当工作窃取失败后的双重检查路径中，存在对已锁定互斥锁的二次加锁，导致死锁：

```cpp
void RenderThread::run() {
    QMutexLocker locker(&mutex_);

    while (!cancelled_) {
        RenderTicketPtr ticket;
        bool have_task = false;

        // 1. 尝试从自己的队列取任务
        if (!queue_.empty()) {
            ticket = queue_.back();
            queue_.pop_back();
            have_task = true;
        }

        if (!have_task) {
            // 2. 解锁
            locker.unlock();

            // 3. 尝试窃取
            ticket = StealFromOthers();

            if (ticket) {
                have_task = true;
            } else {
                // 4. 窃取失败，重新加锁
                locker.relock();        // ← mutex_ 已加锁

                // 5. 双重检查
                if (!queue_.empty()) {
                    ticket = queue_.back();
                    queue_.pop_back();
                    have_task = true;    // ← have_task = true, 但 mutex_ 已经处于加锁状态
                } else if (!cancelled_) {
                    wait_.wait(&mutex_);
                    continue;
                }
            }

            if (have_task) {
                locker.relock();  // ← 💥 BUG: 如果从第5步的分支进来，mutex_ 已锁定，这里再次 relock 会死锁！
            }
        }
        // ...
    }
}
```

**触发条件**:
1. 自己的队列为空 → 进入 `if (!have_task)` 分支
2. 解锁 → 尝试窃取 → 窃取失败
3. 重新加锁 → 双重检查发现队列不为空 → `have_task = true`
4. 落入 `if (have_task) { locker.relock(); }` → **对已锁定的 QMutex 再次加锁 → 死锁**

`QMutex` 默认是非递归的，对已锁定的互斥锁再次加锁会永久阻塞。

**修复建议**:
```cpp
if (!have_task) {
    locker.unlock();
    ticket = StealFromOthers();
    if (ticket) {
        have_task = true;
        locker.relock();  // 窃取成功时重新加锁
    } else {
        locker.relock();
        if (!queue_.empty()) {
            ticket = queue_.back();
            queue_.pop_back();
            have_task = true;
            // 此时 mutex_ 已经被 relock, 不需要再次加锁
        } else if (!cancelled_) {
            wait_.wait(&mutex_);
            continue;
        }
    }
    // 移除外层的 if (have_task) { locker.relock(); }
}
```

---

### 3. `StealFromOthers()` 访问全局线程列表时无锁保护

**文件**: `rendermanager.cpp`，`RenderThread::StealFromOthers()`

**问题**:
```cpp
RenderTicketPtr RenderThread::StealFromOthers() {
    // 直接读取 s_all_threads_，没有持有 s_all_threads_mutex_！
    if (s_all_threads_.size() <= 1) {
        return nullptr;
    }

    size_t num_threads = s_all_threads_.size();  // 无锁读取
    // ...
    for (size_t i = 0; i < num_threads; ++i) {
        size_t idx = (start_idx + i) % num_threads;
        RenderThread *other = s_all_threads_[idx];  // 无锁访问 vector 元素
        // ...
    }
}
```

而 `RegisterThread()` 和 `UnregisterThread()` 会在持有 `s_all_threads_mutex_` 的情况下修改 `s_all_threads_`：
```cpp
void RenderThread::RegisterThread() {
    QMutexLocker locker(&s_all_threads_mutex_);
    s_all_threads_.push_back(this);  // 可能导致 vector 重新分配
}
```

**影响**: 如果一个线程正在 `StealFromOthers()` 中遍历 `s_all_threads_`，而另一个线程正在注册/注销（`push_back` 或 `erase` 可能导致 vector 重新分配），则会发生 **数据竞争（Data Race）**，可能导致访问已释放的内存或无效迭代器。

**修复建议**: 在 `StealFromOthers()` 开头获取 `s_all_threads_mutex_` 的锁，或者复制一份线程列表再遍历。

---

## 🟠 设计问题（Design Issues）

### 4. `SelectBestThread()` 使用轮询而非实际负载均衡

**文件**: `rendermanager.cpp`

```cpp
RenderThread *RenderManager::SelectBestThread() {
    static std::atomic<size_t> next_thread_index{ 0 };
    size_t index = next_thread_index++ % video_threads_.size();
    return video_threads_[index].get();
}
```

- 使用简单的轮询（Round-Robin），完全忽略了各线程的实际队列负载。
- 已经实现了 `GetLightestThreadIndex()` 可以查询各线程队列大小，但从未被调用。
- 工作窃取机制虽然可以弥补一些负载不均，但如果能在分配时就考虑负载，效率会更高。

**建议**: 使用 `GetLightestThreadIndex()` 作为分配策略，或者至少在队列长度差异较大时优先分配给空闲线程。

---

### 5. `WorkStealingQueue` 接口是死代码

**文件**: `rendermanager.h`

```cpp
class WorkStealingQueue {
public:
    virtual ~WorkStealingQueue() = default;
    virtual bool TryPopBack(RenderTicketPtr &ticket) = 0;
    virtual bool TrySteal(RenderTicketPtr &ticket) = 0;
    virtual void PushBack(RenderTicketPtr ticket) = 0;
    virtual size_t Size() const = 0;
    virtual bool Empty() const = 0;
};
```

这个抽象接口被声明了但从未被任何类实现或使用。`RenderThread` 直接操作 `std::deque` 而不是通过这个接口。

**建议**: 要么让 `RenderThread` 实现此接口，要么移除这段死代码。

---

### 6. 内存管理混乱 —— 原始指针与 shared_ptr 混用

**文件**: `rendermanager.h/cpp`

```cpp
QList<RenderThreadPtr> video_threads_;        // shared_ptr 管理
RenderThread *dry_run_thread_;                // 原始指针
RenderThread *audio_thread_;                  // 原始指针
std::vector<RenderThread *> waveform_threads_; // 原始指针
std::list<RenderThread *> render_threads_;     // 原始指针（所有线程都在这里）
```

- `video_threads_` 使用 `shared_ptr`，但 `render_threads_` 保存原始指针指向同一对象。
- 析构函数遍历 `render_threads_` 来 quit/wait，但没有 `delete`，因为 `video_threads_` 的 shared_ptr 会处理释放。
- 但 `dry_run_thread_`、`audio_thread_`、`waveform_threads_` 中的指针在析构时只是被 quit/wait（通过 `render_threads_`），依赖 `new RenderThread(...)` 后这些对象作为 QObject 子对象被父对象 `this` 自动删除。
- 然而 `video_threads_` 中的 shared_ptr 持有同一指针，如果 shared_ptr 先析构并 delete 对象，然后 QObject 父对象再尝试 delete，就会 **double-free**。

**建议**: 统一使用一种所有权模型。建议所有线程都用 `std::unique_ptr` 或统一原始指针 + QObject 父子关系。

---

### 7. 工作窃取的 TrySteal 从队列头部窃取，与 LIFO 取任务冲突

**文件**: `rendermanager.cpp`

```cpp
// 拥有者线程：从尾部取（LIFO，更好的缓存局部性）
ticket = queue_.back();
queue_.pop_back();

// 窃取者线程：从头部窃取
bool RenderThread::TrySteal(RenderTicketPtr &ticket) {
    if (!queue_.empty()) {
        ticket = queue_.front();
        queue_.pop_front();
        return true;
    }
    return false;
}
```

这是标准的工作窃取策略（Chase-Lev deque 的简化版），从两端操作减少竞争。但问题在于：

- 拥有者和窃取者都需要获取同一个 `mutex_`，所以双端操作并不能真正减少锁竞争。
- 真正的无锁工作窃取队列（如 Chase-Lev deque）才能从两端无锁操作中受益。

**建议**: 如果需要高性能工作窃取，考虑实现无锁的工作窃取队列。当前的互斥锁方案下，从头部或尾部窃取没有性能差异。

---

## 🟡 次要问题（Minor Issues）

### 8. RenderTicket::Start() 无条件清空观察者列表

**文件**: `renderticket.cpp`

```cpp
void RenderTicket::Start() {
    QMutexLocker locker(&lock_);
    is_running_ = true;
    has_result_ = false;
    result_.clear();
    watchers_.clear();  // 清空观察者
}
```

如果一个 ticket 在 `Start()` 之前已经有观察者注册（例如通过 `SetTicket` 然后 ticket 被重新提交），这些观察者将永远不会被通知。

---

### 9. RenderTicket 属性的高频锁竞争

`RenderTicket::property()` 和 `setProperty()` 每次调用都获取 `lock_`。在 `RenderProcessor::Run()` 中，单次渲染任务会调用数十次 `ticket_->property()`，每次都加锁/解锁。如果其他线程同时访问同一 ticket 的属性（如设置 `multicam_output`），会产生不必要的锁竞争。

**建议**: 考虑在 `RenderProcessor` 开始处理时一次性读取所有需要的属性到本地变量，减少后续锁操作。

---

### 10. `steal_start_index_` 非原子操作但可能被多线程访问

**文件**: `rendermanager.h/cpp`

```cpp
size_t steal_start_index_ = 0;  // 非 atomic

// 在 StealFromOthers() 中：
size_t start_idx = steal_start_index_++ % num_threads;  // 非原子自增
```

虽然 `StealFromOthers()` 通常只由拥有者线程调用，但 `steal_start_index_` 不受任何锁保护，如果未来代码变更导致多线程访问，就会有数据竞争。

---

## 🟢 架构建议

### 11. OpenGL 多线程渲染的正确架构

OpenGL/GPU 渲染天然不适合多线程并发。推荐的架构模式：

**方案 A: 单 GPU 线程 + 多解码线程**
- 一个专用线程持有 OpenGL 上下文，负责所有 GPU 操作（纹理上传、着色器执行、帧下载）
- 多个工作线程负责 CPU 密集的工作（视频解码、图形遍历、缓存查找）
- 通过任务队列将 GPU 工作发送到 GPU 线程

**方案 B: 每线程一个共享上下文**
- 创建主 OpenGL 上下文后，为每个渲染线程创建共享上下文（`QOpenGLContext::setShareContext()`）
- 共享上下文可以访问相同的纹理和着色器，但每个上下文只能在一个线程中使用
- 这是 Olive 原始设计可能想实现的方案

**方案 C: Vulkan/Compute Shader 方案（长期）**
- 使用 Vulkan 或 OpenCL 替代 OpenGL，它们的多线程支持更好
- Vulkan 支持多线程命令缓冲区录制

---

## 总结

| 严重度 | 问题 | 状态 |
|--------|------|------|
| 🔴 Critical | 所有渲染线程 GPU 上下文为 nullptr | 待修复 |
| 🔴 Critical | run() 中存在死锁 | 待修复 |
| 🔴 Critical | StealFromOthers() 无锁访问共享数据 | 待修复 |
| 🟠 Design | SelectBestThread 未使用实际负载信息 | 待优化 |
| 🟠 Design | WorkStealingQueue 接口未使用 | 待清理 |
| 🟠 Design | 内存管理混乱（raw ptr vs shared_ptr） | 待重构 |
| 🟠 Design | 有锁工作窃取队列无法充分发挥性能 | 待优化 |
| 🟡 Minor | Start() 清空观察者列表 | 待修复 |
| 🟡 Minor | 属性高频锁竞争 | 待优化 |
| 🟡 Minor | steal_start_index_ 非原子操作 | 待修复 |
