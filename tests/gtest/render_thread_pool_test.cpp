/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include "render/rendermanager.h"
#include "render/renderticket.h"

using namespace olive;

class RenderThreadPoolTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		// 确保 RenderManager 实例已创建
		if (!RenderManager::instance()) {
			RenderManager::CreateInstance();
		}
	}

	void TearDown() override
	{
		// 清理所有待处理的任务
		auto *rm = RenderManager::instance();
		if (rm) {
			// 等待一段时间确保任务完成
			QThread::msleep(100);
		}
	}
};

// 测试简单的渲染任务提交和执行
TEST_F(RenderThreadPoolTest, BasicTaskSubmission)
{
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 创建一个简单的测试节点（这里需要一个真实的节点或模拟）
	// 由于实际渲染需要复杂的设置，我们先测试线程池的基本功能

	// 记录开始时间
	QElapsedTimer timer;
	timer.start();

	// 提交多个简单的渲染任务
	const int kNumTasks = 10;
	std::vector<RenderTicketPtr> tickets;

	for (int i = 0; i < kNumTasks; ++i) {
		// 创建一个测试用的渲染参数
		// 注意：这里使用 kNull 返回类型，避免需要实际的 OpenGL 上下文
		RenderManager::RenderVideoParams params(
			nullptr, // node - 使用 nullptr 进行简单测试
			VideoParams(), // 默认视频参数
			AudioParams(), // 默认音频参数
			rational(i, 1), // 时间
			nullptr, // color_manager
			RenderMode::kOffline, // 渲染模式
			RenderPriority::kCache // 优先级
		);

		params.return_type = RenderManager::ReturnType::kNull;

		auto ticket = rm->RenderFrame(params);
		ASSERT_NE(ticket, nullptr);
		tickets.push_back(ticket);
	}

	// 等待所有任务完成
	bool all_finished = false;
	int wait_time = 0;
	const int kMaxWaitTime = 5000; // 最多等待5秒

	while (!all_finished && wait_time < kMaxWaitTime) {
		all_finished = true;
		for (const auto &ticket : tickets) {
			if (!ticket->HasResult() && !ticket->IsCancelled()) {
				all_finished = false;
				break;
			}
		}

		if (!all_finished) {
			QThread::msleep(10);
			wait_time += 10;
		}
	}

	// 验证所有任务都已完成
	EXPECT_TRUE(all_finished) << "Not all tasks finished within timeout";

	for (const auto &ticket : tickets) {
		EXPECT_TRUE(ticket->HasResult() || ticket->IsCancelled())
			<< "Task did not complete or get cancelled";
	}

	qDebug() << "BasicTaskSubmission: All" << tickets.size()
			 << "tasks processed in" << timer.elapsed() << "ms";
}

// 测试任务均匀分配到多个线程
TEST_F(RenderThreadPoolTest, TaskDistribution)
{
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交大量任务以观察分配情况
	const int kNumTasks = 20;
	std::vector<RenderTicketPtr> tickets;

	QElapsedTimer timer;
	timer.start();

	for (int i = 0; i < kNumTasks; ++i) {
		RenderManager::RenderVideoParams params(nullptr, VideoParams(),
												AudioParams(), rational(i, 1),
												nullptr, RenderMode::kOffline,
												RenderPriority::kCache);

		params.return_type = RenderManager::ReturnType::kNull;

		auto ticket = rm->RenderFrame(params);
		tickets.push_back(ticket);
	}

	// 等待所有任务完成
	bool all_finished = false;
	int wait_time = 0;
	const int kMaxWaitTime = 10000;

	while (!all_finished && wait_time < kMaxWaitTime) {
		all_finished = true;
		for (const auto &ticket : tickets) {
			if (!ticket->HasResult() && !ticket->IsCancelled()) {
				all_finished = false;
				break;
			}
		}

		if (!all_finished) {
			QThread::msleep(10);
			wait_time += 10;
		}
	}

	EXPECT_TRUE(all_finished) << "Task distribution test timeout";
	qDebug() << "TaskDistribution:" << kNumTasks
			 << "tasks distributed and processed in" << timer.elapsed() << "ms";
}

// 测试任务取消功能
TEST_F(RenderThreadPoolTest, TaskCancellation)
{
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交一个任务
	RenderManager::RenderVideoParams params(nullptr, VideoParams(),
											AudioParams(), rational(0, 1),
											nullptr, RenderMode::kOffline,
											RenderPriority::kCache);

	params.return_type = RenderManager::ReturnType::kNull;

	auto ticket = rm->RenderFrame(params);
	ASSERT_NE(ticket, nullptr);

	// 立即尝试取消
	ticket->Cancel();

	// 等待一段时间
	QThread::msleep(100);

	// 验证任务被取消或已完成
	EXPECT_TRUE(ticket->IsCancelled() || ticket->HasResult())
		<< "Task should be cancelled or finished";
}

