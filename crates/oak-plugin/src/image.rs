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

//! OFX image: an OFX view of a frame buffer (CPU path).
//!
//! Counterpart of the C++ `OliveImage`. Pixel memory is owned by this
//! crate (this was historically a hotspot of memory bugs: ownership
//! must be single, and lifetime is guaranteed by this type).
//! GL texture views are deferred to phase 2 (`// [P2]`).
//!
//! Property writes mirror the C++ `Image::allocate`
//! (image.cpp:132-172) and the HostSupport image property table
//! (HS: ofxhClip.cpp:458-472): Data/RowBytes/Bounds/
//! RegionOfDefinition/Components/PixelDepth/UniqueIdentifier.
//! This struct does not distinguish bounds from ROD (unified), and
//! does not model field/renderScale/pixelAspect/premultiplication
//! (`// [P2]`).

use std::ffi::{c_void, CString};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::instance::OfxRectD;
use crate::property::{PropertySet, Value};

// ---- OFX property names (macro strings of ofxCore.h / ofxImageEffect.h) ----

/// kOfxImagePropData (ofxCore.h:1275): pixel data pointer.
pub(crate) const K_IMAGE_PROP_DATA: &str = "OfxImagePropData";
/// kOfxImagePropRowBytes (ofxCore.h:1322): row byte count.
pub(crate) const K_IMAGE_PROP_ROW_BYTES: &str = "OfxImagePropRowBytes";
/// kOfxImagePropBounds (ofxCore.h:1291): pixel coordinates, Int x 4.
pub(crate) const K_IMAGE_PROP_BOUNDS: &str = "OfxImagePropBounds";
/// kOfxImagePropRegionOfDefinition (ofxCore.h:1307): pixel coordinates, Int x 4.
pub(crate) const K_IMAGE_PROP_ROD: &str = "OfxImagePropRegionOfDefinition";
/// kOfxImageEffectPropComponents (ofxImageEffect.h:915).
pub(crate) const K_IMAGE_EFFECT_PROP_COMPONENTS: &str = "OfxImageEffectPropComponents";
/// kOfxImageEffectPropPixelDepth (ofxImageEffect.h:901).
pub(crate) const K_IMAGE_EFFECT_PROP_PIXEL_DEPTH: &str = "OfxImageEffectPropPixelDepth";
/// kOfxImagePropUniqueIdentifier (ofxCore.h:927): host-assigned unique id.
pub(crate) const K_IMAGE_PROP_UNIQUE_ID: &str = "OfxImagePropUniqueIdentifier";

/// Process-wide monotonically increasing id (zero-dependency
/// replacement for HostSupport's UUID generation).
static NEXT_IMAGE_ID: AtomicU64 = AtomicU64::new(0);

/// Pixel bit depth (OFX kOfxBitDepth*; the full pipeline only uses
/// Float, the rest are kept for compatibility).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BitDepth {
	/// 8-bit integer (compat).
	Byte,
	/// 16-bit integer (compat).
	Short,
	/// 16-bit half float (compat).
	Half,
	/// 32-bit float (main path).
	Float,
}

impl BitDepth {
	/// Bytes per component.
	pub(crate) fn bytes_per_component(self) -> usize {
		match self {
			BitDepth::Byte => 1,
			BitDepth::Short | BitDepth::Half => 2,
			BitDepth::Float => 4,
		}
	}

	/// OFX bit depth string (kOfxBitDepth*, ofxCore.h:866-880).
	pub(crate) fn to_ofx(self) -> &'static str {
		match self {
			BitDepth::Byte => "OfxBitDepthByte",
			BitDepth::Short => "OfxBitDepthShort",
			BitDepth::Half => "OfxBitDepthHalf",
			BitDepth::Float => "OfxBitDepthFloat",
		}
	}
}

/// Component layout (OFX kOfxImageComponent*).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Components {
	/// RGBA.
	Rgba,
	/// RGB.
	Rgb,
	/// Single-channel Alpha.
	Alpha,
}

impl Components {
	/// Channel count.
	pub(crate) fn channel_count(self) -> usize {
		match self {
			Components::Rgba => 4,
			Components::Rgb => 3,
			Components::Alpha => 1,
		}
	}

	/// OFX component string (kOfxImageComponent*, ofxImageEffect.h:46-55).
	pub(crate) fn to_ofx(self) -> &'static str {
		match self {
			Components::Rgba => "OfxImageComponentRGBA",
			Components::Rgb => "OfxImageComponentRGB",
			Components::Alpha => "OfxImageComponentAlpha",
		}
	}
}

