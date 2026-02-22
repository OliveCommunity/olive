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

#ifndef OPENGLTHREAD_H
#define OPENGLTHREAD_H

#include <QMutex>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>
#include <deque>

#include "gljob.h"

namespace olive
{

class OpenGLRenderer;

/**
 * @brief 单线程 OpenGL 执行线程
 * 
 * 所有 OpenGL 操作都在这个单线程中串行执行，避免多线程 OpenGL 上下文问题
 */
class OpenGLThread : public QThread {
    Q_OBJECT
public:
    explicit OpenGLThread(QObject *parent = nullptr);
    virtual ~OpenGLThread() override;

    /**
     * @brief 提交 GL 任务到队列（异步执行）
     */
    void SubmitJob(GLJobPtr job);

    /**
     * @brief 提交 GL 任务并等待完成（同步执行）
     */
    void SubmitJobAndWait(GLJobPtr job);

    /**
     * @brief 创建纹理（同步）
     */
    TexturePtr CreateTexture(const VideoParams &params, const void *data = nullptr, int linesize = 0);

    /**
     * @brief 销毁纹理（异步）
     */
    void DestroyTexture(Texture *texture);

    /**
     * @brief 上传纹理数据（同步）
     */
    void UploadTexture(TexturePtr texture, const void *data, int linesize);

    /**
     * @brief 下载纹理数据（同步）
     */
    bool DownloadTexture(TexturePtr texture, void *data, int linesize);

    /**
     * @brief Shader Blit（同步）
     */
    void BlitShader(QVariant shader, AcceleratedJob &job, TexturePtr destination,
                    const VideoParams &dest_params, bool clear_destination);

    /**
     * @brief Color Managed Blit（同步）
     */
    void BlitColorManaged(const ColorTransformJob &job, TexturePtr destination,
                          const VideoParams &params);

    /**
     * @brief 清屏（同步）
     */
    void ClearDestination(TexturePtr texture, double r, double g, double b, double a);

    /**
     * @brief 创建 Shader（同步）
     */
    QVariant CreateShader(const ShaderCode &code);

    /**
     * @brief 销毁 Shader（异步）
     */
    void DestroyShader(QVariant shader);

    /**
     * @brief Flush（同步）
     */
    void Flush();

    /**
     * @brief 获取像素（同步）
     */
    Color GetPixel(TexturePtr texture, const QPointF &pt);

    /**
     * @brief 交错纹理（同步）
     */
    TexturePtr InterlaceTexture(TexturePtr top, TexturePtr bottom, const VideoParams &params);

    /**
     * @brief 停止线程
     */
    void Stop();

    /**
     * @brief 等待所有队列任务完成
     */
    void WaitForIdle();

    /**
     * @brief 检查当前是否在 OpenGL 线程
     */
    bool IsInGLThread() const;

protected:
    virtual void run() override;

private:
    bool cancelled_ = false;
    QMutex mutex_;
    QWaitCondition wait_condition_;
    std::deque<GLJobPtr> job_queue_;

    OpenGLRenderer *renderer_ = nullptr;

    // 线程本地存储，用于判断是否在 GL 线程
    static thread_local bool s_is_gl_thread;
};

} // namespace olive

#endif // OPENGLTHREAD_H