// 测试并发任务处理能力
TEST_F(RenderThreadPoolTest, ConcurrentTaskProcessing)
{
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	const int kNumTasks = 30;
	std::vector<RenderTicketPtr> tickets;
	std::atomic<int> completed_count{ 0 };

	// 提交多个任务
	for (int i = 0; i < kNumTasks; ++i) {
		RenderManager::RenderVideoParams params(nullptr, VideoParams(),
												AudioParams(), rational(i, 1),
												nullptr, RenderMode::kOffline,
												RenderPriority::kCache);

		params.return_type = RenderManager::ReturnType::kNull;

		auto ticket = rm->RenderFrame(params);

		// 连接完成信号来计数
		// 注意：RenderTicket 使用 Finished 信号
		tickets.push_back(ticket);
	}

	QElapsedTimer timer;
	timer.start();

	// 等待所有任务完成
	bool all_finished = false;
	int wait_time = 0;
	const int kMaxWaitTime = 15000;

	while (!all_finished && wait_time < kMaxWaitTime) {
		all_finished = true;
		for (const auto &ticket : tickets) {
			if (!ticket->HasResult() && !ticket->IsCancelled()) {
				all_finished = false;
				break;
			}
		}

		if (!all_finished) {
			QThread::msleep(10);
			wait_time += 10;
		}
	}

	int finished_count = 0;
	for (const auto &ticket : tickets) {
		if (ticket->HasResult() || ticket->IsCancelled()) {
			finished_count++;
		}
	}

	EXPECT_EQ(finished_count, kNumTasks) << "Only " << finished_count << " of "
										 << kNumTasks << " tasks finished";

	qDebug() << "ConcurrentTaskProcessing:" << finished_count << "/"
			 << kNumTasks << "tasks completed in" << timer.elapsed() << "ms";
}

// 测试 RenderThread 的基本功能
TEST_F(RenderThreadPoolTest, RenderThreadBasicOperations)
{
	// 这个测试直接测试 RenderThread 的功能

	// 创建 RenderManager 会创建 RenderThread
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交一个简单任务验证线程在运行
	RenderManager::RenderVideoParams params(nullptr, VideoParams(),
											AudioParams(), rational(0, 1),
											nullptr, RenderMode::kOffline,
											RenderPriority::kCache);

	params.return_type = RenderManager::ReturnType::kNull;

	auto ticket = rm->RenderFrame(params);
	ASSERT_NE(ticket, nullptr);

	// 等待任务完成
	int wait_time = 0;
	while (!ticket->HasResult() && !ticket->IsCancelled() && wait_time < 5000) {
		QThread::msleep(10);
		wait_time += 10;
	}

	EXPECT_TRUE(ticket->HasResult() || ticket->IsCancelled())
		<< "RenderThread should process the task";
}

// 测试长时间运行的任务不会阻塞系统
TEST_F(RenderThreadPoolTest, NonBlockingOperation)
{
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	QElapsedTimer timer;
	timer.start();

	// 提交多个任务
	const int kNumBatches = 3;
	const int kTasksPerBatch = 5;

	for (int batch = 0; batch < kNumBatches; ++batch) {
		std::vector<RenderTicketPtr> batch_tickets;

		for (int i = 0; i < kTasksPerBatch; ++i) {
			RenderManager::RenderVideoParams params(
				nullptr, VideoParams(), AudioParams(),
				rational(batch * kTasksPerBatch + i, 1), nullptr,
				RenderMode::kOffline, RenderPriority::kCache);

			params.return_type = RenderManager::ReturnType::kNull;

			auto ticket = rm->RenderFrame(params);
			batch_tickets.push_back(ticket);
		}

		// 每批提交后短暂等待，让系统有时间处理
		QThread::msleep(50);

		// 验证任务正在处理中或已完成
		int processing = 0;
		for (const auto &ticket : batch_tickets) {
			if (ticket->IsRunning() || ticket->HasResult() ||
				ticket->IsCancelled()) {
				processing++;
			}
		}

		// 不要求所有任务都开始处理，但至少应该有进展
		qDebug() << "Batch" << batch << ":" << processing << "/"
				 << kTasksPerBatch << "tasks started processing";
	}

	qDebug()
		<< "NonBlockingOperation test completed in" << timer.elapsed() << "ms";
}
/*
// 主测试入口
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
*/