/// A single frame. `data` is row-major; the row stride may be padded
/// for alignment; bounds are in pixel coordinates.
/// `#[repr(C)]` with `props` at offset 0 (handle convention,
/// see [`crate::suites::tag`]).
#[repr(C)]
pub struct Image {
	/// Image-level properties (bounds, row bytes, depth, components,
	/// unique identifier).
	pub props: PropertySet,
	/// Pixel buffer (length = row_bytes * height).
	data: Vec<u8>,
	/// Bit depth.
	depth: BitDepth,
	/// Components.
	components: Components,
	/// Pixel bounds.
	bounds: OfxRectD,
	/// Row byte count.
	row_bytes: usize,
}

impl Image {
	/// Allocate by format (uninitialized pixels).
	///
	/// Property writes mirror the C++ `Image::allocate`
	/// (image.cpp:156-172): Data/RowBytes/Bounds/RegionOfDefinition/
	/// Components/PixelDepth; UniqueIdentifier is also written per the
	/// image property table (HS: ofxhClip.cpp:470).
	/// ROD is unified with bounds (this struct does not distinguish
	/// the two; both are pixel coordinates). A zero-size or inverted
	/// rectangle yields an empty buffer (mirrors the
	/// `buffer_size < 0 -> 0` guard in image.cpp:147-149).
	pub fn allocate(depth: BitDepth, components: Components, bounds: OfxRectD) -> Self {
		let (w, h) = {
			let width = (bounds.x2 - bounds.x1).round();
			let height = (bounds.y2 - bounds.y1).round();
			if width > 0.0 && height > 0.0 {
				(width as usize, height as usize)
			} else {
				(0, 0)
			}
		};

		let row_bytes = w * components.channel_count() * depth.bytes_per_component();

		let mut img = Self {
			props: PropertySet::new(),
			data: vec![0u8; row_bytes * h],
			depth,
			components,
			bounds,
			row_bytes,
		};

		// `data` is a heap buffer; moving/borrowing the vec does not move
		// the buffer, and it is never resized after allocation, so the
		// pointer stays valid for the whole Image lifetime (ownership
		// discipline: this is the only place that holds the buffer).
		img.props.define(
			K_IMAGE_PROP_DATA,
			vec![Value::Pointer(img.data.as_mut_ptr() as *mut c_void)],
		);
		img.props
			.define(K_IMAGE_PROP_ROW_BYTES, vec![Value::Int(row_bytes as i32)]);
		let b = |v: f64| Value::Int(v.round() as i32);
		img.props.define(
			K_IMAGE_PROP_BOUNDS,
			vec![b(bounds.x1), b(bounds.y1), b(bounds.x2), b(bounds.y2)],
		);
		img.props.define(
			K_IMAGE_PROP_ROD,
			vec![b(bounds.x1), b(bounds.y1), b(bounds.x2), b(bounds.y2)],
		);
		img.props.define(
			K_IMAGE_EFFECT_PROP_COMPONENTS,
			vec![Value::String(CString::new(components.to_ofx()).unwrap())],
		);
		img.props.define(
			K_IMAGE_EFFECT_PROP_PIXEL_DEPTH,
			vec![Value::String(CString::new(depth.to_ofx()).unwrap())],
		);
		img.props.define(
			K_IMAGE_PROP_UNIQUE_ID,
			vec![Value::String(unique_identifier())],
		);

		img
	}

	/// Mutable pixel slice (for writing plugin output). The length is
	/// consistent with the format by type construction.
	pub fn pixels_mut(&mut self) -> &mut [u8] {
		&mut self.data
	}

	/// Read-only pixel slice.
	pub fn pixels(&self) -> &[u8] {
		&self.data
	}

	/// Pixel bounds (canonical coordinates; the Image keeps bounds and
	/// ROD unified).
	pub fn bounds(&self) -> OfxRectD {
		self.bounds
	}

	/// Bit depth.
	pub fn depth(&self) -> BitDepth {
		self.depth
	}

	/// Components.
	pub fn components(&self) -> Components {
		self.components
	}

	/// Row byte count.
	pub fn row_bytes(&self) -> usize {
		self.row_bytes
	}
}

/// Unique identifier string (monotonically increasing per process,
/// hexadecimal; corresponds to the `uniqueIdentifier` parameter of
/// HS: ofxhClip.cpp:537).
pub(crate) fn unique_identifier() -> CString {
	let n = NEXT_IMAGE_ID.fetch_add(1, Ordering::Relaxed);
	// Hexadecimal ASCII, no NUL; unwrap cannot fail.
	CString::new(format!("{:x}", n)).unwrap()
}
