#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QtTest/QSignalSpy>

#include "render/renderticket.h"

using namespace olive;

TEST(RenderTicketWatcher, DoesNotEmitFinishedForRunningTicketSynchronously)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();

	RenderTicketWatcher watcher;
	QSignalSpy spy(&watcher, &RenderTicketWatcher::finished);

	watcher.set_ticket(ticket);

	// The ticket is still running, so the watcher must not emit Finished
	// synchronously when SetTicket is called.
	EXPECT_EQ(spy.count(), 0);

	ticket->finish();

	// Once the ticket finishes, the watcher should emit Finished.
	spy.wait(100);
	EXPECT_EQ(spy.count(), 1);
}

TEST(RenderTicketWatcher, EmitsFinishedForAlreadyFinishedTicketAsynchronously)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();
	ticket->finish();

	RenderTicketWatcher watcher;
	QSignalSpy spy(&watcher, &RenderTicketWatcher::finished);

	watcher.set_ticket(ticket);

	// The ticket has already finished. The watcher must not delete itself or
	// emit Finished synchronously inside SetTicket, because the caller may still
	// need the returned pointer. Instead it should defer the signal.
	EXPECT_EQ(spy.count(), 0);
	EXPECT_FALSE(watcher.get_ticket() == nullptr);

	// Process the queued Finished emission.
	QCoreApplication::processEvents();

	EXPECT_EQ(spy.count(), 1);
}

TEST(RenderTicketWatcher, CancelMarksTicketAsCancelled)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();

	RenderTicketWatcher watcher;
	watcher.set_ticket(ticket);

	EXPECT_TRUE(watcher.is_running());
	EXPECT_FALSE(ticket->is_cancelled());

	watcher.cancel();

	EXPECT_TRUE(ticket->is_cancelled());
}

TEST(RenderTicket, HasResultIsFalseWhileRunning)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();

	EXPECT_TRUE(ticket->is_running());
	EXPECT_FALSE(ticket->has_result());
}

TEST(RenderTicket, FinishWithValueProvidesResult)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();
	ticket->finish(QVariant(42));

	EXPECT_FALSE(ticket->is_running());
	EXPECT_TRUE(ticket->has_result());
	EXPECT_EQ(ticket->get().toInt(), 42);
}

TEST(RenderTicket, FinishCountIncrementsOnEachFinish)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	EXPECT_EQ(ticket->get_finish_count(), 0);

	ticket->start();
	ticket->finish();
	EXPECT_EQ(ticket->get_finish_count(), 1);

	ticket->start();
	ticket->finish();
	EXPECT_EQ(ticket->get_finish_count(), 2);
}

TEST(RenderTicket, FinishWithoutStartIsIgnored)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->finish();
	EXPECT_EQ(ticket->get_finish_count(), 0);
}

TEST(RenderTicketWatcher, DelegatesGetAndHasResultToTicket)
{
	RenderTicketPtr ticket = std::make_shared<RenderTicket>();
	ticket->start();
	ticket->finish(QVariant(QStringLiteral("result")));

	RenderTicketWatcher watcher;
	watcher.set_ticket(ticket);

	EXPECT_FALSE(watcher.is_running());
	EXPECT_TRUE(watcher.has_result());
	EXPECT_EQ(watcher.get().toString(), QStringLiteral("result"));
}

TEST(RenderTicketWatcher, EmptyWatcherReturnsDefaults)
{
	RenderTicketWatcher watcher;
	EXPECT_FALSE(watcher.is_running());
	EXPECT_FALSE(watcher.has_result());
	EXPECT_TRUE(watcher.get().isNull());
	EXPECT_EQ(watcher.get_ticket(), nullptr);
}

TEST(RenderTicketWatcher, SettingTicketTwiceIsRejected)
{
	RenderTicketPtr first = std::make_shared<RenderTicket>();
	RenderTicketPtr second = std::make_shared<RenderTicket>();

	RenderTicketWatcher watcher;
	watcher.set_ticket(first);
	watcher.set_ticket(second);

	EXPECT_EQ(watcher.get_ticket(), first);
}
