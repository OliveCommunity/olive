# OpenGL VAO/VBO 复用优化计划

## 问题描述

在当前的 `OpenGLRenderer::Blit()` 实现中，**每次渲染调用都会重复创建和销毁VAO（顶点数组对象）和VBO（顶点缓冲对象）**。这是一个明显的性能瓶颈，因为：

1. Blit操作在视频渲染中非常频繁（每帧可能调用数十次）
2. 顶点数据实际上是固定的（6个顶点，2个三角形组成的四边形）
3. 频繁的GPU资源创建/销毁会产生大量驱动开销

### 问题代码位置

**文件**: `app/render/opengl/openglrenderer.cpp`  
**函数**: `OpenGLRenderer::Blit()` (第592-736行)

```cpp
// 每次Blit都创建新的VAO/VBO
QOpenGLVertexArrayObject vao_;
vao_.create();
vao_.bind();

QOpenGLBuffer vert_vbo_;
vert_vbo_.create();
vert_vbo_.bind();
vert_vbo_.allocate(blit_vertices.constData(),  // 上传相同的顶点数据
                   blit_vertices.size() * sizeof(GLfloat));

QOpenGLBuffer frag_vbo_;
frag_vbo_.create();
frag_vbo_.bind();
frag_vbo_.allocate(blit_texcoords.constData(),  // 上传相同的纹理坐标数据
                   blit_texcoords.size() * sizeof(GLfloat));

// ... 渲染 ...

// 每次Blit结束都销毁
frag_vbo_.destroy();
vert_vbo_.destroy();
vao_.destroy();
```

---

## 优化方案

### 核心思想

由于Blit使用的几何数据是**完全静态**的：
- 顶点坐标：固定的6个顶点（2个三角形）
- 纹理坐标：固定的6个坐标

可以将VAO/VBO缓存到 `OpenGLRenderer` 类中，在初始化时创建一次，渲染时重复使用。

---

## 实施步骤

### 步骤1：在头文件中添加缓存成员

**文件**: `app/render/opengl/openglrenderer.h`

在 `private:` 区域添加（约第137行后）：

```cpp
// 缓存的VAO/VBO结构体
struct CachedGeometry {
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vert_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer tex_vbo{QOpenGLBuffer::VertexBuffer};
    bool initialized{false};
    
    void cleanup() {
        if (initialized) {
            vao.destroy();
            vert_vbo.destroy();
            tex_vbo.destroy();
            initialized = false;
        }
    }
};
CachedGeometry cached_geometry_;
```

在同一区域添加方法声明：

```cpp
void EnsureGeometryCached();  // 确保VAO/VBO已创建并初始化
```

---

### 步骤2：实现缓存初始化方法

**文件**: `app/render/opengl/openglrenderer.cpp`

在文件顶部（约第70行后）添加新方法：

```cpp
void OpenGLRenderer::EnsureGeometryCached()
{
    // 如果已经初始化，直接返回
    if (cached_geometry_.initialized) {
        return;
    }

    // 创建VAO
    cached_geometry_.vao.create();
    cached_geometry_.vao.bind();

    // 创建并填充顶点VBO（静态数据）
    cached_geometry_.vert_vbo.create();
    cached_geometry_.vert_vbo.bind();
    cached_geometry_.vert_vbo.allocate(
        blit_vertices.constData(),
        blit_vertices.size() * sizeof(GLfloat)
    );
    cached_geometry_.vert_vbo.release();

    // 创建并填充纹理坐标VBO（静态数据）
    cached_geometry_.tex_vbo.create();
    cached_geometry_.tex_vbo.bind();
    cached_geometry_.tex_vbo.allocate(
        blit_texcoords.constData(),
        blit_texcoords.size() * sizeof(GLfloat)
    );
    cached_geometry_.tex_vbo.release();

    cached_geometry_.vao.release();
    cached_geometry_.initialized = true;
}
```

---

### 步骤3：修改Blit函数 - 使用缓存的VAO/VBO

**文件**: `app/render/opengl/openglrenderer.cpp`  
**函数**: `OpenGLRenderer::Blit()`

替换第592-636行的代码：

```cpp
// === 原代码（删除）===
// QOpenGLVertexArrayObject vao_;
// vao_.create();
// vao_.bind();
// QOpenGLBuffer vert_vbo_;
// vert_vbo_.create();
// ...

// === 新代码 ===
// 确保缓存的VAO/VBO已创建
EnsureGeometryCached();

// 绑定缓存的VAO
cached_geometry_.vao.bind();

// 设置顶点属性指针
GLint vertex_location = functions_->glGetAttribLocation(shader, "a_position");
if (vertex_location != -1) {
    cached_geometry_.vert_vbo.bind();
    functions_->glEnableVertexAttribArray(vertex_location);
    functions_->glVertexAttribPointer(
        vertex_location, 3, GL_FLOAT, GL_FALSE, 0, nullptr
    );
    cached_geometry_.vert_vbo.release();
}

GLint tex_location = functions_->glGetAttribLocation(shader, "a_texcoord");
if (tex_location != -1) {
    cached_geometry_.tex_vbo.bind();
    functions_->glEnableVertexAttribArray(tex_location);
    functions_->glVertexAttribPointer(
        tex_location, 2, GL_FLOAT, GL_FALSE, 0, nullptr
    );
    cached_geometry_.tex_vbo.release();
}
```

