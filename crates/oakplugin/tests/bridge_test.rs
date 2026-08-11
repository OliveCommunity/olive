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

//! bridge 层测试：param↔oaknode、clip↔oakrender、undo 打包。
//!
//! 依赖 liboaknode/liboakrender/liboakundo 的用例（standalone 树
//! 环境，外层 ctest 驱动）在 cargo 内以库内桩替代
//! （`--features test-stubs`）：桥调用面一致，桩提供节点值/命令/
//! 帧状态访问器（见 `bridge::{node,render,undo}::stub`）。未启用
//! 桩时同用例走 dlsym 缺失的降级路径（可解释错误 / no-op）。
//!
//! 系统级冒烟：[`system_misc_ofx_bundle_smoke`] 在
//! `/Library/OFX/Plugins/Misc.ofx.bundle` 存在时扫描真实 OFX 插件
//! 并 create_instance（absent 时 skip）。

mod common;

use std::ffi::{c_int, c_void, CString};

use oakplugin::clip::ClipInstance;
use oakplugin::descriptor::ClipDescriptor;
use oakplugin::handle::CHandle;
use oakplugin::image::{BitDepth, Components, Image};
use oakplugin::instance::{Instance, OfxRectD, RenderScale};
use oakplugin::param::ParamInstance;
use oakplugin::property::PropertySet;
use oakplugin::suites::param::suite_v1 as param_suite;
use oakplugin::suites::tag;

const OK: c_int = 0;
const TEST_PLUGIN_ID: &str = "org.oak.test-plugin";

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

/// 简易纹理句柄（桩 ctx 约定，见 bridge::render::stub：src=0xA2、
/// dst=0xA1）。
fn fake_texture(ctx: usize) -> CHandle {
	CHandle {
		ctx: ctx as *mut c_void,
		addref: None,
		release: None,
		abi_version: 1,
	}
}

/// 用 param suite 的 paramDefine 造一个指定 OFX 类型的参数实例
/// （describe 产物 → createInstance）。
fn make_param(ofx_type: &str) -> ParamInstance {
	let mut desc = oakplugin::descriptor::EffectDescriptor::new();
	let s = param_suite();
	let dhandle = tag::make(&desc.props as *const PropertySet, tag::DESCRIPTOR);
	let t = cs(ofx_type);
	let n = cs("p");
	let mut ph: *mut c_void = std::ptr::null_mut();
	let r = unsafe { (s.param_define)(dhandle, t.as_ptr(), n.as_ptr(), &mut ph) };
	assert_eq!(r, OK, "paramDefine {ofx_type} 失败: {r}");
	ParamInstance::from_def(*desc.params.pop().expect("刚 define 的参数"))
}

/// 简易 clip 实例。
fn make_clip(name: &str) -> ClipInstance {
	ClipInstance::from_descriptor(&ClipDescriptor {
		props: PropertySet::new(),
		name: name.into(),
	})
}

/// 扫描测试插件并创建实例，绑定 `identity`（0 = 不绑定），返回
/// （实例, gain 参数句柄）。插件不可用时 skip 并返回 None。
fn bound_inst(
	identity: usize,
) -> Option<(
	std::sync::Arc<oakplugin::handle::RefBox<Instance>>,
	*mut c_void,
)> {
	if common::test_plugin_scan_dir().is_none() {
		common::skip("最小测试插件未构建");
		return None;
	}
	let dir = cs(common::test_plugin_scan_dir().unwrap().to_str().unwrap());
	let dirs = [dir.as_ptr()];
	unsafe { oakplugin::ffi::oakplugin_host_scan(dirs.as_ptr(), 1) };
	let inst = oakplugin::host::Host::global()
		.create_instance(TEST_PLUGIN_ID, None)
		.ok()?;
	inst.value.bind_node(identity);
	let ih = tag::make(&inst.value.props as *const PropertySet, tag::INSTANCE);
	let name = cs("gain");
	let mut ph: *mut c_void = std::ptr::null_mut();
	let s = param_suite();
	let r = unsafe { (s.param_get_handle)(ih, name.as_ptr(), &mut ph, std::ptr::null_mut()) };
	assert_eq!(r, OK, "paramGetHandle(gain) 失败: {r}");
	Some((inst, ph))
}

