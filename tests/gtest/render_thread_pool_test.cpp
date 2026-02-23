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
#include "node/color/colormanager/colormanager.h"

using namespace olive;

class RenderThreadPoolTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		// 确保 ColorManager 的默认配置已设置（必须在创建 RenderManager 之前）
		if (!ColorManager::GetDefaultConfig()) {
			ColorManager::SetUpDefaultConfig();
		}
	}

	void TearDown() override
	{
		// 销毁 RenderManager 实例
		if (RenderManager::instance()) {
			RenderManager::DestroyInstance();
		}
	}
};

// 测试 RenderManager 测试实例能正确创建和销毁（无 GPU 模式）
TEST_F(RenderThreadPoolTest, CreateAndDestroy)
{
	// 验证没有实例
	ASSERT_EQ(RenderManager::instance(), nullptr);

	// 创建测试实例（使用 kDummy backend，不需要 GPU）
	RenderManager::CreateTestInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 验证 backend 是 kDummy
	EXPECT_EQ(rm->backend(), RenderManager::kDummy);

	// 销毁实例
	RenderManager::DestroyInstance();
	EXPECT_EQ(RenderManager::instance(), nullptr);
}

// 测试任务可以提交到线程池（使用 kNull 返回类型，不需要实际渲染）
TEST_F(RenderThreadPoolTest, TaskSubmission)
{
	// 创建测试实例
	RenderManager::CreateTestInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交一个简单的渲染任务
	RenderManager::RenderVideoParams params(
		nullptr, // node - 使用 nullptr
		VideoParams(1920, 1080, PixelFormat::U8, 4),
		AudioParams(),
		rational(0, 1),
		nullptr,
		RenderMode::kOffline,
		RenderPriority::kCache
	);
	params.return_type = RenderManager::ReturnType::kNull;

	auto ticket = rm->RenderFrame(params);
	ASSERT_NE(ticket, nullptr);

	// 验证任务已创建（即使 node 为 nullptr，任务也应该被处理）
	EXPECT_FALSE(ticket->IsCancelled());
}

// 测试任务取消功能
TEST_F(RenderThreadPoolTest, TaskCancellation)
{
	// 创建测试实例
	RenderManager::CreateTestInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交任务
	RenderManager::RenderVideoParams params(
		nullptr,
		VideoParams(1920, 1080, PixelFormat::U8, 4),
		AudioParams(),
		rational(0, 1),
		nullptr,
		RenderMode::kOffline,
		RenderPriority::kCache
	);
	params.return_type = RenderManager::ReturnType::kNull;

	auto ticket = rm->RenderFrame(params);
	ASSERT_NE(ticket, nullptr);

	// 取消任务
	ticket->Cancel();

	// 验证任务已被取消
	EXPECT_TRUE(ticket->IsCancelled());
}

// 测试多个任务提交
TEST_F(RenderThreadPoolTest, MultipleTaskSubmission)
{
	// 创建测试实例
	RenderManager::CreateTestInstance();
	auto *rm = RenderManager::instance();
	ASSERT_NE(rm, nullptr);

	// 提交多个任务
	const int kNumTasks = 5;
	std::vector<RenderTicketPtr> tickets;

	for (int i = 0; i < kNumTasks; ++i) {
		RenderManager::RenderVideoParams params(
			nullptr,
			VideoParams(1920, 1080, PixelFormat::U8, 4),
			AudioParams(),
			rational(i, 1),
			nullptr,
			RenderMode::kOffline,
			RenderPriority::kCache
		);
		params.return_type = RenderManager::ReturnType::kNull;

		auto ticket = rm->RenderFrame(params);
		ASSERT_NE(ticket, nullptr);
		tickets.push_back(ticket);
	}

	// 验证所有任务都已创建
	EXPECT_EQ(tickets.size(), kNumTasks);

	// 等待一段时间让任务处理
	QThread::msleep(100);
}