---

### 步骤4：修改Blit函数 - 移除销毁逻辑

在 `Blit()` 函数末尾（原第733-736行），替换：

```cpp
// === 原代码（删除）===
// frag_vbo_.destroy();
// vert_vbo_.destroy();
// vao_.release();
// vao_.destroy();

// === 新代码 ===
// 只需解绑VAO，不需要销毁
cached_geometry_.vao.release();
```

---

### 步骤5：在销毁时清理缓存

**文件**: `app/render/opengl/openglrenderer.cpp`  
**函数**: `DestroyInternal()`

在函数末尾（第168行前）添加：

```cpp
// 清理缓存的VAO/VBO
cached_geometry_.cleanup();
```

---

### 步骤6：处理动态顶点坐标（可选增强）

当前代码支持通过 `job.GetVertexCoordinates()` 传入自定义顶点坐标。如果该功能被使用，需要特殊处理：

**方案A**（推荐）：保持动态VBO的创建（仅在需要时）

```cpp
// 在Blit函数中
QOpenGLBuffer* dynamic_vert_vbo = nullptr;

if (!job.GetVertexCoordinates().isEmpty()) {
    // 有自定义顶点坐标，创建临时VBO
    dynamic_vert_vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    dynamic_vert_vbo->create();
    dynamic_vert_vbo->bind();
    dynamic_vert_vbo->allocate(
        job.GetVertexCoordinates().constData(),
        job.GetVertexCoordinates().size() * sizeof(float)
    );
    dynamic_vert_vbo->release();
}

// ... 渲染时使用 dynamic_vert_vbo 或 cached_geometry_.vert_vbo ...

// 清理临时VBO
if (dynamic_vert_vbo) {
    dynamic_vert_vbo->destroy();
    delete dynamic_vert_vbo;
}
```

**方案B**（进一步优化）：使用Uniform传递变换矩阵

如果动态顶点只是对标准四边形的变换（缩放、旋转、位移），可以通过Uniform传递变换矩阵，避免修改顶点数据。

---

## 性能收益预估

| 指标 | 优化前 | 优化后 | 提升 |
|------|--------|--------|------|
| 每帧Blit的GL API调用 | ~20次 (create/bind/allocate/release/destroy) | ~4次 (bind/release) | **~80%减少** |
| GPU驱动开销 | 高（频繁资源创建/销毁） | 低（资源复用） | 显著提升 |
| 内存分配 | 每帧堆分配 | 一次性初始化 | 消除GC压力 |
| 顶点数据上传 | 每帧上传相同数据 | 仅初始化时上传 | 带宽节省 |

### 实际场景估算

假设一个1080p视频渲染：
- 每帧约50个节点需要Blit
- 每秒30帧
- 优化前：每秒创建/销毁 1500个VAO + 3000个VBO
- 优化后：VAO/VBO创建次数降为0（运行时）

---

## 注意事项

### 1. 线程安全

`Blit()` 只在OpenGL线程中调用，无需额外的线程同步。但需确保：
- `EnsureGeometryCached()` 在正确的OpenGL上下文中调用
- 缓存的VAO/VBO与OpenGL上下文生命周期一致

### 2. 上下文生命周期

如果OpenGL上下文被重建（如窗口切换、GPU重置）：
- 需要在 `DestroyInternal()` 中正确调用 `cached_geometry_.cleanup()`
- 下次 `EnsureGeometryCached()` 会自动重新创建

### 3. 验证

建议添加以下调试验计数器：

```cpp
// 在 CachedGeometry 中添加
struct CachedGeometry {
    // ... 原有成员 ...
    #ifdef DEBUG
    int cache_hits{0};      // 命中次数
    int cache_misses{0};    // 创建次数
    #endif
};

void EnsureGeometryCached() {
    if (cached_geometry_.initialized) {
        #ifdef DEBUG
        cached_geometry_.cache_hits++;
        #endif
        return;
    }
    #ifdef DEBUG
    cached_geometry_.cache_misses++;
    #endif
    // ... 创建逻辑 ...
}
```

---

## 代码变更汇总

### 新增文件内容

1. **openglrenderer.h**: 添加 `CachedGeometry` 结构和 `EnsureGeometryCached()` 声明
2. **openglrenderer.cpp**: 
   - 添加 `EnsureGeometryCached()` 实现
   - 修改 `Blit()` 使用缓存
   - 修改 `DestroyInternal()` 清理缓存

### 修改行数预估

- 新增代码：约60行
- 删除代码：约45行
- 净增代码：约15行

---

## 相关文档

- [渲染线程架构分析](render-threading-analysis.md)
- [多线程渲染实现计划](../plan-zh.md)

---

*最后更新: 2026-02-23*
