#include <gtest/gtest.h>

#include <QEventLoop>
#include <QTimer>

#include "task/taskmanager.h"

namespace
{
class DummyTask final : public olive::Task {
public:
	explicit DummyTask(bool *ran)
		: ran_(ran)
	{
		set_title(QStringLiteral("DummyTask"));
	}

protected:
	bool run() override
	{
		if (ran_) {
			*ran_ = true;
		}
		return true;
	}

private:
	bool *ran_ = nullptr;
};

class FailingTask final : public olive::Task {
public:
	FailingTask()
	{
		set_title(QStringLiteral("FailingTask"));
	}

protected:
	bool run() override
	{
		set_error(QStringLiteral("expected failure"));
		return false;
	}
};

class ProgressTask final : public olive::Task {
public:
	explicit ProgressTask(int steps)
		: steps_(steps)
	{
		set_title(QStringLiteral("ProgressTask"));
	}

protected:
	bool run() override
	{
		for (int i = 0; i <= steps_; ++i) {
			emit progress_changed(static_cast<double>(i) / steps_);
		}
		return true;
	}

private:
	int steps_ = 1;
};
}

TEST(TaskManager, AddAndRunTask)
{
	olive::TaskManager::create_instance();
	olive::TaskManager *mgr = olive::TaskManager::instance();
	ASSERT_NE(mgr, nullptr);

	bool ran = false;
	DummyTask *task = new DummyTask(&ran);

	QEventLoop loop;
	QObject::connect(task, &olive::Task::finished, &loop,
					 [&loop](olive::Task *, bool) { loop.quit(); });

	mgr->add_task(task);

	QTimer::singleShot(5000, &loop, &QEventLoop::quit);
	loop.exec();

	EXPECT_TRUE(ran);
	olive::TaskManager::destroy_instance();
}

TEST(TaskManager, FailedTaskEmitsTaskFailed)
{
	olive::TaskManager::create_instance();
	olive::TaskManager *mgr = olive::TaskManager::instance();
	ASSERT_NE(mgr, nullptr);

	FailingTask *task = new FailingTask();

	QEventLoop loop;
	bool saw_failed = false;
	QObject::connect(mgr, &olive::TaskManager::task_failed, &loop,
					 [&loop, &saw_failed](olive::Task *) {
						 saw_failed = true;
						 loop.quit();
					 });
	QTimer::singleShot(5000, &loop, &QEventLoop::quit);

	mgr->add_task(task);
	loop.exec();

	EXPECT_TRUE(saw_failed);
	EXPECT_FALSE(task->get_error().isEmpty());
	olive::TaskManager::destroy_instance();
}

TEST(TaskManager, ProgressSignalIsEmitted)
{
	olive::TaskManager::create_instance();
	olive::TaskManager *mgr = olive::TaskManager::instance();
	ASSERT_NE(mgr, nullptr);

	ProgressTask *task = new ProgressTask(4);

	QEventLoop loop;
	QVector<double> progress;
	QObject::connect(task, &olive::Task::progress_changed, &loop,
					 [&progress](double p) { progress.append(p); });
	QObject::connect(task, &olive::Task::finished, &loop,
					 [&loop](olive::Task *, bool) { loop.quit(); });
	QTimer::singleShot(5000, &loop, &QEventLoop::quit);

	mgr->add_task(task);
	loop.exec();

	EXPECT_FALSE(progress.isEmpty());
	EXPECT_GE(progress.last(), 0.99);
	olive::TaskManager::destroy_instance();
}

TEST(TaskManager, MultipleTasksComplete)
{
	olive::TaskManager::create_instance();
	olive::TaskManager *mgr = olive::TaskManager::instance();
	ASSERT_NE(mgr, nullptr);

	bool ran1 = false;
	bool ran2 = false;
	DummyTask *task1 = new DummyTask(&ran1);
	DummyTask *task2 = new DummyTask(&ran2);

	QEventLoop loop;
	int finished = 0;
	auto on_finished = [&loop, &finished](olive::Task *, bool) {
		if (++finished == 2) {
			loop.quit();
		}
	};
	QObject::connect(task1, &olive::Task::finished, &loop, on_finished);
	QObject::connect(task2, &olive::Task::finished, &loop, on_finished);
	QTimer::singleShot(5000, &loop, &QEventLoop::quit);

	mgr->add_task(task1);
	mgr->add_task(task2);
	loop.exec();

	EXPECT_TRUE(ran1);
	EXPECT_TRUE(ran2);
	olive::TaskManager::destroy_instance();
}