/// 节点→插件：oaknode 输入值变化经桥写入 OFX 参数（类型映射表
/// 逐行：int/float/bool/color/vec2/vec3/combo/str_combo）。
#[test]
fn node_to_param_type_mapping() {
	use oakplugin::bridge::node::{node_value_type as T, Value};

	// Integer ← INT（num 载荷）。
	let p = make_param(oakplugin::param::TYPE_INTEGER);
	let mut v = Value::default();
	v.r#type = T::INT;
	v.num = 7;
	p.set_from_node(&v);
	assert_eq!(p.get(), oakplugin::param::ParamValue::Int([7, 0, 0], 1));

	// Double ← FLOAT（f[0] 载荷）。
	let p = make_param(oakplugin::param::TYPE_DOUBLE);
	p.set_from_node(&Value::float(1.5));
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Double([1.5, 0.0, 0.0], 1)
	);

	// Boolean ← BOOL（num 0/1）。
	let p = make_param(oakplugin::param::TYPE_BOOLEAN);
	p.set_from_node(&Value::bool_(true));
	assert_eq!(p.get(), oakplugin::param::ParamValue::Bool(true));

	// Choice ← COMBO。
	let p = make_param(oakplugin::param::TYPE_CHOICE);
	p.set_from_node(&Value::combo(2));
	assert_eq!(p.get(), oakplugin::param::ParamValue::Choice(2));

	// RGBA ← COLOR（r,g,b,a）。
	let p = make_param(oakplugin::param::TYPE_RGBA);
	p.set_from_node(&Value::color(0.1, 0.2, 0.3, 0.9));
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Color([0.1, 0.2, 0.3, 0.9], 4)
	);

	// RGB ← COLOR（alpha 忽略，恒 0 占位）。
	let p = make_param(oakplugin::param::TYPE_RGB);
	p.set_from_node(&Value::color(0.1, 0.2, 0.3, 0.5));
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Color([0.1, 0.2, 0.3, 0.0], 3)
	);

	// Double2D ← VEC2；Double3D ← VEC3。
	let p = make_param(oakplugin::param::TYPE_DOUBLE2D);
	p.set_from_node(&Value::vec(&[1.0, 2.0]));
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Double([1.0, 2.0, 0.0], 2)
	);
	let p = make_param(oakplugin::param::TYPE_DOUBLE3D);
	p.set_from_node(&Value::vec(&[1.0, 2.0, 3.0]));
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Double([1.0, 2.0, 3.0], 3)
	);

	// Integer2D ← VEC2 / Integer3D ← VEC3（浮点截断为 int）。
	let p = make_param(oakplugin::param::TYPE_INTEGER2D);
	p.set_from_node(&Value::vec(&[1.5, 2.5]));
	assert_eq!(p.get(), oakplugin::param::ParamValue::Int([1, 2, 0], 2));
	let p = make_param(oakplugin::param::TYPE_INTEGER3D);
	p.set_from_node(&Value::vec(&[1.5, 2.5, 3.5]));
	assert_eq!(p.get(), oakplugin::param::ParamValue::Int([1, 2, 3], 3));

	// String 参数 + STRING 节点类型：POD 不携带数据 → 值不变
	// （保持默认空串；字符串值走 facade 的字符串 API）。
	let p = make_param(oakplugin::param::TYPE_STRING);
	p.set_from_node(&Value::string());
	assert!(matches!(p.get(), oakplugin::param::ParamValue::String(s) if s.to_bytes().is_empty()));

	// 类型不匹配 → 忽略（保持现值）。
	let p = make_param(oakplugin::param::TYPE_DOUBLE);
	p.set_ofx(oakplugin::param::ParamValue::Double([5.0, 0.0, 0.0], 1));
	p.set_from_node(&Value::int(9)); // INT 不是 FLOAT
	assert_eq!(
		p.get(),
		oakplugin::param::ParamValue::Double([5.0, 0.0, 0.0], 1)
	);
}

