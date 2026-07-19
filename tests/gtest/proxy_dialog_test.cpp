#include <gtest/gtest.h>

#include <QCheckBox>
#include <QPushButton>
#include <QTreeWidget>

#include "config/config.h"
#include "dialog/proxy/proxydialog.h"
#include "node/project/footage/footage.h"

namespace
{

QVariant proxy_dialog_config_value(const char *key)
{
	return olive::Config::current()[QString::fromUtf8(key)];
}

} // namespace

TEST(ProxyDialog, ConstructsInGlobalModeWithNullParent)
{
	olive::ProxyDialog dialog(nullptr);

	// The global settings editors must reflect the current config values
	EXPECT_EQ(dialog.proxy_width(),
			  proxy_dialog_config_value("ProxyWidth").value<int>());
	EXPECT_EQ(dialog.proxy_height(),
			  proxy_dialog_config_value("ProxyHeight").value<int>());
	EXPECT_EQ(dialog.proxy_crf(),
			  proxy_dialog_config_value("ProxyCRF").value<int>());
	EXPECT_EQ(dialog.proxy_preset(),
			  proxy_dialog_config_value("ProxyPreset").toString());
	EXPECT_EQ(dialog.proxy_include_audio(),
			  proxy_dialog_config_value("ProxyIncludeAudio").toBool());
	EXPECT_EQ(dialog.f_fmpeg_path(),
			  proxy_dialog_config_value("FFmpegPath").toString());
}

TEST(ProxyDialog, ConstructsWithFootageList)
{
	olive::Footage footage(QStringLiteral("/tmp/oak-proxy-test.mov"));
	const QVector<olive::Footage *> items = { &footage };

	olive::ProxyDialog dialog(nullptr, items);
	EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Proxy Settings"));

	// Global mode has no footage tree at all
	olive::ProxyDialog global_dialog(nullptr);
	EXPECT_EQ(global_dialog.findChild<QTreeWidget *>(), nullptr);

	// Footage mode shows one tree row per item with its proxy state
	auto *tree = dialog.findChild<QTreeWidget *>();
	ASSERT_NE(tree, nullptr);
	ASSERT_EQ(tree->topLevelItemCount(), 1);
	EXPECT_EQ(tree->topLevelItem(0)->text(0),
			  QStringLiteral("/tmp/oak-proxy-test.mov"));
	EXPECT_EQ(tree->topLevelItem(0)->text(1), QStringLiteral("missing"));

	// Fresh footage has no custom params, so the custom settings checkbox
	// starts unchecked
	QCheckBox *custom_checkbox = nullptr;
	foreach (QCheckBox *box, dialog.findChildren<QCheckBox *>()) {
		if (box->text() ==
			QStringLiteral("Use custom settings for selected footage")) {
			custom_checkbox = box;
			break;
		}
	}
	ASSERT_NE(custom_checkbox, nullptr);
	EXPECT_FALSE(custom_checkbox->isChecked());

	// Footage mode adds generate/delete actions next to Close
	QStringList button_texts;
	foreach (QPushButton *b, dialog.findChildren<QPushButton *>()) {
		button_texts << b->text();
	}
	EXPECT_TRUE(button_texts.contains(QStringLiteral("Generate Proxies")));
	EXPECT_TRUE(button_texts.contains(QStringLiteral("Delete Proxies")));
	EXPECT_TRUE(button_texts.contains(QStringLiteral("Close")));
}

TEST(ProxyDialog, AcceptSavesGlobalSettingsToConfig)
{
	const int old_width = proxy_dialog_config_value("ProxyWidth").value<int>();
	const bool old_include_audio =
		proxy_dialog_config_value("ProxyIncludeAudio").toBool();
	const QString old_ffmpeg_path =
		proxy_dialog_config_value("FFmpegPath").toString();

	{
		olive::ProxyDialog dialog(nullptr);
		dialog.set_proxy_width(640);
		dialog.set_proxy_include_audio(!old_include_audio);
		dialog.set_f_fmpeg_path(QStringLiteral("/tmp/oak-test-ffmpeg"));
		dialog.accept();
	}

	EXPECT_EQ(proxy_dialog_config_value("ProxyWidth").value<int>(), 640);
	EXPECT_EQ(proxy_dialog_config_value("ProxyIncludeAudio").toBool(),
			  !old_include_audio);
	EXPECT_EQ(proxy_dialog_config_value("FFmpegPath").toString(),
			  QStringLiteral("/tmp/oak-test-ffmpeg"));

	// Restore previous config values so other tests are unaffected
	olive::Config::current()[QStringLiteral("ProxyWidth")] = old_width;
	olive::Config::current()[QStringLiteral("ProxyIncludeAudio")] =
		old_include_audio;
	olive::Config::current()[QStringLiteral("FFmpegPath")] = old_ffmpeg_path;
}
