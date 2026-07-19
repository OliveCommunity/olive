#include <gtest/gtest.h>

#include "common/jobtime.h"

namespace
{

// Collects qDebug output for inspection
QStringList g_captured_messages;

void capture_message_handler(QtMsgType, const QMessageLogContext &,
						   const QString &msg)
{
	g_captured_messages.append(msg);
}

} // namespace

TEST(CommonJobTime, ConstructorAcquiresValue)
{
	olive::JobTime a;
	olive::JobTime b;

	EXPECT_NE(a.value(), b.value());
	EXPECT_LT(a.value(), b.value());
}

TEST(CommonJobTime, AcquireUpdatesValue)
{
	olive::JobTime a;
	uint64_t first = a.value();
	a.acquire();
	uint64_t second = a.value();

	EXPECT_GT(second, first);
}

TEST(CommonJobTime, ComparisonOperators)
{
	olive::JobTime a;
	olive::JobTime b;

	// `a` was acquired first, so its value is strictly lower than `b`'s
	EXPECT_LT(a, b);
	EXPECT_GT(b, a);
	EXPECT_LE(a, b);
	EXPECT_GE(b, a);
	EXPECT_NE(a, b);

	// A copy holds the same value and must compare equal
	olive::JobTime c = a;
	EXPECT_EQ(a, c);
	EXPECT_LE(a, c);
	EXPECT_GE(a, c);
}

TEST(CommonJobTime, DebugStream)
{
	olive::JobTime a;

	g_captured_messages.clear();
	QtMessageHandler old = qInstallMessageHandler(capture_message_handler);
	{
		QDebug debug(QtDebugMsg);
		debug << a;
	}
	qInstallMessageHandler(old);

	// operator<< streams the raw value, so the message must contain it
	ASSERT_EQ(g_captured_messages.size(), 1);
	EXPECT_TRUE(
		g_captured_messages.first().contains(QString::number(a.value())));
}
