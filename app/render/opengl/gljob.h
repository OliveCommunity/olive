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

#ifndef GLJOB_H
#define GLJOB_H

#include <QMutex>
#include <QSemaphore>
#include <QWaitCondition>
#include <functional>
#include <memory>
#include <queue>

#include "render/renderer.h"
#include "render/renderticket.h"

namespace olive
{

// 前置声明
class OpenGLRenderer;

// GL 任务基类
class GLJob {
public:
    enum Type {
        kCreateTexture,
        kDestroyTexture,
        kUploadTexture,
        kDownloadTexture,
        kBlitShader,
        kBlitColorManaged,
        kClearDestination,
        kCreateShader,
        kDestroyShader,
        kFlush,
        kGetPixel,
        kInterlaceTexture,
        kProcessShader,
        kProcessColorTransform,
        kProcessFrameGeneration,
        kProcessVideoCache,
        kCustom
    };

    GLJob(Type type) : type_(type), cancelled_(false), completed_(false) {}
    virtual ~GLJob() = default;

    Type type() const { return type_; }

    // 执行任务（在 OpenGL 线程中调用）
    virtual void Execute(OpenGLRenderer *renderer) = 0;

    // 等待任务完成（使用 QSemaphore 避免条件变量丢失信号）
    void WaitForCompletion() {
        semaphore_.acquire();
    }

    // 等待任务完成（带超时），返回true表示成功完成，false表示超时
    bool WaitForCompletionWithTimeout(int timeout_ms) {
        return semaphore_.tryAcquire(1, timeout_ms);
    }

    // 标记任务完成
    void MarkCompleted() {
        completed_ = true;
        semaphore_.release();
    }

    bool IsCancelled() const { return cancelled_; }
    void Cancel() { cancelled_ = true; }

protected:
    Type type_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> completed_;
    QSemaphore semaphore_{0};  // 初始为0，完成后release
};

typedef std::shared_ptr<GLJob> GLJobPtr;

// 创建纹理任务
class GLCreateTextureJob : public GLJob {
public:
    GLCreateTextureJob(const VideoParams &params, const void *data = nullptr, int linesize = 0)
        : GLJob(kCreateTexture), params_(params), data_(data), linesize_(linesize) {}

    void Execute(OpenGLRenderer *renderer) override;

    TexturePtr GetResult() const { return result_; }

private:
    VideoParams params_;
    const void *data_;
    int linesize_;
    TexturePtr result_;
};

// 销毁纹理任务
class GLDestroyTextureJob : public GLJob {
public:
    explicit GLDestroyTextureJob(Texture *texture)
        : GLJob(kDestroyTexture), texture_(texture) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    Texture *texture_;
};

// 上传纹理数据任务
class GLUploadTextureJob : public GLJob {
public:
    GLUploadTextureJob(TexturePtr texture, const void *data, int linesize)
        : GLJob(kUploadTexture), texture_(texture), data_(const_cast<void*>(data)), linesize_(linesize) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    TexturePtr texture_;
    void *data_;
    int linesize_;
};

// 下载纹理数据任务
class GLDownloadTextureJob : public GLJob {
public:
    GLDownloadTextureJob(TexturePtr texture, void *data, int linesize)
        : GLJob(kDownloadTexture), texture_(texture), data_(data), linesize_(linesize) {}

    void Execute(OpenGLRenderer *renderer) override;
    bool success() const { return success_; }

private:
    TexturePtr texture_;
    void *data_;
    int linesize_;
    bool success_ = false;
};

// Shader Blit 任务
class GLBlitShaderJob : public GLJob {
public:
    GLBlitShaderJob(QVariant shader, AcceleratedJob *job, TexturePtr destination,
                    const VideoParams &dest_params, bool clear_destination)
        : GLJob(kBlitShader), shader_(shader), job_(job), destination_(destination),
          dest_params_(dest_params), clear_destination_(clear_destination) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    QVariant shader_;
    AcceleratedJob *job_;
    TexturePtr destination_;
    VideoParams dest_params_;
    bool clear_destination_;
};

// Color Managed Blit 任务
class GLBlitColorManagedJob : public GLJob {
public:
    GLBlitColorManagedJob(const ColorTransformJob &job, TexturePtr destination,
                          const VideoParams &params)
        : GLJob(kBlitColorManaged), job_(job), destination_(destination), params_(params) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    ColorTransformJob job_;
    TexturePtr destination_;
    VideoParams params_;
};

// 清屏任务
class GLClearDestinationJob : public GLJob {
public:
    GLClearDestinationJob(TexturePtr texture, double r, double g, double b, double a)
        : GLJob(kClearDestination), texture_(texture), r_(r), g_(g), b_(b), a_(a) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    TexturePtr texture_;
    double r_, g_, b_, a_;
};

// 创建 Shader 任务
class GLCreateShaderJob : public GLJob {
public:
    explicit GLCreateShaderJob(const ShaderCode &code)
        : GLJob(kCreateShader), code_(code) {}

    void Execute(OpenGLRenderer *renderer) override;

    QVariant GetResult() const { return result_; }

private:
    ShaderCode code_;
    QVariant result_;
};

// 销毁 Shader 任务
class GLDestroyShaderJob : public GLJob {
public:
    explicit GLDestroyShaderJob(QVariant shader)
        : GLJob(kDestroyShader), shader_(shader) {}

    void Execute(OpenGLRenderer *renderer) override;

private:
    QVariant shader_;
};

// Flush 任务
class GLFlushJob : public GLJob {
public:
    GLFlushJob() : GLJob(kFlush) {}

    void Execute(OpenGLRenderer *renderer) override;
};

// 获取像素任务
class GLGetPixelJob : public GLJob {
public:
    GLGetPixelJob(TexturePtr texture, const QPointF &pt)
        : GLJob(kGetPixel), texture_(texture), pt_(pt) {}

    void Execute(OpenGLRenderer *renderer) override;

    Color GetResult() const { return result_; }

private:
    TexturePtr texture_;
    QPointF pt_;
    Color result_;
};

// 交错纹理任务
class GLInterlaceTextureJob : public GLJob {
public:
    GLInterlaceTextureJob(TexturePtr top, TexturePtr bottom, const VideoParams &params)
        : GLJob(kInterlaceTexture), top_(top), bottom_(bottom), params_(params) {}

    void Execute(OpenGLRenderer *renderer) override;

    TexturePtr GetResult() const { return result_; }

private:
    TexturePtr top_;
    TexturePtr bottom_;
    VideoParams params_;
    TexturePtr result_;
};

// 通用 GL 执行任务（使用 lambda）
class GLCustomJob : public GLJob {
public:
    using Callback = std::function<void(OpenGLRenderer *)>;

    explicit GLCustomJob(Callback callback)
        : GLJob(kCustom), callback_(callback) {}

    void Execute(OpenGLRenderer *renderer) override {
        if (callback_) {
            callback_(renderer);
        }
    }

private:
    Callback callback_;
};

} // namespace olive

#endif // GLJOB_H
