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

//! ofxColour（M11 §4）：宿主 OCIO 能力宣告、实例协商属性、输入 clip
//! 工作空间（ACEScg）、GetOutputColourspace 往返（偏好采纳 +
//! 交叉引用解析 + 输出写回）。

mod common;

use std::ffi::{c_void, CString};

use oakplugin::ffi::{oakplugin_host_scan, oakplugin_instance_create, oakplugin_instance_free};
use oakplugin::handle::{get, CHandle};
use oakplugin::host::Host;
use oakplugin::property::Value;

const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

fn scan_and_create(id: &str) -> CHandle {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return CHandle::null();
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin_host_scan(dirs.as_ptr(), 1) };
	let id = cs(id);
	unsafe { oakplugin_instance_create(id.as_ptr()) }
}

fn instance_of(
	h: &CHandle,
) -> Option<std::sync::Arc<oakplugin::handle::RefBox<oakplugin::instance::Instance>>> {
	unsafe { get::<std::sync::Arc<oakplugin::handle::RefBox<oakplugin::instance::Instance>>>(h) }
		.cloned()
}

fn prop_str(props: &oakplugin::property::PropertySet, name: &str) -> Option<String> {
	match props.get(name, 0)? {
		Value::String(s) => Some(s.to_string_lossy().into_owned()),
		_ => None,
	}
}

/// 宿主能力宣告：OCIO 模式 + native 配置列表 + GL 支持。
#[test]
fn host_declares_ocio_capability() {
	let host = Host::global();
	assert_eq!(
		prop_str(&host.props, "OfxImageEffectPropColourManagementStyle").as_deref(),
		Some("OfxImageEffectColourManagementOCIO")
	);
	assert_eq!(
		host.props
			.dimension("OfxImageEffectPropColourManagementAvailableConfigs"),
		1,
		"可用配置至少含 ofx-native-v1.5_aces-v1.3_ocio-v2.3"
	);
	assert_eq!(
		prop_str(&host.props, "OfxImageEffectPropOpenGLRenderSupported").as_deref(),
		Some("true")
	);
}

/// 实例期协商属性：style/config/OCIOConfig 已写；输入 clip 色彩空间
/// = 工作空间 ACEScg；输出 clip 未设（待 GetOutputColourspace）。
#[test]
fn instance_and_clip_colourspace_props() {
	common::with_host(|| {
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		let inst = instance_of(&h).expect("句柄应可解析");
		assert_eq!(
			prop_str(&inst.value.props, "OfxImageEffectPropColourManagementStyle").as_deref(),
			Some("OfxImageEffectColourManagementOCIO")
		);
		assert_eq!(
			prop_str(
				&inst.value.props,
				"OfxImageEffectPropColourManagementConfig"
			)
			.as_deref(),
			Some("ofx-native-v1.5_aces-v1.3_ocio-v2.3")
		);
		assert_eq!(
			prop_str(&inst.value.props, "OfxImageEffectPropOCIOConfig").as_deref(),
			Some("ocio://default")
		);
		let source = inst
			.value
			.clips
			.iter()
			.find(|c| c.name == "Source")
			.unwrap();
		assert_eq!(
			prop_str(&source.props, "OfxImageClipPropColourspace").as_deref(),
			Some("ACEScg"),
			"输入 clip 色彩空间 = 工作空间"
		);
		let output = inst
			.value
			.clips
			.iter()
			.find(|c| c.name == "Output")
			.unwrap();
		assert!(
			prop_str(&output.props, "OfxImageClipPropColourspace")
				.map(|s| s.is_empty())
				.unwrap_or(true),
			"输出 clip 色彩空间在 GetOutputColourspace 前未设"
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// GetOutputColourspace 往返：无偏好 → 插件回 "OfxColourspace_Source"
/// 交叉引用 → 宿主解析为 Source 的实际色彩空间（ACEScg）并写回输出。
#[test]
fn get_output_colourspace_cross_reference() {
	common::with_host(|| {
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		let inst = instance_of(&h).expect("句柄应可解析");
		let cs = inst
			.value
			.get_output_colourspace(&[])
			.expect("GetOutputColourspace 应成功");
		assert_eq!(cs, "ACEScg", "交叉引用 OfxColourspace_Source → ACEScg");
		inst.value.set_output_colourspace(&cs);
		let output = inst
			.value
			.clips
			.iter()
			.find(|c| c.name == "Output")
			.unwrap();
		assert_eq!(
			prop_str(&output.props, "OfxImageClipPropColourspace").as_deref(),
			Some("ACEScg"),
			"输出 clip 已写回解析后的色彩空间"
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// GetOutputColourspace 偏好采纳：插件优先取宿主偏好的第一个色彩
/// 空间。
#[test]
fn get_output_colourspace_preferred() {
	common::with_host(|| {
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		let inst = instance_of(&h).expect("句柄应可解析");
		let cs = inst
			.value
			.get_output_colourspace(&["ACEScg".to_string(), "linear".to_string()])
			.expect("GetOutputColourspace 应成功");
		assert_eq!(cs, "ACEScg", "偏好列表第一个被采纳");
		unsafe { oakplugin_instance_free(&mut h) };
	});
}

/// 交叉引用解析的边界：未知 clip 引用原样返回；非交叉引用原样返回。
#[test]
fn resolve_colourspace_edge_cases() {
	common::with_host(|| {
		let mut h = scan_and_create(TEST_PLUGIN_ID);
		if h.is_null() {
			return;
		}
		let inst = instance_of(&h).expect("句柄应可解析");
		assert_eq!(
			inst.value
				.resolve_colourspace("OfxColourspace_NoSuchClip".to_string()),
			"OfxColourspace_NoSuchClip",
			"未知 clip 引用保持原样"
		);
		assert_eq!(
			inst.value.resolve_colourspace("ACEScg".to_string()),
			"ACEScg",
			"非交叉引用原样返回"
		);
		unsafe { oakplugin_instance_free(&mut h) };
	});
}
