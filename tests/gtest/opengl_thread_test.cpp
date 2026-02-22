/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

***/

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
#include <vector>
#include <functional>

#include "render/opengl/openglthread.h"
#include "render/opengl/openglrenderer.h"

// 辅助类：用于在线程中执行 lambda
class LambdaThread : public QThread {
public:
    explicit LambdaThread(std::function<void()> func) : func_(std::move(func)) {}
    
protected:
    void run() override {
        if (func_) func_();
    }
    
private:
    std::function<void()> func_;
};

using namespace olive;

class OpenGLThreadTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		// 每个测试前确保没有残留的实例
		gl_thread_ = nullptr;
	}

	void TearDown() override
	{
		// 清理
		if (gl_thread_) {
			gl_thread_->Stop();
			gl_thread_->wait();
			delete gl_thread_;
			gl_thread_ = nullptr;
		}
	}

	OpenGLThread *gl_thread_ = nullptr;
};

// 测试 OpenGLThread 能正确创建和启动
TEST_F(OpenGLThreadTest, CreateAndStart)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	// 等待线程启动
	QThread::msleep(100);
	
	EXPECT_TRUE(gl_thread_->isRunning());
}

// 测试 OpenGLThread 能正确停止
TEST_F(OpenGLThreadTest, StopAndWait)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(100);
	EXPECT_TRUE(gl_thread_->isRunning());
	
	gl_thread_->Stop();
	gl_thread_->wait(5000);
	
	EXPECT_FALSE(gl_thread_->isRunning());
}

// 测试在 OpenGLThread 中创建纹理
TEST_F(OpenGLThreadTest, CreateTexture)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	// 等待线程启动
	QThread::msleep(200);
	
	VideoParams params(64, 64, PixelFormat::U8, 4);
	TexturePtr texture = gl_thread_->CreateTexture(params);
	
	EXPECT_NE(texture, nullptr);
	if (texture) {
		EXPECT_EQ(texture->params().width(), 64);
		EXPECT_EQ(texture->params().height(), 64);
		EXPECT_EQ(texture->params().format(), PixelFormat::U8);
	}
}

// 测试多线程并发创建纹理（模拟多 CPU 线程使用单 GL 线程）
TEST_F(OpenGLThreadTest, ConcurrentTextureCreation)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	const int kNumThreads = 4;
	const int kTexturesPerThread = 10;
	std::atomic<int> success_count{0};
	std::vector<std::unique_ptr<QThread>> threads;
	
	for (int t = 0; t < kNumThreads; ++t) {
		auto thread = std::make_unique<LambdaThread>([&]() {
			for (int i = 0; i < kTexturesPerThread; ++i) {
				VideoParams params(32 + i, 32 + i, PixelFormat::U8, 4);
				TexturePtr texture = gl_thread_->CreateTexture(params);
				if (texture) {
					success_count++;
				}
			}
		});
		threads.push_back(std::move(thread));
	}
	
	// 启动所有线程
	for (auto &thread : threads) {
		thread->start();
	}
	
	// 等待所有线程完成
	for (auto &thread : threads) {
		thread->wait(10000);
	}
	
	EXPECT_EQ(success_count.load(), kNumThreads * kTexturesPerThread);
}

// 测试 Flush 操作
TEST_F(OpenGLThreadTest, FlushOperation)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	// 创建一些纹理
	VideoParams params(64, 64, PixelFormat::U8, 4);
	auto texture1 = gl_thread_->CreateTexture(params);
	auto texture2 = gl_thread_->CreateTexture(params);
	
	EXPECT_NE(texture1, nullptr);
	EXPECT_NE(texture2, nullptr);
	
	// 执行 Flush
	gl_thread_->Flush();
	
	// Flush 应该能正常完成
	SUCCEED();
}

// 测试在 GL 线程内调用 IsInGLThread
TEST_F(OpenGLThreadTest, IsInGLThread)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	// 从外部线程调用，应该返回 false
	EXPECT_FALSE(gl_thread_->IsInGLThread());
	
	// 使用自定义任务在 GL 线程内检查
	std::atomic<bool> in_thread_result{false};
	auto job = std::make_shared<GLCustomJob>([&](OpenGLRenderer *) {
		in_thread_result = gl_thread_->IsInGLThread();
	});
	
	gl_thread_->SubmitJobAndWait(job);
	
	EXPECT_TRUE(in_thread_result.load());
}

// 测试同步和异步任务提交
TEST_F(OpenGLThreadTest, SyncAndAsyncJobSubmission)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	std::atomic<int> counter{0};
	
	// 提交异步任务
	for (int i = 0; i < 5; ++i) {
		auto job = std::make_shared<GLCustomJob>([&](OpenGLRenderer *) {
			counter++;
		});
		gl_thread_->SubmitJob(job);
	}
	
	// 等待所有任务完成
	gl_thread_->WaitForIdle();
	EXPECT_EQ(counter.load(), 5);
	
	// 提交同步任务
	auto sync_job = std::make_shared<GLCustomJob>([&](OpenGLRenderer *) {
		counter += 10;
	});
	gl_thread_->SubmitJobAndWait(sync_job);
	
	EXPECT_EQ(counter.load(), 15);
}

// 测试任务取消
TEST_F(OpenGLThreadTest, JobCancellation)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	std::atomic<bool> job_executed{false};
	
	auto job = std::make_shared<GLCustomJob>([&](OpenGLRenderer *) {
		job_executed = true;
	});
	
	// 取消任务
	job->Cancel();
	
	// 提交已取消的任务
	gl_thread_->SubmitJobAndWait(job);
	
	// 已取消的任务不应该执行
	EXPECT_FALSE(job_executed.load());
}

// 测试 InterlaceTexture 操作
TEST_F(OpenGLThreadTest, InterlaceTexture)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	VideoParams params(64, 64, PixelFormat::U8, 4);
	auto top = gl_thread_->CreateTexture(params);
	auto bottom = gl_thread_->CreateTexture(params);
	
	EXPECT_NE(top, nullptr);
	EXPECT_NE(bottom, nullptr);
	
	// 执行交错操作
	auto result = gl_thread_->InterlaceTexture(top, bottom, params);
	
	// 注意：InterlaceTexture 可能需要更复杂的设置，这里只是测试接口调用
	// 如果内部实现有特定的要求，可能需要调整测试
}

// 性能测试：大量纹理创建
TEST_F(OpenGLThreadTest, PerformanceMassTextureCreation)
{
	gl_thread_ = new OpenGLThread();
	gl_thread_->start();
	
	QThread::msleep(200);
	
	const int kNumTextures = 100;
	QElapsedTimer timer;
	timer.start();
	
	for (int i = 0; i < kNumTextures; ++i) {
		VideoParams params(64, 64, PixelFormat::U8, 4);
		auto texture = gl_thread_->CreateTexture(params);
		EXPECT_NE(texture, nullptr);
	}
	
	qint64 elapsed = timer.elapsed();
	qDebug() << "Created" << kNumTextures << "textures in" << elapsed << "ms"
			 << "(" << (double)elapsed / kNumTextures << "ms per texture)";
	
	// 性能检查：创建 100 个纹理应该在 5 秒内完成
	EXPECT_LT(elapsed, 5000);
}
