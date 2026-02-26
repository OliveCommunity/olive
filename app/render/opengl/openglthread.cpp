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

#include "openglthread.h"

#include "openglrenderer.h"

namespace olive
{

thread_local bool OpenGLThread::s_is_gl_thread = false;

OpenGLThread::OpenGLThread(QObject *parent)
    : QThread(parent)
    , cancelled_(false)
    , renderer_(nullptr)
{
}

OpenGLThread::~OpenGLThread()
{
    Stop();
    wait();
}

void OpenGLThread::SubmitJob(GLJobPtr job)
{
    if (IsInGLThread()) {
        // 如果已经在 GL 线程，直接执行
        job->Execute(renderer_);
        job->MarkCompleted();
        return;
    }

    QMutexLocker locker(&mutex_);
    job_queue_.push_back(job);
    wait_condition_.wakeOne();
}

void OpenGLThread::SubmitJobAndWait(GLJobPtr job)
{
    if (IsInGLThread()) {
        // 如果已经在 GL 线程，直接执行
        job->Execute(renderer_);
        job->MarkCompleted();
        return;
    }

    SubmitJob(job);
    job->WaitForCompletion();
}

TexturePtr OpenGLThread::CreateTexture(const VideoParams &params, const void *data, int linesize)
{
    auto job = std::make_shared<GLCreateTextureJob>(params, data, linesize);
    SubmitJobAndWait(job);
    return job->GetResult();
}

void OpenGLThread::DestroyTexture(Texture *texture)
{
    if (!texture) return;
    auto job = std::make_shared<GLDestroyTextureJob>(texture);
    SubmitJob(job); // 异步执行
}

void OpenGLThread::UploadTexture(TexturePtr texture, const void *data, int linesize)
{
    if (!texture) return;
    auto job = std::make_shared<GLUploadTextureJob>(texture, data, linesize);
    SubmitJobAndWait(job);
}

bool OpenGLThread::DownloadTexture(TexturePtr texture, void *data, int linesize)
{
    if (!texture) return false;
    auto job = std::make_shared<GLDownloadTextureJob>(texture, data, linesize);
    SubmitJobAndWait(job);
    return job->success();
}

void OpenGLThread::BlitShader(QVariant shader, AcceleratedJob &job, TexturePtr destination,
                              const VideoParams &dest_params, bool clear_destination)
{
    auto gl_job = std::make_shared<GLBlitShaderJob>(shader, &job, destination, dest_params, clear_destination);
    SubmitJobAndWait(gl_job);
}

void OpenGLThread::BlitColorManaged(const ColorTransformJob &job, TexturePtr destination,
                                    const VideoParams &params)
{
    auto gl_job = std::make_shared<GLBlitColorManagedJob>(job, destination, params);
    SubmitJobAndWait(gl_job);
}

void OpenGLThread::ClearDestination(TexturePtr texture, double r, double g, double b, double a)
{
    auto job = std::make_shared<GLClearDestinationJob>(texture, r, g, b, a);
    SubmitJobAndWait(job);
}

QVariant OpenGLThread::CreateShader(const ShaderCode &code)
{
    auto job = std::make_shared<GLCreateShaderJob>(code);
    SubmitJobAndWait(job);
    return job->GetResult();
}

void OpenGLThread::DestroyShader(QVariant shader)
{
    auto job = std::make_shared<GLDestroyShaderJob>(shader);
    SubmitJob(job); // 异步执行
}

void OpenGLThread::Flush()
{
    auto job = std::make_shared<GLFlushJob>();
    SubmitJobAndWait(job);
}

Color OpenGLThread::GetPixel(TexturePtr texture, const QPointF &pt)
{
    auto job = std::make_shared<GLGetPixelJob>(texture, pt);
    SubmitJobAndWait(job);
    return job->GetResult();
}

TexturePtr OpenGLThread::InterlaceTexture(TexturePtr top, TexturePtr bottom, const VideoParams &params)
{
    auto job = std::make_shared<GLInterlaceTextureJob>(top, bottom, params);
    SubmitJobAndWait(job);
    return job->GetResult();
}

void OpenGLThread::Stop()
{
    QMutexLocker locker(&mutex_);
    cancelled_ = true;
    wait_condition_.wakeAll();
}

void OpenGLThread::WaitForIdle()
{
    if (IsInGLThread()) {
        // 如果在 GL 线程，处理所有当前队列中的任务
        QMutexLocker locker(&mutex_);
        while (!job_queue_.empty()) {
            GLJobPtr job = job_queue_.front();
            job_queue_.pop_front();
            locker.unlock();
            
            if (!job->IsCancelled()) {
                job->Execute(renderer_);
            }
            fprintf(stderr, "[DEADLOCK DEBUG] GL thread: calling MarkCompleted\n");
            fflush(stderr);
            job->MarkCompleted();
            fprintf(stderr, "[DEADLOCK DEBUG] GL thread: MarkCompleted done\n");
            fflush(stderr);
            
            locker.relock();
        }
        return;
    }

    // 在其他线程，等待队列变空
    bool idle = false;
    while (!idle) {
        QMutexLocker locker(&mutex_);
        idle = job_queue_.empty();
        if (!idle) {
            locker.unlock();
            QThread::msleep(1);
        }
    }
}

bool OpenGLThread::IsInGLThread() const
{
    return s_is_gl_thread && QThread::currentThread() == this;
}

void OpenGLThread::run()
{
    s_is_gl_thread = true;

    // 初始化 OpenGL 渲染器
    renderer_ = new OpenGLRenderer();
    if (!renderer_->Init()) {
        qCritical() << "Failed to initialize OpenGL renderer in GL thread";
        s_is_gl_thread = false;
        return;
    }
    renderer_->PostInit();

    QMutexLocker locker(&mutex_);
    
    while (!cancelled_) {
        // 处理队列中的所有任务
        while (!job_queue_.empty() && !cancelled_) {
            GLJobPtr job = job_queue_.front();
            job_queue_.pop_front();
            
            locker.unlock();
            
            if (!job->IsCancelled()) {
                try {
                    job->Execute(renderer_);
                } catch (const std::exception& e) {
                    qCritical() << "OpenGLThread: Exception in job execution:" << e.what();
                } catch (...) {
                    qCritical() << "OpenGLThread: Unknown exception in job execution";
                }
            }
            job->MarkCompleted();
            
            locker.relock();
        }

        if (!cancelled_) {
            wait_condition_.wait(&mutex_);
        }
    }

    // 清理剩余任务
    while (!job_queue_.empty()) {
        GLJobPtr job = job_queue_.front();
        job_queue_.pop_front();
        job->MarkCompleted();
    }

    locker.unlock();

    // 清理 OpenGL 资源
    if (renderer_) {
        renderer_->Destroy();
        delete renderer_;
        renderer_ = nullptr;
    }

    s_is_gl_thread = false;
}

} // namespace olive
