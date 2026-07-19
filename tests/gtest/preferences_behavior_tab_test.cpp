#include <gtest/gtest.h>

#include <QApplication>

#include "audio/audiomanager.h"
#include "config/config.h"
#include "dialog/preferences/tabs/preferencesbehaviortab.h"
#include "dialog/preferences/tabs/preferencesgeneraltab.h"
#include "dialog/preferences/tabs/preferencesaudiotab.h"

using namespace olive;

TEST(PreferencesBehaviorTab, TimelineCategoryHasExpectedCheckboxes)
{
	PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_timeline);
	// 8 timeline behavior options
	EXPECT_EQ(tab.findChildren<QCheckBox *>().size(), 8);
}

TEST(PreferencesBehaviorTab, PlaybackCategoryHasExpectedCheckboxes)
{
	PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_playback);
	EXPECT_EQ(tab.findChildren<QCheckBox *>().size(), 2);
}

TEST(PreferencesBehaviorTab, ProjectCategoryHasExpectedCheckboxes)
{
	PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_project);
	EXPECT_EQ(tab.findChildren<QCheckBox *>().size(), 1);
}

TEST(PreferencesBehaviorTab, NodesCategoryHasExpectedCheckboxes)
{
	PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_nodes);
	EXPECT_EQ(tab.findChildren<QCheckBox *>().size(), 3);
}

TEST(PreferencesBehaviorTab, RenderingCategoryHasGraphicsBackendCombobox)
{
	PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_rendering);
	EXPECT_FALSE(tab.findChildren<QComboBox *>().isEmpty());
	EXPECT_FALSE(tab.findChildren<QCheckBox *>().isEmpty());
}

TEST(PreferencesBehaviorTab, BehaviorPrefTrReturnsExactSourceStrings)
{
	// BehaviorPrefTr() provides the shared source strings used by the other
	// preference tabs; without a translator installed it returns the source
	// text unchanged. Pin the exact strings so accidental edits are caught
	// (they would silently change the translation keys and the cross-tab
	// lookups that rely on them).
	EXPECT_EQ(PreferencesBehaviorTab::behavior_pref_tr("Behavior"),
			  QStringLiteral("Behavior"));
	EXPECT_EQ(PreferencesBehaviorTab::behavior_pref_tr("Enable hover focus"),
			  QStringLiteral("Enable hover focus"));
	EXPECT_EQ(PreferencesBehaviorTab::behavior_pref_tr("Enable slider ladder"),
			  QStringLiteral("Enable slider ladder"));
	EXPECT_EQ(PreferencesBehaviorTab::behavior_pref_tr(
				  "Scrolling zooms by default"),
			  QStringLiteral("Scrolling zooms by default"));
	EXPECT_EQ(PreferencesBehaviorTab::behavior_pref_tr("Enable audio scrubbing"),
			  QStringLiteral("Enable audio scrubbing"));
}

TEST(PreferencesBehaviorTab, RenderingCategoryContainsDefaultBackend)
{
	// Force the config back to the registered default so the selected entry
	// is deterministic regardless of test order
	const QVariant saved_backend =
		Config::current()[QStringLiteral("GraphicsBackend")];
	Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("opengl");

	{
		PreferencesBehaviorTab tab(PreferencesBehaviorTab::k_category_rendering);
		QList<QComboBox *> boxes = tab.findChildren<QComboBox *>();
		ASSERT_FALSE(boxes.isEmpty());

		QComboBox *backend_box = boxes.first();

		// config.cpp registers "opengl" as the default GraphicsBackend
		const int opengl_index =
			backend_box->findData(QStringLiteral("opengl"));
		ASSERT_NE(opengl_index, -1);
		EXPECT_EQ(backend_box->itemText(opengl_index),
				  QStringLiteral("OpenGL"));
		EXPECT_EQ(backend_box->currentIndex(), opengl_index);
	}

	Config::current()[QStringLiteral("GraphicsBackend")] = saved_backend;
}

TEST(PreferencesGeneralTab, ContainsHoverFocusOption)
{
	PreferencesGeneralTab tab;
	QList<QCheckBox *> boxes = tab.findChildren<QCheckBox *>();

	bool found = false;
	foreach (QCheckBox *box, boxes) {
		if (box->text() ==
			PreferencesBehaviorTab::behavior_pref_tr("Enable hover focus")) {
			found = true;
			break;
		}
	}
	EXPECT_TRUE(found);
}

TEST(PreferencesAudioTab, AudioScrubbingCheckboxUsesBehaviorTranslation)
{
	AudioManager::create_instance();

	{
		PreferencesAudioTab tab;
		QList<QCheckBox *> boxes = tab.findChildren<QCheckBox *>();

		bool found = false;
		foreach (QCheckBox *box, boxes) {
			if (box->text() == PreferencesBehaviorTab::behavior_pref_tr(
								   "Enable audio scrubbing")) {
				found = true;
				break;
			}
		}
		EXPECT_TRUE(found);
	}

	AudioManager::destroy_instance();
}

TEST(PreferencesGeneralTab, IncludesBehaviorOptions)
{
	PreferencesGeneralTab tab;
	QList<QCheckBox *> boxes = tab.findChildren<QCheckBox *>();
	EXPECT_GE(boxes.size(), 3);
}

TEST(PreferencesAudioTab, IncludesAudioScrubbingOption)
{
	AudioManager::create_instance();

	{
		PreferencesAudioTab tab;

		// Locate the scrubbing checkbox by its exact (untranslated) label
		QCheckBox *scrubbing = nullptr;
		foreach (QCheckBox *box, tab.findChildren<QCheckBox *>()) {
			if (box->text() == QStringLiteral("Enable audio scrubbing")) {
				scrubbing = box;
				break;
			}
		}
		ASSERT_NE(scrubbing, nullptr);

		// Its initial state mirrors the AudioScrubbing config entry
		EXPECT_EQ(scrubbing->isChecked(),
				  Config::current()[QStringLiteral("AudioScrubbing")].toBool());
	}

	AudioManager::destroy_instance();
}