/// 插件→节点：paramSetValue（插件自改）经 instanceChanged 桥回写
/// oaknode，且打包为一条 undo 命令（undo/redo 后值正确）。
#[test]
fn param_to_node_undoable_writeback() {
	common::with_host(|| {
		let Some((inst, gain_h)) = bound_inst(42) else {
			return;
		};
		let s = param_suite();

		#[cfg(feature = "test-stubs")]
		{
			use oakplugin::bridge::{node, undo};
			node::stub::reset();
			undo::stub::reset();
			node::stub::register_node(42);
			node::stub::set_input(42, "gain", node::Value::float(0.5));

			// 插件自改 → 回写 + 单命令立即 redo。
			let r = unsafe { (s.param_set_value)(gain_h, 0.75) };
			assert_eq!(r, OK, "paramSetValue 失败: {r}");
			let cur = node::stub::input(42, "gain").expect("已回写节点输入");
			assert_eq!(cur.value.f[0], 0.75);

			// undoable：命令捕获 prev/next 且已应用（redo 语义）。
			let rec = undo::stub::last_command().expect("回写产生命令");
			assert_eq!(rec.node, 42);
			assert_eq!(rec.input, "gain");
			assert!(!rec.is_multi);
			assert!(rec.applied);
			assert_eq!(rec.prev.f[0], 0.5, "prev = 修改前节点值");
			assert_eq!(rec.next.f[0], 0.75, "next = 修改后值");

			// undo → 恢复旧值；redo → 再应用。
			undo::stub::undo(rec.id);
			assert_eq!(node::stub::input(42, "gain").unwrap().value.f[0], 0.5);
			undo::stub::redo(rec.id);
			assert_eq!(node::stub::input(42, "gain").unwrap().value.f[0], 0.75);

			// 字符串参数：POD 无数据 → 经字符串桥回写。
			node::stub::set_input_string(42, "label", "old");
			let name = cs("label");
			let mut lh: *mut c_void = std::ptr::null_mut();
			let ih = tag::make(&inst.value.props as *const PropertySet, tag::INSTANCE);
			assert_eq!(
				unsafe { (s.param_get_handle)(ih, name.as_ptr(), &mut lh, std::ptr::null_mut()) },
				OK
			);
			let hello = cs("hello");
			assert_eq!(unsafe { (s.param_set_value)(lh, hello.as_ptr()) }, OK);
			let cur = node::stub::input(42, "label").expect("字符串已回写");
			assert_eq!(cur.string.as_deref(), Some("hello"));
		}

		#[cfg(not(feature = "test-stubs"))]
		{
			// 无 liboaknode：桥符号缺失 → node_from_identity 空句柄，
			// 回写 no-op；paramSetValue 仍成功且值本地生效。
			let r = unsafe { (s.param_set_value)(gain_h, 0.75) };
			assert_eq!(r, OK);
			let mut out = 0.0;
			assert_eq!(unsafe { (s.param_get_value)(gain_h, &mut out) }, OK);
			assert_eq!(out, 0.75);
		}
	});
}

/// 未绑定节点的实例收到 instanceChanged：no-op 不崩（身份注册表
/// 查无此项的路径）。
#[test]
fn writeback_without_bound_node_is_noop() {
	common::with_host(|| {
		let Some((inst, gain_h)) = bound_inst(0) else {
			return;
		};
		let s = param_suite();

		// identity 0（未绑定）：回写直接短路，值只写本地参数。
		assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.5) }, OK);
		let mut out = 0.0;
		assert_eq!(unsafe { (s.param_get_value)(gain_h, &mut out) }, OK);
		assert_eq!(out, 0.5);

		#[cfg(feature = "test-stubs")]
		{
			use oakplugin::bridge::{node, undo};
			node::stub::reset();
			undo::stub::reset();
			node::stub::register_node(9);

			// 已绑定但身份在注册表查无 → node_from_identity 空句柄。
			inst.value.bind_node(9999);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.6) }, OK);
			assert!(
				undo::stub::records().is_empty(),
				"身份查无不应产生命令：{:?}",
				undo::stub::records()
			);

			// 已登记身份但输入名不存在 → 桥返回错误，同样 no-op。
			inst.value.bind_node(9);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.7) }, OK);
			assert!(undo::stub::records().is_empty(), "输入不存在不应产生命令");

			// 节点输入值保持未回写（本地参数值仍有效）。
			let mut out = 0.0;
			assert_eq!(unsafe { (s.param_get_value)(gain_h, &mut out) }, OK);
			assert_eq!(out, 0.7);
		}
	});
}

