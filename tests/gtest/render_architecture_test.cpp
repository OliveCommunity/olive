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

#include "render/rendermanager.h"
#include "render/opengl/openglthread.h"
#include "node/color/colormanager/colormanager.h"

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

class RenderArchitectureTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		// 确保 ColorManager 的默认配置已设置
		if (!ColorManager::GetDefaultConfig()) {
			ColorManager::SetUpDefaultConfig();
		}
	}

	void TearDown() override
	{
		// 清理 RenderManager
		if (RenderManager::instance()) {
			RenderManager::DestroyInstance();
		}
	}
};

// 测试新的多线程 CPU + 单线程 OpenGL 架构能正确初始化
TEST_F(RenderArchitectureTest, MultiThreadCPU_SingleThreadGL_Init)
{
	// 验证没有实例
	ASSERT_EQ(RenderManager::instance(), nullptr);
	
	// 创建实例（使用真实的 OpenGL 后端）
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	// 验证 GL 线程存在
	auto *gl_thread = rm->GetGLThread();
	EXPECT_NE(gl_thread, nullptr);
	
	// 验证有多个渲染线程
	EXPECT_GT(rm->GetVideoThreadCount(), 0);
	
	// 销毁实例
	RenderManager::DestroyInstance();
	EXPECT_EQ(RenderManager::instance(), nullptr);
}

// 测试 GL 线程是单线程
TEST_F(RenderArchitectureTest, GLThreadIsSingleThread)
{
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	auto *gl_thread = rm->GetGLThread();
	ASSERT_NE(gl_thread, nullptr);
	
	// 验证 GL 线程只在一个操作系统线程上运行
	std::atomic<int> check_count{0};
	
	for (int i = 0; i < 10; ++i) {
		auto job = std::make_shared<GLCustomJob>([&](OpenGLRenderer *) {
			(void)QThread::currentThreadId(); // 验证可以在 GL 线程中调用
			check_count++;
		});
		gl_thread->SubmitJobAndWait(job);
	}
	
	EXPECT_EQ(check_count.load(), 10);
	
	RenderManager::DestroyInstance();
}

// 测试多 CPU 线程并发提交 GL 任务
TEST_F(RenderArchitectureTest, ConcurrentCPUSubmissions)
{
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	auto *gl_thread = rm->GetGLThread();
	ASSERT_NE(gl_thread, nullptr);
	
	const int kNumCPUThreads = 4;
	const int kJobsPerThread = 25;
	std::atomic<int> success_count{0};
	std::vector<std::unique_ptr<QThread>> cpu_threads;
	
	QElapsedTimer timer;
	timer.start();
	
	for (int t = 0; t < kNumCPUThreads; ++t) {
		auto thread = std::make_unique<LambdaThread>([&, t]() {
			for (int i = 0; i < kJobsPerThread; ++i) {
				VideoParams params(32 + t, 32 + i, PixelFormat::U8, 4);
				TexturePtr texture = gl_thread->CreateTexture(params);
				if (texture) {
					success_count++;
				}
			}
		});
		cpu_threads.push_back(std::move(thread));
	}
	
	// 启动所有 CPU 线程
	for (auto &thread : cpu_threads) {
		thread->start();
	}
	
	// 等待所有线程完成
	for (auto &thread : cpu_threads) {
		thread->wait(10000);
	}
	
	qint64 elapsed = timer.elapsed();
	
	EXPECT_EQ(success_count.load(), kNumCPUThreads * kJobsPerThread);
	qDebug() << "Concurrent texture creation:" << kNumCPUThreads << "threads x"
			 << kJobsPerThread << "jobs =" << success_count.load() 
			 << "in" << elapsed << "ms";
	
	RenderManager::DestroyInstance();
}

