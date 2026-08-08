// Oak Video Editor - Non-Linear Video Editor
// Copyright (C) 2026 Oak Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//! Pixel and sample format enums (oakcore `PixelFormat` /
//! `SampleFormat` equivalents). Integer values MUST match the C++
//! enums (`core/include/olive/core/render/pixelformat.h`,
//! `sampleformat.h`) — they cross the C ABI as `int`.

/// Pixel format (values identical to `olive::core::PixelFormat::Format`:
/// invalid=-1, u8=0, u10=1, u16=2, f16=3, f32=4).
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PixelFormat {
    /// Invalid/unspecified.
    Invalid = -1,
    /// 8-bit unsigned per channel.
    U8 = 0,
    /// 10-bit unsigned per channel (packed).
    U10 = 1,
    /// 16-bit unsigned per channel.
    U16 = 2,
    /// 16-bit half float.
    F16 = 3,
    /// 32-bit float (primary pipeline format).
    F32 = 4,
}

impl PixelFormat {
    /// Bytes per channel (C++ `byte_count`; Invalid -> 0, U10 -> 4
    /// since it is packed RGBA10A2 stored as 4 bytes per pixel).
    pub fn bytes_per_channel(self) -> usize {
        match self {
            PixelFormat::Invalid => 0,
            PixelFormat::U8 => 1,
            PixelFormat::U10 => 4,
            PixelFormat::U16 | PixelFormat::F16 => 2,
            PixelFormat::F32 => 4,
        }
    }

    /// Bytes per pixel for `channels` (C++ `bytes_per_pixel`).
    pub fn bytes_per_pixel(self, channels: usize) -> usize {
        self.bytes_per_channel() * channels
    }
}

/// Audio sample format (values identical to
/// `olive::core::SampleFormat::Format`: PLANAR first — u8_p=0, s16_p=1,
/// s32_p=2, s64_p=3, f32_p=4, f64_p=5, then packed u8=6, s16=7, s32=8,
/// s64=9, f32=10, f64=11). This order is load-bearing: oakaudio's
/// default format constant (f32_p) is 4.
#[repr(i32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SampleFormat {
    /// Invalid/unspecified.
    Invalid = -1,
    /// Unsigned 8-bit planar.
    U8Planar = 0,
    /// Signed 16-bit planar.
    S16Planar = 1,
    /// Signed 32-bit planar.
    S32Planar = 2,
    /// Signed 64-bit planar.
    S64Planar = 3,
    /// 32-bit float planar.
    F32Planar = 4,
    /// 64-bit float planar.
    F64Planar = 5,
    /// Unsigned 8-bit packed.
    U8 = 6,
    /// Signed 16-bit packed.
    S16 = 7,
    /// Signed 32-bit packed.
    S32 = 8,
    /// Signed 64-bit packed.
    S64 = 9,
    /// 32-bit float packed.
    F32 = 10,
    /// 64-bit float packed.
    F64 = 11,
}

impl SampleFormat {
    /// Bytes per sample (C++ `byte_count`; Invalid -> 0).
    pub fn bytes_per_sample(self) -> usize {
        match self {
            SampleFormat::Invalid => 0,
            SampleFormat::U8Planar | SampleFormat::U8 => 1,
            SampleFormat::S16Planar | SampleFormat::S16 => 2,
            SampleFormat::S32Planar
            | SampleFormat::S32
            | SampleFormat::F32Planar
            | SampleFormat::F32 => 4,
            SampleFormat::S64Planar
            | SampleFormat::S64
            | SampleFormat::F64Planar
            | SampleFormat::F64 => 8,
        }
    }

    /// True for planar layouts (C++ `is_planar`).
    pub fn is_planar(self) -> bool {
        (self as i32) >= 0 && (self as i32) < 6
    }

    /// Packed counterpart of a planar format (and vice versa;
    /// C++ `to_packed`/`to_planar`).
    pub fn to_packed(self) -> SampleFormat {
        match self {
            SampleFormat::U8Planar => SampleFormat::U8,
            SampleFormat::S16Planar => SampleFormat::S16,
            SampleFormat::S32Planar => SampleFormat::S32,
            SampleFormat::S64Planar => SampleFormat::S64,
            SampleFormat::F32Planar => SampleFormat::F32,
            SampleFormat::F64Planar => SampleFormat::F64,
            other => other,
        }
    }

    /// See [`SampleFormat::to_packed`].
    pub fn to_planar(self) -> SampleFormat {
        match self {
            SampleFormat::U8 => SampleFormat::U8Planar,
            SampleFormat::S16 => SampleFormat::S16Planar,
            SampleFormat::S32 => SampleFormat::S32Planar,
            SampleFormat::S64 => SampleFormat::S64Planar,
            SampleFormat::F32 => SampleFormat::F32Planar,
            SampleFormat::F64 => SampleFormat::F64Planar,
            other => other,
        }
    }
}