/// clip 输入：oakrender 纹理挂接后 fetch_image 读回的像素与
/// 纹理内容一致（F32 格式不丢精度）。
#[test]
fn clip_texture_roundtrip_f32() {
	let clip = make_clip("Source");
	let scale = RenderScale { x: 1.0, y: 1.0 };

	// 未挂纹理 → NotFound（两模式一致）。
	assert!(matches!(
		clip.fetch_image(0.0, scale, None),
		Err(oakplugin::error::Error::NotFound)
	));
	// 子区域第 1 期不支持 → Failed。
	assert!(matches!(
		clip.fetch_image(
			0.0,
			scale,
			Some(OfxRectD {
				x1: 0.0,
				y1: 0.0,
				x2: 2.0,
				y2: 2.0
			})
		),
		Err(oakplugin::error::Error::Failed(_))
	));

	#[cfg(feature = "test-stubs")]
	{
		use oakplugin::bridge::render::{self, PIXEL_FORMAT_F32, PIXEL_FORMAT_U8};
		render::stub::reset();

		// 构造可识别的 F32 RGBA 帧（r=行, g=列, b=0, a=1）。
		let (w, h) = (8usize, 4usize);
		let mut pixels = Vec::with_capacity(w * h * 16);
		for y in 0..h {
			for x in 0..w {
				for v in [y as f32, x as f32, 0.0, 1.0] {
					pixels.extend_from_slice(&v.to_le_bytes());
				}
			}
		}
		render::stub::setup_src(w as i32, h as i32, PIXEL_FORMAT_F32, pixels.clone());
		let tex = fake_texture(0xA2);
		clip.set_input_texture(tex, 0.0);
		let img = clip
			.fetch_image(0.0, scale, None)
			.expect("fetch_image 成功");
		assert_eq!(img.pixels(), &pixels, "F32 像素 round-trip 不丢精度");
		assert_eq!(img.components(), Components::Rgba);
		assert_eq!(img.depth(), BitDepth::Float);

		// dummy 纹理输入 → 空输入（NotFound）。
		render::stub::set_dummy(0xA2, true);
		assert!(matches!(
			clip.fetch_image(0.0, scale, None),
			Err(oakplugin::error::Error::NotFound)
		));
		render::stub::set_dummy(0xA2, false);

		// 非 F32 输入帧 → 明确失败。
		render::stub::setup_src(w as i32, h as i32, PIXEL_FORMAT_U8, vec![0u8; w * h * 4]);
		assert!(matches!(
			clip.fetch_image(0.0, scale, None),
			Err(oakplugin::error::Error::Failed(_))
		));
	}
}