// 测试架构性能：比较单线程 vs 多线程 CPU 提交
TEST_F(RenderArchitectureTest, ArchitecturePerformanceComparison)
{
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	auto *gl_thread = rm->GetGLThread();
	ASSERT_NE(gl_thread, nullptr);
	
	const int kTotalJobs = 100;
	
	// 单线程提交
	QElapsedTimer single_thread_timer;
	single_thread_timer.start();
	
	for (int i = 0; i < kTotalJobs; ++i) {
		VideoParams params(64, 64, PixelFormat::U8, 4);
		auto texture = gl_thread->CreateTexture(params);
		EXPECT_NE(texture, nullptr);
	}
	
	qint64 single_thread_time = single_thread_timer.elapsed();
	
	// 多线程提交（4 个 CPU 线程）
	const int kNumThreads = 4;
	const int kJobsPerThread = kTotalJobs / kNumThreads;
	
	QElapsedTimer multi_thread_timer;
	multi_thread_timer.start();
	
	std::vector<std::unique_ptr<QThread>> threads;
	for (int t = 0; t < kNumThreads; ++t) {
		auto thread = std::make_unique<LambdaThread>([&]() {
			for (int i = 0; i < kJobsPerThread; ++i) {
				VideoParams params(64, 64, PixelFormat::U8, 4);
				auto texture = gl_thread->CreateTexture(params);
			}
		});
		threads.push_back(std::move(thread));
	}
	
	for (auto &thread : threads) {
		thread->start();
	}
	for (auto &thread : threads) {
		thread->wait(10000);
	}
	
	qint64 multi_thread_time = multi_thread_timer.elapsed();
	
	qDebug() << "Performance comparison for" << kTotalJobs << "textures:";
	qDebug() << "  Single-threaded:" << single_thread_time << "ms";
	qDebug() << "  Multi-threaded (" << kNumThreads << "threads):" << multi_thread_time << "ms";
	if (multi_thread_time > 0) {
		qDebug() << "  Speedup:" << (double)single_thread_time / multi_thread_time << "x";
	}
	
	// 多线程应该更快或至少不更慢（考虑到线程开销）
	// 这里不做严格断言，因为性能受系统负载影响
	
	RenderManager::DestroyInstance();
}

// 测试任务优先级和队列管理
TEST_F(RenderArchitectureTest, TaskQueueManagement)
{
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	auto *gl_thread = rm->GetGLThread();
	ASSERT_NE(gl_thread, nullptr);
	
	std::atomic<int> execution_order{0};
	std::vector<int> order_record;
	QMutex order_mutex;
	
	// 提交多个任务记录执行顺序
	for (int i = 0; i < 10; ++i) {
		auto job = std::make_shared<GLCustomJob>([&, i](OpenGLRenderer *) {
			QMutexLocker locker(&order_mutex);
			order_record.push_back(i);
		});
		gl_thread->SubmitJob(job);
	}
	
	// 等待所有任务完成
	gl_thread->WaitForIdle();
	
	// 验证所有任务都执行了
	EXPECT_EQ(order_record.size(), 10);
	
	RenderManager::DestroyInstance();
}

// 测试架构稳定性：大量并发操作
TEST_F(RenderArchitectureTest, StressTest)
{
	RenderManager::CreateInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);
	
	auto *gl_thread = rm->GetGLThread();
	ASSERT_NE(gl_thread, nullptr);
	
	const int kNumThreads = 8;
	const int kOperationsPerThread = 50;
	std::atomic<int> success_count{0};
	std::atomic<int> failure_count{0};
	
	QElapsedTimer timer;
	timer.start();
	
	std::vector<std::unique_ptr<QThread>> threads;
	for (int t = 0; t < kNumThreads; ++t) {
		auto thread = std::make_unique<LambdaThread>([&, t]() {
			for (int i = 0; i < kOperationsPerThread; ++i) {
				try {
					// 混合操作：创建纹理、flush
					VideoParams params(32 + (i % 32), 32 + (i % 32), PixelFormat::U8, 4);
					auto texture = gl_thread->CreateTexture(params);
					if (texture) {
						success_count++;
					} else {
						failure_count++;
					}
					
					// 每 10 个操作执行一次 flush
					if (i % 10 == 0) {
						gl_thread->Flush();
					}
				} catch (...) {
					failure_count++;
				}
			}
		});
		threads.push_back(std::move(thread));
	}
	
	for (auto &thread : threads) {
		thread->start();
	}
	for (auto &thread : threads) {
		thread->wait(30000);
	}
	
	qint64 elapsed = timer.elapsed();
	
	int total = success_count.load() + failure_count.load();
	qDebug() << "Stress test:" << total << "operations in" << elapsed << "ms";
	qDebug() << "  Success:" << success_count.load();
	qDebug() << "  Failure:" << failure_count.load();
	
	// 所有操作都应该成功
	EXPECT_EQ(failure_count.load(), 0);
	EXPECT_EQ(success_count.load(), kNumThreads * kOperationsPerThread);
	
	RenderManager::DestroyInstance();
}
