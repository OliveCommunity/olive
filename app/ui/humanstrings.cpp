/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "humanstrings.h"

#include <QCoreApplication>

namespace olive
{

QString HumanStrings::sample_rate_to_string(const int &sample_rate)
{
	return QCoreApplication::translate("AudioParams", "%1 Hz").arg(sample_rate);
}

QString HumanStrings::channel_layout_to_string(const uint64_t &layout)
{
	switch (layout) {
	case k_channel_layout_mono:
		return QCoreApplication::translate("AudioParams", "Mono");
	case k_channel_layout_stereo:
		return QCoreApplication::translate("AudioParams", "Stereo");
	case k_channel_layout2_1:
		return QCoreApplication::translate("AudioParams", "2.1");
	case k_channel_layout5_point1:
		return QCoreApplication::translate("AudioParams", "5.1");
	case k_channel_layout7_point1:
		return QCoreApplication::translate("AudioParams", "7.1");
	default:
		return QCoreApplication::translate("AudioParams", "Unknown (0x%1)")
			.arg(layout, 1, 16);
	}
}

QString HumanStrings::format_to_string(const SampleFormat &f)
{
	switch (f) {
	case SampleFormat::u8:
		return QCoreApplication::translate("AudioParams",
										   "Unsigned 8-bit (Packed)");
	case SampleFormat::s16:
		return QCoreApplication::translate("AudioParams",
										   "Signed 16-bit (Packed)");
	case SampleFormat::s32:
		return QCoreApplication::translate("AudioParams",
										   "Signed 32-bit (Packed)");
	case SampleFormat::s64:
		return QCoreApplication::translate("AudioParams",
										   "Signed 64-bit (Packed)");
	case SampleFormat::f32:
		return QCoreApplication::translate("AudioParams",
										   "Float 32-bit (Packed)");
	case SampleFormat::f64:
		return QCoreApplication::translate("AudioParams",
										   "Float 64-bit (Packed)");
	case SampleFormat::u8_p:
		return QCoreApplication::translate("AudioParams",
										   "Unsigned 8-bit (Planar)");
	case SampleFormat::s16_p:
		return QCoreApplication::translate("AudioParams",
										   "Signed 16-bit (Planar)");
	case SampleFormat::s32_p:
		return QCoreApplication::translate("AudioParams",
										   "Signed 32-bit (Planar)");
	case SampleFormat::s64_p:
		return QCoreApplication::translate("AudioParams",
										   "Signed 64-bit (Planar)");
	case SampleFormat::f32_p:
		return QCoreApplication::translate("AudioParams",
										   "Float 32-bit (Planar)");
	case SampleFormat::f64_p:
		return QCoreApplication::translate("AudioParams",
										   "Float 64-bit (Planar)");

	case SampleFormat::invalid:
	case SampleFormat::count:
		break;
	}

	return QCoreApplication::translate("AudioParams", "Unknown (0x%1)")
		.arg(static_cast<int>(f), 1, 16);
}

}