/// 输出：store_output_image 产出的纹理在 oakrender 侧可读且
/// 像素一致；dummy 纹理输入按空输入处理。
#[test]
fn output_image_to_texture() {
	let clip = make_clip("Output");
	let img = Image::allocate(
		BitDepth::Float,
		Components::Rgba,
		OfxRectD {
			x1: 0.0,
			y1: 0.0,
			x2: 8.0,
			y2: 4.0,
		},
	);

	// 未挂输出纹理 → NotFound（两模式一致）。
	assert!(matches!(
		clip.store_output_image(&img),
		Err(oakplugin::error::Error::NotFound)
	));

	#[cfg(feature = "test-stubs")]
	{
		use oakplugin::bridge::render::{self, PIXEL_FORMAT_F32, PIXEL_FORMAT_U8};
		render::stub::reset();
		render::stub::setup_dst(8, 4, PIXEL_FORMAT_F32);
		let tex = fake_texture(0xA1);
		clip.set_output_texture(tex, 0.0);

		// 填非平凡像素后回写 → oakrender 侧读到的与图像一致。
		let mut img = img;
		for (i, b) in img.pixels_mut().iter_mut().enumerate() {
			*b = (i % 256) as u8;
		}
		let tex2 = clip
			.store_output_image(&img)
			.expect("store_output_image 成功");
		assert_eq!(tex2.ctx, tex.ctx, "返回挂入的纹理句柄");
		assert_eq!(render::stub::dst_pixels(), img.pixels(), "输出像素一致");

		// dummy 输出纹理 → NotFound。
		render::stub::set_dummy(0xA1, true);
		assert!(matches!(
			clip.store_output_image(&img),
			Err(oakplugin::error::Error::NotFound)
		));
		render::stub::set_dummy(0xA1, false);

		// 帧尺寸与图像不一致 → 明确失败。
		render::stub::setup_dst(16, 4, PIXEL_FORMAT_F32);
		assert!(matches!(
			clip.store_output_image(&img),
			Err(oakplugin::error::Error::Failed(_))
		));

		// 非 F32 输出帧 → 明确失败。
		render::stub::setup_dst(8, 4, PIXEL_FORMAT_U8);
		assert!(matches!(
			clip.store_output_image(&img),
			Err(oakplugin::error::Error::Failed(_))
		));
	}
}

/// undo 打包：一次编辑事务（paramEditBegin/End）内的多次参数
/// 修改合并为一条命令。
#[test]
fn edit_transaction_coalescing() {
	common::with_host(|| {
		let Some((_inst, gain_h)) = bound_inst(42) else {
			return;
		};
		let s = param_suite();

		#[cfg(feature = "test-stubs")]
		{
			use oakplugin::bridge::{node, undo};
			node::stub::reset();
			undo::stub::reset();
			node::stub::register_node(42);
			node::stub::set_input(42, "gain", node::Value::float(0.0));

			// 编辑事务内两次回写 → 一条 multi。
			assert_eq!(unsafe { (s.param_edit_begin)(gain_h) }, OK);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.25) }, OK);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.5) }, OK);
			assert_eq!(unsafe { (s.param_edit_end)(gain_h) }, OK);

			let recs = undo::stub::records();
			let multi = recs.iter().find(|r| r.is_multi).expect("存在 multi 命令");
			assert_eq!(multi.children.len(), 2, "两次修改合并为一条 multi");
			for c in &multi.children {
				let child = recs.iter().find(|r| r.id == *c).expect("子命令在表");
				assert!(!child.is_multi);
				assert!(child.applied, "子命令已 redo 生效");
				assert_eq!(child.node, 42);
				assert_eq!(child.input, "gain");
			}
			// 值即时生效（事务内子命令 redo；editEnd 的 multi redo 幂等）。
			assert_eq!(node::stub::input(42, "gain").unwrap().value.f[0], 0.5);

			// 事务外回写 → 独立单命令（不并入 multi）。
			let before = undo::stub::records().len();
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.75) }, OK);
			let after = undo::stub::records().len();
			assert_eq!(after, before + 1, "事务外每次回写一条独立命令");
			let last = undo::stub::last_command().unwrap();
			assert!(!last.is_multi);
			assert_eq!(node::stub::input(42, "gain").unwrap().value.f[0], 0.75);

			// 嵌套 editBegin/End：内层结束不提交（multi 保留未 free），
			// 最外层结束才提交（redo + free）。
			assert_eq!(unsafe { (s.param_edit_begin)(gain_h) }, OK);
			assert_eq!(unsafe { (s.param_edit_begin)(gain_h) }, OK);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 1.0) }, OK);
			assert_eq!(unsafe { (s.param_edit_end)(gain_h) }, OK);
			let newest_multi = || {
				undo::stub::records()
					.iter()
					.filter(|r| r.is_multi)
					.max_by_key(|r| r.id)
					.cloned()
					.expect("存在 multi")
			};
			let m = newest_multi();
			assert_eq!(m.children.len(), 1, "事务内回写并入新 multi");
			assert!(!m.freed, "内层 editEnd 不提交");
			assert_eq!(unsafe { (s.param_edit_end)(gain_h) }, OK);
			assert!(newest_multi().freed, "最外层 editEnd 提交（redo+free）");
			assert_eq!(node::stub::input(42, "gain").unwrap().value.f[0], 1.0);
		}

		#[cfg(not(feature = "test-stubs"))]
		{
			// 无桥：editBegin/End 与回写 no-op；参数值本地生效。
			assert_eq!(unsafe { (s.param_edit_begin)(gain_h) }, OK);
			assert_eq!(unsafe { (s.param_set_value)(gain_h, 0.25) }, OK);
			assert_eq!(unsafe { (s.param_edit_end)(gain_h) }, OK);
			let mut out = 0.0;
			assert_eq!(unsafe { (s.param_get_value)(gain_h, &mut out) }, OK);
			assert_eq!(out, 0.25);
		}
	});
}

/// 系统级冒烟：真实 OFX 插件 bundle（/Library/OFX/Plugins/
/// Misc.ofx.bundle，本机安装的标准 openfx-misc）在存在时扫描 →
/// describe → create_instance → 协商；absent 时 skip。
///
/// 这是 0 期 golden master 的轻量替代（0 期基建可能尚未落地——见
/// README「与 M11 §3.5 的对照」）：只验"能加载、能建实例、协商
/// 有结果"，不比对快照。
#[test]
fn system_misc_ofx_bundle_smoke() {
	let bundle = std::path::Path::new("/Library/OFX/Plugins/Misc.ofx.bundle");
	if !bundle.is_dir() {
		common::skip("/Library/OFX/Plugins/Misc.ofx.bundle 不存在");
		return;
	}
	common::with_host(|| {
		use oakplugin::host::Host;
		let host = Host::global();
		// 只扫 Misc：临时目录内建软链（scan_path 会 canonicalize）。
		let tmp = std::env::temp_dir().join(format!("oak-misc-scan-{}", std::process::id()));
		let link = tmp.join("Misc.ofx.bundle");
		if !link.exists() {
			std::fs::create_dir_all(&tmp).ok();
			std::os::unix::fs::symlink(bundle, &link).expect("建软链");
		}
		host.cache.scan_path(&tmp).expect("扫描 Misc.ofx.bundle");
		std::fs::remove_dir_all(&tmp).ok();

		// bundle 内的插件已 describe 入缓存。
		let ids: Vec<String> = (0..host.cache.count())
			.filter_map(|i| host.cache.at(i).map(|p| p.identifier.clone()))
			.collect();
		if ids.is_empty() {
			// 本机 bundle 若为 Natron 分支构建（describe 返回
			// MissingHostFeature），扫描必然为空——README 已记录该
			// 不兼容；兼容 bundle 的机器上此处照常断言。
			common::skip("/Library/OFX/Plugins/Misc.ofx.bundle 为 Natron 分支构建（describe 返回 MissingHostFeature）或空 bundle");
			return;
		}
		println!("Misc.ofx.bundle 扫描到 {} 个插件：{ids:?}", ids.len());

		// create_instance 冒烟（首个插件的首选上下文）。
		let first = host.cache.at(0).expect("首个插件").identifier.clone();
		let inst = host.create_instance(&first, None);
		assert!(inst.is_ok(), "create_instance({first}) 应成功");
		// 协商冒烟（输出 clip 的分量/位深/帧率应返回）。
		let inst = inst.unwrap();
		let prefs = inst.value.get_clip_preferences();
		assert!(
			prefs.is_ok(),
			"getClipPreferences({first}) 应成功: {prefs:?}"
		);
	});
}
