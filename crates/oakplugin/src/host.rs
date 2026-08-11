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

//! Host 单例与插件缓存。
//!
//! 对应 C++ 的 `OliveHost` + HostSupport 的 `PluginCache`。职责：
//! bundle 扫描、`dlopen` 插件、调 `setHost`/`load`/`describe`
//! 建立 [`Plugin`] 记录、按标识创建实例。参照：
//! HS: ofxhPluginCache.cpp（扫描去重与索引语义）、
//! HS: ofxhImageEffectAPI.cpp:150-238（load/describe/
//! describeInContext 的 action 序列）。
//!
//! ## 扫描语义（对照 olivehost.cpp:118-191）
//!
//! 默认路径集：`$HOME/.OFX/Plugins`、`$HOME/.local/share/OFX/Plugins`、
//! `$HOME/.local/share/olive/ofx/Plugins`、`../OFX/Plugins`、
//! `../share/olive/ofx/Plugins`、`../lib/olive/ofx/Plugins`，以及
//! `OLIVE_OFX_PLUGIN_PATH`/`OLIVE_PLUGIN_PATH`/`OFX_PLUGIN_PATH`
//! 环境变量（':' 分隔）；重复扫描按路径去重。
//! `// [P2]`：Info.plist 解析（CFBundleExecutable）不做——bundle 内
//! 二进制以探测方式定位（`Contents/MacOS/*`、`Contents/Linux-*/*`、
//! 根下 `*.so`/`*.dylib`）。
//!
//! ## dlopen（零依赖）
//!
//! 直接声明 `dlopen`/`dlsym`/`dlclose`（macOS libSystem / Linux libc
//! 内建；RTLD 常量按平台取值）。加载顺序按 ofxCore.h:574：
//! `OfxGetNumberOfPlugins` → `OfxGetPlugin`，随后
//! `OfxPlugin::setHost`（mandatory，ofxCore.h:124-132）→
//! `load` → `describe`（HS: ofxhImageEffectAPI.cpp:158-190）。

use std::ffi::{c_char, c_int, c_uint, c_void, CStr, CString};
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};

use crate::descriptor::EffectDescriptor;
use crate::handle::{RefBox, Registry};
use crate::instance::Instance;
use crate::property::{PropertySet, Value};
use crate::suites::status;

// ---- dlopen FFI（零依赖；RTLD 常量按平台）----

#[cfg(target_os = "macos")]
const RTLD_NOW: c_int = 0x2;
#[cfg(target_os = "linux")]
const RTLD_NOW: c_int = 0x2;
#[cfg(target_os = "macos")]
const RTLD_LOCAL: c_int = 0x4;
#[cfg(target_os = "linux")]
const RTLD_LOCAL: c_int = 0x0;

extern "C" {
	fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
	fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
	fn dlclose(handle: *mut c_void) -> c_int;
}

/// 动态加载共享库；失败返回 None。
fn dl_open(path: &Path) -> Option<*mut c_void> {
	let c = CString::new(path.to_str()?).ok()?;
	let h = unsafe { dlopen(c.as_ptr(), RTLD_NOW | RTLD_LOCAL) };
	if h.is_null() {
		None
	} else {
		Some(h)
	}
}

/// 查符号；失败返回 None。
fn dl_sym(handle: *mut c_void, name: &str) -> Option<*mut c_void> {
	let c = CString::new(name).ok()?;
	let p = unsafe { dlsym(handle, c.as_ptr()) };
	if p.is_null() {
		None
	} else {
		Some(p)
	}
}

/// 从 `dlsym` 结果取函数指针（libloading 同款转换；调用方保证符号
/// 类型正确）。
///
/// # Safety
/// 符号必须确实是该函数类型。
unsafe fn dlsym_fn<T>(handle: *mut c_void, name: &str) -> Option<T> {
	let p = dl_sym(handle, name)?;
	Some(unsafe { std::mem::transmute_copy(&p) })
}

// ---- OFX 宿主侧结构（ofxCore.h，字段序与 SDK 逐字一致）----

/// `OfxHost`（ofxCore.h:44）。
#[repr(C)]
struct OfxHost {
	/// 宿主属性集句柄（`Host::props` 的裸指针，标签 0）。
	host: *mut c_void,
	/// fetchSuite。
	fetch_suite: unsafe extern "C" fn(*mut c_void, *const c_char, c_int) -> *const c_void,
}

/// `OfxPlugin`（ofxCore.h:96）。
#[repr(C)]
struct OfxPlugin {
	plugin_api: *const c_char,
	api_version: c_int,
	plugin_identifier: *const c_char,
	plugin_version_major: c_uint,
	plugin_version_minor: c_uint,
	set_host: Option<unsafe extern "C" fn(*mut OfxHost)>,
	main_entry: Option<
		unsafe extern "C" fn(*const c_char, *const c_void, *mut c_void, *mut c_void) -> c_int,
	>,
}

/// fetchSuite 的宿主出口（插件经 `OfxHost::fetchSuite` 拿到；
/// 经 [`crate::suites::fetch_suite`] 分发表）。
unsafe extern "C" fn host_fetch_suite(
	host: *mut c_void,
	name: *const c_char,
	version: c_int,
) -> *const c_void {
	let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
		if host.is_null() || name.is_null() {
			return None;
		}
		let name = unsafe { CStr::from_ptr(name) }.to_str().ok()?;
		crate::suites::fetch_suite(name, version)
	}));
	match result {
		Ok(Some(p)) => p,
		_ => std::ptr::null(),
	}
}

// ---- 动作与属性常量（ofxCore.h / ofxImageEffect.h）----

/// kOfxActionLoad。
pub(crate) const ACTION_LOAD: &str = "OfxActionLoad";
/// kOfxActionUnload。
pub(crate) const ACTION_UNLOAD: &str = "OfxActionUnload";
/// kOfxActionDescribe。
pub(crate) const ACTION_DESCRIBE: &str = "OfxActionDescribe";
/// kOfxActionCreateInstance。
pub(crate) const ACTION_CREATE_INSTANCE: &str = "OfxActionCreateInstance";
/// kOfxActionDestroyInstance。
pub(crate) const ACTION_DESTROY_INSTANCE: &str = "OfxActionDestroyInstance";
/// kOfxImageEffectActionDescribeInContext。
pub(crate) const ACTION_DESCRIBE_IN_CONTEXT: &str = "OfxImageEffectActionDescribeInContext";
/// kOfxImageEffectActionGetClipPreferences。
pub(crate) const ACTION_GET_CLIP_PREFERENCES: &str = "OfxImageEffectActionGetClipPreferences";
/// kOfxImageEffectActionGetRegionOfDefinition。
pub(crate) const ACTION_GET_ROD: &str = "OfxImageEffectActionGetRegionOfDefinition";
/// kOfxImageEffectActionGetRegionsOfInterest。
pub(crate) const ACTION_GET_ROI: &str = "OfxImageEffectActionGetRegionsOfInterest";
/// kOfxImageEffectActionIsIdentity。
pub(crate) const ACTION_IS_IDENTITY: &str = "OfxImageEffectActionIsIdentity";
/// kOfxImageEffectActionRender。
pub(crate) const ACTION_RENDER: &str = "OfxImageEffectActionRender";
/// kOfxImageEffectActionBeginSequenceRender。
pub(crate) const ACTION_BEGIN_SEQUENCE: &str = "OfxImageEffectActionBeginSequenceRender";
/// kOfxImageEffectActionEndSequenceRender。
pub(crate) const ACTION_END_SEQUENCE: &str = "OfxImageEffectActionEndSequenceRender";
/// kOfxActionOpenGLContextAttached（ofxGPURender.h:345）。
pub(crate) const ACTION_GL_CONTEXT_ATTACHED: &str = "OfxActionOpenGLContextAttached";
/// kOfxActionOpenGLContextDetached（ofxGPURender.h:371；宏值自带
/// "kOfx" 前缀，是规范原文）。
pub(crate) const ACTION_GL_CONTEXT_DETACHED: &str = "kOfxActionOpenGLContextDetached";
/// kOfxImageEffectActionGetOutputColourspace（ofxColour.h:283）。
pub(crate) const ACTION_GET_OUTPUT_COLOURSPACE: &str = "OfxImageEffectActionGetOutputColourspace";

/// kOfxPropTime（ofxCore.h:613）。
pub(crate) const PROP_TIME: &str = "OfxPropTime";
/// kOfxImageEffectPropRenderScale。
pub(crate) const PROP_RENDER_SCALE: &str = "OfxImageEffectPropRenderScale";
/// kOfxImageEffectPropRenderWindow。
pub(crate) const PROP_RENDER_WINDOW: &str = "OfxImageEffectPropRenderWindow";
/// kOfxImageEffectPropRegionOfInterest。
pub(crate) const PROP_ROI: &str = "OfxImageEffectPropRegionOfInterest";
/// kOfxImageEffectPropRegionOfDefinition。
pub(crate) const PROP_ROD: &str = "OfxImageEffectPropRegionOfDefinition";
/// kOfxImageEffectPropIsIdentity。
pub(crate) const PROP_IS_IDENTITY: &str = "OfxImageEffectPropIsIdentity";
/// kOfxImageEffectPropFieldToRender。
pub(crate) const PROP_FIELD_TO_RENDER: &str = "OfxImageEffectPropFieldToRender";
/// kOfxImageEffectPropSequentialRenderStatus。
pub(crate) const PROP_SEQUENTIAL_RENDER: &str = "OfxImageEffectPropSequentialRenderStatus";
/// kOfxImageEffectPropInteractiveRenderStatus。
pub(crate) const PROP_INTERACTIVE_RENDER: &str = "OfxImageEffectPropInteractiveRenderStatus";
/// kOfxImageEffectPropRenderQualityDraft。
pub(crate) const PROP_RENDER_QUALITY_DRAFT: &str = "OfxImageEffectPropRenderQualityDraft";
/// kOfxImageEffectPropNoSpatialAwareness。
pub(crate) const PROP_NO_SPATIAL_AWARENESS: &str = "OfxImageEffectPropNoSpatialAwareness";
/// kOfxImageEffectPropFrameRange。
pub(crate) const PROP_FRAME_RANGE: &str = "OfxImageEffectPropFrameRange";
/// kOfxImageEffectPropFrameStep。
pub(crate) const PROP_FRAME_STEP: &str = "OfxImageEffectPropFrameStep";
/// kOfxImageEffectPropFrameRate。
pub(crate) const PROP_FRAME_RATE: &str = "OfxImageEffectPropFrameRate";
/// kOfxImageEffectPropPreMultiplication。
pub(crate) const PROP_PREMULT: &str = "OfxImageEffectPropPreMultiplication";
/// kOfxImageClipPropFieldOrder。
pub(crate) const PROP_FIELD_ORDER: &str = "OfxImageClipPropFieldOrder";
/// kOfxImageClipPropContinuousSamples。
pub(crate) const PROP_CONTINUOUS_SAMPLES: &str = "OfxImageClipPropContinuousSamples";
/// kOfxImageEffectFrameVarying。
pub(crate) const PROP_FRAME_VARYING: &str = "OfxImageEffectFrameVarying";
/// kOfxImageClipPropConnected。
pub(crate) const PROP_CLIP_CONNECTED: &str = "OfxImageClipPropConnected";
/// kOfxImageEffectPropContext。
pub(crate) const PROP_CONTEXT: &str = "OfxImageEffectPropContext";
/// kOfxImageEffectPropSupportedContexts。
pub(crate) const PROP_SUPPORTED_CONTEXTS: &str = "OfxImageEffectPropSupportedContexts";
/// kOfxImageEffectPropProjectSize。
pub(crate) const PROP_PROJECT_SIZE: &str = "OfxImageEffectPropProjectSize";
/// kOfxImageEffectPropProjectOffset。
pub(crate) const PROP_PROJECT_OFFSET: &str = "OfxImageEffectPropProjectOffset";
/// kOfxImageEffectPropProjectExtent。
pub(crate) const PROP_PROJECT_EXTENT: &str = "OfxImageEffectPropProjectExtent";
/// kOfxImageEffectPropProjectPixelAspectRatio（宏值即 PixelAspectRatio）。
pub(crate) const PROP_PROJECT_PAR: &str = "OfxImageEffectPropPixelAspectRatio";
/// kOfxImageEffectInstancePropEffectDuration。
pub(crate) const PROP_EFFECT_DURATION: &str = "OfxImageEffectInstancePropEffectDuration";
/// kOfxImageEffectInstancePropSequentialRender。
pub(crate) const PROP_SEQUENTIAL: &str = "OfxImageEffectInstancePropSequentialRender";
/// kOfxImageEffectPropPluginHandle。
pub(crate) const PROP_PLUGIN_HANDLE: &str = "OfxImageEffectPropPluginHandle";
/// kOfxPluginPropFilePath。
pub(crate) const PROP_PLUGIN_FILE_PATH: &str = "OfxPluginPropFilePath";
/// kOfxImageEffectPropSupportsTiles。
pub(crate) const PROP_SUPPORTS_TILES: &str = "OfxImageEffectPropSupportsTiles";
/// kOfxPropType。
pub(crate) const PROP_TYPE: &str = "OfxPropType";
/// kOfxPropLabel。
pub(crate) const PROP_LABEL: &str = "OfxPropLabel";

// ---- GL 能力与协商属性（ofxGPURender.h，M11 第 2 期）----

/// kOfxImageEffectPropOpenGLRenderSupported（ofxGPURender.h:62）：
/// 宿主描述符（"true"）与插件描述符（"false"/"true"/"needed"）。
pub(crate) const PROP_GL_RENDER_SUPPORTED: &str = "OfxImageEffectPropOpenGLRenderSupported";
/// kOfxImageEffectPropOpenGLEnabled（ofxGPURender.h:117）：Render/
/// Begin/EndSequenceRender 的 in args。
pub(crate) const PROP_GL_ENABLED: &str = "OfxImageEffectPropOpenGLEnabled";
/// kOfxOpenGLPropPixelDepth（ofxGPURender.h:89）：插件描述符的 GL
/// 渲染支持位深（可选；格式协商的宿主侧输入）。
pub(crate) const PROP_GL_PIXEL_DEPTH: &str = "OfxOpenGLPropPixelDepth";

// ---- ofxColour 属性（ofxColour.h，M11 第 2 期）----

/// kOfxImageEffectPropColourManagementStyle（ofxColour.h:60）。
pub(crate) const PROP_COLOUR_STYLE: &str = "OfxImageEffectPropColourManagementStyle";
/// kOfxImageEffectColourManagementOCIO（ofxColour.h:73）：宿主声明的
/// 色彩管理模式（任务要求）。
pub(crate) const COLOUR_STYLE_OCIO: &str = "OfxImageEffectColourManagementOCIO";
/// kOfxImageEffectPropColourManagementAvailableConfigs（ofxColour.h:85）。
pub(crate) const PROP_COLOUR_AVAILABLE_CONFIGS: &str =
	"OfxImageEffectPropColourManagementAvailableConfigs";
/// kOfxImageEffectPropColourManagementConfig（ofxColour.h:98）：实例期
/// 协商出的 native 配置。
pub(crate) const PROP_COLOUR_CONFIG: &str = "OfxImageEffectPropColourManagementConfig";
/// kOfxImageEffectPropOCIOConfig（ofxColour.h:110）：实例期 OCIO 配置
/// 路径/URI。
pub(crate) const PROP_OCIO_CONFIG: &str = "OfxImageEffectPropOCIOConfig";
/// kOfxImageClipPropColourspace（ofxColour.h:142）：clip 实际色彩空间。
pub(crate) const PROP_CLIP_COLOURSPACE: &str = "OfxImageClipPropColourspace";
/// kOfxImageClipPropPreferredColourspaces（ofxColour.h:201）：clip 偏好
/// 色彩空间（GetClipPreferences 的 out args 可写）。
pub(crate) const PROP_CLIP_PREFERRED_COLOURSPACES: &str = "OfxImageClipPropPreferredColourspaces";
/// 工作空间（任务要求：全链路 ACEScg）。
pub(crate) const WORKING_COLOURSPACE: &str = "ACEScg";
/// 宿主支持的 native 色彩配置标识（ofxColour.h:78 的唯一现行值）。
pub(crate) const NATIVE_CONFIG_ID: &str = "ofx-native-v1.5_aces-v1.3_ocio-v2.3";
/// 宿主 OCIO 配置 URI（ofxColour.h:110 允许 ocio:// 内建配置；本宿主
/// 无自有 config 文件，用 OCIO 内建默认配置并以 clip 色彩空间属性
/// 传达 ACEScg 工作空间）。
pub(crate) const OCIO_CONFIG_URI: &str = "ocio://default";

/// 协商期 per-clip 约定名（HS: "OfxImageClipPropComponents_<clip>"
/// 等，ofxhImageEffect.cpp:1725-1727）。
pub(crate) fn clip_pref_prop(prefix: &str, clip: &str) -> String {
	format!("{prefix}_{clip}")
}

/// 组件名前缀。
pub(crate) const CLIP_PREF_COMPONENTS: &str = "OfxImageClipPropComponents";
/// 位深名前缀。
pub(crate) const CLIP_PREF_DEPTH: &str = "OfxImageClipPropDepth";
/// 像素比名前缀。
pub(crate) const CLIP_PREF_PAR: &str = "OfxImageClipPropPAR";

fn cs(s: &str) -> CString {
	CString::new(s).unwrap()
}

// ---- Plugin --------------------------------------------------------------

/// 一个已加载插件（bundle 内的一个 effect 入口）。
pub struct Plugin {
	/// 插件标识（OfxPlugin::pluginIdentifier，如 "net.sf.cimg.CImgInvert"）。
	pub identifier: String,
	/// 版本号（major/minor）。
	pub version: (u32, u32),
	/// bundle 路径。
	pub bundle_path: PathBuf,
	/// describe 得到的支持上下文（filter/generator/transition）。
	pub contexts: Vec<String>,
	/// describe 产物（根描述符）。
	pub descriptor: EffectDescriptor,
	/// dlopen 句柄（公开字段供测试构造假插件）。
	pub lib: *mut c_void,
	/// 插件入口函数 `OfxPluginEntryPoint`（公开字段供测试构造；
	/// 签名与 SDK ofxCore.h:84 逐字一致——handle 是 `const void*`）。
	pub entry: unsafe extern "C" fn(
		action: *const c_char,
		handle: *const c_void,
		in_args: *mut c_void,
		out_args: *mut c_void,
	) -> i32,
	/// OfxPlugin 结构指针（kOfxImageEffectPropPluginHandle 属性用；
	/// 公开：测试构造假插件需要）。
	pub ofx_plugin: *mut c_void,
}

// dlopen 句柄与 OfxPlugin 指针是**不透明令牌**（只经 dlsym/插件
// 结构字段访问，不直接解引用）；跨线程搬运句柄是宿主分发语义。
// 与 property.rs 的 Value::Pointer 同理。
unsafe impl Send for Plugin {}
unsafe impl Sync for Plugin {}

impl Plugin {
	/// 调插件的 action。`handle` 视 action 而定（describe 时为
	/// descriptor，render 时为 instance）。`in_args`/`out_args` 按
	/// OFX 规范组装属性集。
	///
	/// 返回 OFX 状态码（kOfxStatOK == 0）。任何插件侧异常/崩溃信号
	/// 无法捕获——FFI 纪律只兜 Rust 侧 panic。
	///
	/// # Safety
	/// `handle`/参数集必须与 action 匹配（规范约定）。
	pub(crate) unsafe fn call_action(
		&self,
		action: &str,
		handle: *mut c_void,
		in_args: &PropertySet,
		out_args: &PropertySet,
	) -> i32 {
		// 属性集经裸指针（标签 0）传给插件（property suite 接受）。
		let in_ptr = in_args as *const PropertySet as *mut c_void;
		let out_ptr = out_args as *const PropertySet as *mut c_void;
		let action = cs(action);
		unsafe { (self.entry)(action.as_ptr(), handle as *const c_void, in_ptr, out_ptr) }
	}
}

// ---- PluginCache ---------------------------------------------------------

/// 一个已加载二进制（dlopen 句柄；多个 Plugin 可共享一个二进制）。
struct LoadedBinary {
	handle: *mut c_void,
	bundle_path: PathBuf,
}

impl Drop for LoadedBinary {
	fn drop(&mut self) {
		unsafe { dlclose(self.handle) };
	}
}

// 同 Plugin：dlopen 句柄为不透明令牌。
unsafe impl Send for LoadedBinary {}
unsafe impl Sync for LoadedBinary {}

/// 插件缓存（进程单例，见 [`Host::global`]）。
pub struct PluginCache {
	/// 已加载插件（identifier 唯一）。
	plugins: Mutex<Vec<Arc<Plugin>>>,
	/// 已扫描的搜索路径（防重复扫描）。
	scanned_paths: Mutex<Vec<PathBuf>>,
	/// 已加载二进制（dlclose 责任；shutdown 时释放）。
	binaries: Mutex<Vec<LoadedBinary>>,
}

/// bundle 目录判定：目录名以 `.bundle` 或 `.plugin` 结尾。
fn is_bundle_dir(path: &Path) -> bool {
	let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
		return false;
	};
	name.ends_with(".bundle") || name.ends_with(".plugin")
}

/// 在 bundle 目录内定位二进制（探测式；`// [P2]` 起读 Info.plist 的
/// CFBundleExecutable）。
fn find_binary_in_bundle(bundle: &Path) -> Option<PathBuf> {
	let contents = bundle.join("Contents");
	for platform_dir in [
		contents.join("MacOS"),
		contents.join("Linux-x86-64"),
		contents.join("Linux-aarch64"),
	] {
		if let Ok(entries) = std::fs::read_dir(&platform_dir) {
			for e in entries.flatten() {
				let p = e.path();
				if p.is_file() && !p.extension().is_some_and(|x| x == "plist") {
					return Some(p);
				}
			}
		}
	}
	// 退而求其次：bundle 根下的 .so/.dylib/.ofx。
	if let Ok(entries) = std::fs::read_dir(bundle) {
		for e in entries.flatten() {
			let p = e.path();
			let ext = p.extension().and_then(|x| x.to_str()).unwrap_or("");
			if p.is_file() && (ext == "so" || ext == "dylib" || ext == "ofx") {
				return Some(p);
			}
		}
	}
	None
}

impl PluginCache {
	/// 扫描标准目录（M9 行为：环境变量 + 用户/系统默认路径集，顺序
	/// 与 olivehost.cpp:118-191 一致）。重复调用是 no-op（按路径
	/// 去重）。
	pub fn scan(&self) -> crate::error::Result<()> {
		let home = std::env::var("HOME").ok().unwrap_or_default();
		let mut paths = Vec::new();
		if !home.is_empty() {
			paths.push(PathBuf::from(&home).join(".OFX/Plugins"));
			paths.push(PathBuf::from(&home).join(".local/share/OFX/Plugins"));
			paths.push(PathBuf::from(&home).join(".local/share/olive/ofx/Plugins"));
		}
		paths.push(PathBuf::from("../OFX/Plugins"));
		paths.push(PathBuf::from("../share/olive/ofx/Plugins"));
		paths.push(PathBuf::from("../lib/olive/ofx/Plugins"));
		for var in [
			"OLIVE_OFX_PLUGIN_PATH",
			"OLIVE_PLUGIN_PATH",
			"OFX_PLUGIN_PATH",
		] {
			if let Ok(raw) = std::env::var(var) {
				paths.extend(raw.split(':').filter(|p| !p.is_empty()).map(PathBuf::from));
			}
		}
		for p in paths {
			self.scan_path(&p)?;
		}
		Ok(())
	}

	/// 追加一个自定义搜索目录并扫描之（递归至深度 3；按路径去重）。
	pub fn scan_path(&self, path: &std::path::Path) -> crate::error::Result<()> {
		// 规范化去重（HS: addFileToPath 的 weakly_canonical）。
		let canonical = std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
		{
			let mut seen = self.scanned_paths.lock().unwrap_or_else(|e| e.into_inner());
			if seen.iter().any(|p| p == &canonical) {
				return Ok(());
			}
			seen.push(canonical.clone());
		}
		if !canonical.is_dir() {
			return Ok(());
		}
		let mut bundles = Vec::new();
		walk_dirs(&canonical, 0, 3, &mut bundles);
		for bundle in bundles {
			self.load_bundle(&bundle);
		}
		Ok(())
	}

	/// 加载一个 bundle（幂等：按 bundle 路径去重）。
	fn load_bundle(&self, bundle: &Path) {
		let Some(binary) = find_binary_in_bundle(bundle) else {
			return;
		};
		let Some(handle) = dl_open(&binary) else {
			return;
		};
		{
			let bins = self.binaries.lock().unwrap_or_else(|e| e.into_inner());
			if bins.iter().any(|b| b.bundle_path == bundle) {
				unsafe { dlclose(handle) };
				return;
			}
		}
		let plugins = unsafe { self.collect_plugins(handle, bundle) };
		if plugins.is_empty() {
			unsafe { dlclose(handle) };
			return;
		}
		let mut plugs = self.plugins.lock().unwrap_or_else(|e| e.into_inner());
		let mut bins = self.binaries.lock().unwrap_or_else(|e| e.into_inner());
		for p in plugins {
			// identifier 去重（后加载者忽略，HS 语义）。
			if !plugs.iter().any(|e| e.identifier == p.identifier) {
				plugs.push(p);
			}
		}
		bins.push(LoadedBinary {
			handle,
			bundle_path: bundle.to_path_buf(),
		});
	}

	/// 从已加载二进制收集插件（setHost → load → describe）。
	///
	/// # Safety
	/// `handle` 必须是有效 dlopen 句柄。
	unsafe fn collect_plugins(&self, handle: *mut c_void, bundle: &Path) -> Vec<Arc<Plugin>> {
		let get_no: unsafe extern "C" fn() -> c_int =
			match unsafe { dlsym_fn(handle, "OfxGetNumberOfPlugins") } {
				Some(f) => f,
				None => {
					return Vec::new();
				}
			};
		let get_plug: unsafe extern "C" fn(c_int) -> *mut OfxPlugin =
			match unsafe { dlsym_fn(handle, "OfxGetPlugin") } {
				Some(f) => f,
				None => {
					return Vec::new();
				}
			};
		let count = unsafe { get_no() };
		if count <= 0 {
			return Vec::new();
		}
		let mut out = Vec::new();
		for i in 0..count {
			let ofx = unsafe { get_plug(i) };
			if ofx.is_null() {
				continue;
			}
			if let Some(p) = unsafe { self.build_plugin(ofx, handle, bundle) } {
				out.push(p);
			}
		}
		out
	}

	/// 读 OfxPlugin 头部并跑 setHost/load/describe。
	///
	/// # Safety
	/// `ofx` 指向插件导出的有效 OfxPlugin 结构。
	unsafe fn build_plugin(
		&self,
		ofx: *mut OfxPlugin,
		lib: *mut c_void,
		bundle: &Path,
	) -> Option<Arc<Plugin>> {
		let ofx_ref = unsafe { &*ofx };
		// API 匹配（kOfxImageEffectPluginApi "OfxImageEffectPluginAPI" v1，
		// ofxImageEffect.h:28-32）。
		let api = if ofx_ref.plugin_api.is_null() {
			return None;
		} else {
			unsafe { CStr::from_ptr(ofx_ref.plugin_api) }
				.to_str()
				.ok()?
		};
		if api != "OfxImageEffectPluginAPI" || ofx_ref.api_version != 1 {
			return None;
		}
		let identifier = unsafe { CStr::from_ptr(ofx_ref.plugin_identifier) }
			.to_str()
			.ok()?
			.to_string();
		let entry = ofx_ref.main_entry?;

		// setHost 是 mandatory 的第一个调用（ofxCore.h:124-132）。
		let host = Host::global();
		let mut ofx_host = OfxHost {
			host: &host.props as *const PropertySet as *mut c_void,
			fetch_suite: host_fetch_suite,
		};
		if let Some(f) = ofx_ref.set_host {
			unsafe { f(&mut ofx_host) };
		}

		let mut plugin = Plugin {
			identifier,
			version: (ofx_ref.plugin_version_major, ofx_ref.plugin_version_minor),
			bundle_path: bundle.to_path_buf(),
			contexts: Vec::new(),
			descriptor: EffectDescriptor::new(),
			lib,
			entry,
			ofx_plugin: ofx as *mut c_void,
		};

		// 根描述符属性（HS: effectDescriptorStuff，ofxhImageEffect.cpp:133-155）。
		init_descriptor_props(&plugin.descriptor.props, bundle);

		let empty = PropertySet::new();

		// load（HS: ofxhImageEffectAPI.cpp:158-165；OK/ReplyDefault 接受）。
		let stat = unsafe { plugin.call_action(ACTION_LOAD, std::ptr::null_mut(), &empty, &empty) };
		if stat != status::OK && stat != status::REPLY_DEFAULT {
			return None;
		}

		// describe（handle = 打标描述符；HS: ofxhImageEffectAPI.cpp:173-180）。
		let desc_handle = crate::suites::tag::make(
			&plugin.descriptor.props as *const PropertySet,
			crate::suites::tag::DESCRIPTOR,
		);
		let stat = unsafe { plugin.call_action(ACTION_DESCRIBE, desc_handle, &empty, &empty) };
		if stat != status::OK && stat != status::REPLY_DEFAULT {
			return None;
		}

		// 支持上下文（describe 产物；HS 从 props 读）。
		let contexts = read_contexts(&plugin.descriptor.props);
		// 只收标准上下文。
		let contexts: Vec<String> = contexts
			.into_iter()
			.filter(|c| {
				matches!(
					c.as_str(),
					"OfxImageEffectContextFilter"
						| "OfxImageEffectContextGenerator"
						| "OfxImageEffectContextTransition"
				)
			})
			.collect();
		if contexts.is_empty() {
			return None;
		}
		plugin.contexts = contexts;
		Some(Arc::new(plugin))
	}

	/// 已加载插件数量。
	pub fn count(&self) -> usize {
		self.plugins.lock().unwrap_or_else(|e| e.into_inner()).len()
	}

	/// 第 `index` 个插件（扫描顺序稳定）。
	pub fn at(&self, index: usize) -> Option<Arc<Plugin>> {
		self.plugins
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.get(index)
			.cloned()
	}

	/// 按标识查找。
	pub fn find(&self, identifier: &str) -> Option<Arc<Plugin>> {
		self.plugins
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.iter()
			.find(|p| p.identifier == identifier)
			.cloned()
	}

	/// 卸载全部（shutdown 路径）。`scanned_paths` 一并清空——
	/// 否则再 init 后的扫描会被去重短路（缓存已空但路径仍在）。
	pub(crate) fn unload_all(&self) {
		self.plugins
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.clear();
		let mut bins = self.binaries.lock().unwrap_or_else(|e| e.into_inner());
		bins.clear(); // Drop → dlclose
		self.scanned_paths
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.clear();
	}
}

/// 递归枚举目录下的 bundle 目录（深度受限，防符号链接环）。
fn walk_dirs(dir: &Path, depth: usize, max_depth: usize, out: &mut Vec<PathBuf>) {
	if depth > max_depth {
		return;
	}
	let Ok(entries) = std::fs::read_dir(dir) else {
		return;
	};
	for e in entries.flatten() {
		let p = e.path();
		if !p.is_dir() {
			continue;
		}
		if is_bundle_dir(&p) {
			out.push(p);
		} else {
			walk_dirs(&p, depth + 1, max_depth, out);
		}
	}
}

/// 根描述符属性表（HS: effectDescriptorStuff 的子集，
/// ofxhImageEffect.cpp:133-155）。
fn init_descriptor_props(props: &PropertySet, bundle: &Path) {
	props.set_one(PROP_TYPE, Value::String(cs("OfxTypeImageEffect")));
	props.set_one(PROP_LABEL, Value::String(cs("")));
	props.set_one("OfxPropShortLabel", Value::String(cs("")));
	props.set_one("OfxPropLongLabel", Value::String(cs("")));
	props.define("OfxPropVersion", vec![Value::Int(0)]);
	props.set_one("OfxPropVersionLabel", Value::String(cs("")));
	props.set_one("OfxPropPluginDescription", Value::String(cs("")));
	props.define(PROP_SUPPORTED_CONTEXTS, vec![]);
	props.set_one("OfxImageEffectPluginPropGrouping", Value::String(cs("")));
	props.set_one("OfxImageEffectPluginPropSingleInstance", Value::Int(0));
	props.set_one(
		"OfxImageEffectPluginRenderThreadSafety",
		Value::String(cs("OfxImageEffectRenderInstanceSafe")),
	);
	props.set_one("OfxImageEffectPluginPropHostFrameThreading", Value::Int(1));
	props.set_one(
		"OfxImageEffectPluginPropOverlayInteractV1",
		Value::Pointer(std::ptr::null_mut()),
	);
	props.set_one("OfxImageEffectPropSupportsMultiResolution", Value::Int(1));
	props.set_one(PROP_SUPPORTS_TILES, Value::Int(1));
	props.set_one("OfxImageEffectPropTemporalClipAccess", Value::Int(0));
	props.define("OfxImageEffectPropSupportedPixelDepths", vec![]);
	props.set_one(
		"OfxImageEffectPluginPropFieldRenderTwiceAlways",
		Value::Int(1),
	);
	props.set_one(
		"OfxImageEffectPropSupportsMultipleClipDepths",
		Value::Int(0),
	);
	props.set_one("OfxImageEffectPropSupportsMultipleClipPARs", Value::Int(0));
	props.define("OfxImageEffectPropClipPreferencesSlaveParam", vec![]);
	props.set_one(PROP_SEQUENTIAL, Value::Int(0));
	props.set_one(
		PROP_PLUGIN_FILE_PATH,
		Value::String(cs(bundle.to_str().unwrap_or(""))),
	);
	// GL 能力声明（M11 §4；ofxGPURender.h）：描述符预定义
	// OpenGLRenderSupported（默认 "false"）与 kOfxOpenGLPropPixelDepth
	// （空数组）——插件在 describe 期经属性 suite 写入（HostSupport
	// 同款：propSet 不创建属性，宿主预定义属性宇宙）。
	props.set_one(PROP_GL_RENDER_SUPPORTED, Value::String(cs("false")));
	props.define(PROP_GL_PIXEL_DEPTH, vec![]);
	// ofxColour 描述符预定义（M11 §4；ofxColour.h）：style（默认
	// None）与可用配置（空数组），插件 describe 期声明自身能力。
	props.set_one(
		PROP_COLOUR_STYLE,
		Value::String(cs("OfxImageEffectColourManagementNone")),
	);
	props.define(PROP_COLOUR_AVAILABLE_CONFIGS, vec![]);
}

/// describe 后读支持上下文。
fn read_contexts(props: &PropertySet) -> Vec<String> {
	let mut out = Vec::new();
	let dim = props.dimension(PROP_SUPPORTED_CONTEXTS);
	for i in 0..dim {
		if let Some(Value::String(s)) = props.get(PROP_SUPPORTED_CONTEXTS, i) {
			out.push(s.to_string_lossy().into_owned());
		}
	}
	out
}

// ---- Host -----------------------------------------------------------------

/// 宿主。持有宿主级属性集（kOfxPropName/Version 与能力宣告属性）
/// 与插件缓存；是 suite 的注册与分发点。
pub struct Host {
	/// 宿主属性集（kOfxProp* 宿主级属性；能力宣告如
	/// kOfxImageEffectPropSupportedPixelDepths——协商的宿主侧输入）。
	pub props: PropertySet,
	/// 插件缓存。
	pub cache: PluginCache,
	/// 活跃实例注册表（泄漏断言与调试）。
	pub(crate) instances: Mutex<Vec<std::sync::Weak<RefBox<Instance>>>>,
}

/// 实例身份注册表（param 桥按身份反查；见 [`crate::handle::Registry`]）。
static INSTANCE_REGISTRY: OnceLock<Registry<Instance>> = OnceLock::new();

/// 实例身份注册表入口。
pub(crate) fn instance_registry() -> &'static Registry<Instance> {
	INSTANCE_REGISTRY.get_or_init(Registry::new)
}

impl Host {
	/// 进程单例。首次调用构建宿主属性集（能力宣告在此写入）。
	pub fn global() -> &'static Host {
		static HOST: OnceLock<Host> = OnceLock::new();
		HOST.get_or_init(|| {
			let props = PropertySet::new();
			init_host_props(&props);
			Host {
				props,
				cache: PluginCache {
					plugins: Mutex::new(Vec::new()),
					scanned_paths: Mutex::new(Vec::new()),
					binaries: Mutex::new(Vec::new()),
				},
				instances: Mutex::new(Vec::new()),
			}
		})
	}

	/// 按标识创建实例（describeInContext → createInstance）。
	/// 参照 HS: ofxhImageEffectAPI.cpp:200-238（describeInContext 的
	/// in-args 只有 kOfxImageEffectPropContext）与
	/// ofxhImageEffect.cpp:389-460（populate：clip/param 实例化）。
	/// `context` 为空用插件首个支持上下文。失败返回
	/// [`crate::error::Error::Failed`]/[`crate::error::Error::NotFound`]。
	pub fn create_instance(
		&self,
		identifier: &str,
		context: Option<&str>,
	) -> crate::error::Result<Arc<RefBox<Instance>>> {
		let plugin = self
			.cache
			.find(identifier)
			.ok_or(crate::error::Error::NotFound)?;

		let context = match context {
			Some(c) => c.to_string(),
			None => plugin
				.contexts
				.first()
				.cloned()
				.ok_or(crate::error::Error::Failed("插件无支持上下文".into()))?,
		};
		if !plugin.contexts.iter().any(|c| c == &context) {
			return Err(crate::error::Error::Failed(format!(
				"插件不支持上下文 {context}"
			)));
		}

		// describeInContext：上下文描述符 = 根描述符属性拷贝 +
		// 插件在 action 内重定义参数/clip。
		let mut ctx_desc = EffectDescriptor::new();
		ctx_desc.props = plugin.descriptor.props.clone();
		let in_args = PropertySet::new();
		in_args.set_one(PROP_CONTEXT, Value::String(cs(&context)));
		let desc_handle = crate::suites::tag::make(
			&ctx_desc.props as *const PropertySet,
			crate::suites::tag::DESCRIPTOR,
		);
		let stat = unsafe {
			plugin.call_action(ACTION_DESCRIBE_IN_CONTEXT, desc_handle, &in_args, &in_args)
		};
		if stat != status::OK && stat != status::REPLY_DEFAULT {
			return Err(crate::error::Error::Failed(format!(
				"describeInContext 失败：{stat}"
			)));
		}

		// populate（HS: Instance::populate：clip 实例 + param 实例，
		// ofxhImageEffect.cpp:389-460）。
		let clips = ctx_desc
			.clips
			.iter()
			.map(|c| Box::new(crate::clip::ClipInstance::from_descriptor(c.as_ref())))
			.collect();
		let params = crate::param::ParamSetInstance {
			params: ctx_desc
				.params
				.iter()
				.map(|d| Box::new(crate::param::ParamInstance::from_def((**d).clone())))
				.collect(),
		};

		let instance = Instance {
			props: PropertySet::new(),
			plugin,
			context: context.clone(),
			params,
			clips,
			node_identity: std::sync::atomic::AtomicUsize::new(0),
			destroyed: std::sync::atomic::AtomicBool::new(false),
			sequence_range: std::sync::Mutex::new(None),
			progress_cb: std::sync::Mutex::new(None),
			cancel: std::sync::atomic::AtomicBool::new(false),
			edit: std::sync::Mutex::new(crate::instance::EditTransaction::new()),
			render_lock: std::sync::Mutex::new(()),
		};
		init_instance_props(&instance.props, &instance);

		let arc = Arc::new(RefBox {
			refs: std::sync::atomic::AtomicU32::new(1),
			value: instance,
		});

		// createInstance action（HS: ofxhImageEffect.cpp:675-688）。
		let inst_handle = crate::suites::tag::make(
			&arc.value.props as *const PropertySet,
			crate::suites::tag::INSTANCE,
		);
		let empty = PropertySet::new();
		let stat = unsafe {
			arc.value
				.plugin
				.call_action(ACTION_CREATE_INSTANCE, inst_handle, &empty, &empty)
		};
		if stat != status::OK && stat != status::REPLY_DEFAULT {
			return Err(crate::error::Error::Failed(format!(
				"createInstance 失败：{stat}"
			)));
		}

		// 登记：实例表 + param→instance 回写表。
		self.instances
			.lock()
			.unwrap_or_else(|e| e.into_inner())
			.push(Arc::downgrade(&arc));
		let inst_props = &arc.value.props as *const PropertySet as usize;
		for p in &arc.value.params.params {
			let p_addr = &p.props as *const PropertySet as usize;
			crate::suites::param::register_param_owner(p_addr, inst_props);
		}
		instance_registry().register(&arc);

		Ok(arc)
	}

	/// 关闭宿主（`oakplugin_host_shutdown` 的宿主侧）：
	/// 1. 对全部活实例发 destroyInstance 通知（此刻插件仍加载）；
	/// 2. 打取消标记——shutdown 后仍被 C 侧持有的句柄不得再调
	///    已卸载的插件入口（render 入口即短路）；
	/// 3. 卸载插件与二进制。
	///
	/// 说明：C 侧句柄持有 Arc，宿主无法（也不应）强制析构——实例
	/// 内存随句柄释放；destroyed 标记保证 Drop 不再碰插件入口。
	pub fn shutdown(&self) {
		let list = self.instances.lock().unwrap_or_else(|e| e.into_inner());
		for w in list.iter() {
			if let Some(arc) = w.upgrade() {
				// 置 destroyed 标记后再通知：实例随后的 Drop 不得再
				// 调已卸载的插件入口（二次 destroyInstance → 悬垂）。
				arc.value
					.destroyed
					.store(true, std::sync::atomic::Ordering::Relaxed);
				arc.value.notify_destroy();
				arc.value
					.cancel
					.store(true, std::sync::atomic::Ordering::Relaxed);
			}
		}
		self.cache.unload_all();
	}

	/// 当前活跃实例数（泄漏断言）。
	pub fn alive_count(&self) -> usize {
		let mut list = self.instances.lock().unwrap_or_else(|e| e.into_inner());
		let alive = list.iter().filter(|w| w.strong_count() > 0).count();
		list.retain(|w| w.strong_count() > 0);
		alive
	}
}

/// 宿主属性集（对照 C++ OliveHost::OliveHost，olivehost.cpp:193-207：
/// Name/Label/Version；能力宣告为 phase 1 最小集，协商按需扩展）。
fn init_host_props(props: &PropertySet) {
	props.set_one("OfxPropName", Value::String(cs("Oak Video Editor")));
	props.set_one("OfxPropLabel", Value::String(cs("Oak Video Editor")));
	props.set_one(
		"OfxPropVersionLabel",
		Value::String(cs(env!("CARGO_PKG_VERSION"))),
	);
	props.define(
		"OfxPropVersion",
		vec![Value::Int(0), Value::Int(0), Value::Int(0)],
	);
	// 能力宣告：协商的宿主侧输入（phase 1 全链路 F32+RGBA）。
	props.define(
		"OfxImageEffectPropSupportedPixelDepths",
		vec![Value::String(cs("OfxBitDepthFloat"))],
	);
	props.define(
		"OfxImageEffectPropSupportedComponents",
		vec![
			Value::String(cs("OfxImageComponentRGBA")),
			Value::String(cs("OfxImageComponentRGB")),
			Value::String(cs("OfxImageComponentAlpha")),
		],
	);
	// GL 能力宣告（M11 §4；ofxGPURender.h "OpenGL House Keeping"：
	// 宿主在描述符置 "true"）。
	props.set_one(PROP_GL_RENDER_SUPPORTED, Value::String(cs("true")));
	// ofxColour 能力宣告（M11 §4）：OCIO 模式 + native 配置列表。
	props.set_one(PROP_COLOUR_STYLE, Value::String(cs(COLOUR_STYLE_OCIO)));
	props.define(
		PROP_COLOUR_AVAILABLE_CONFIGS,
		vec![Value::String(cs(NATIVE_CONFIG_ID))],
	);
}

/// 实例属性表（HS: effectInstanceStuff 的 phase 1 子集，
/// ofxhImageEffect.cpp:313-330）。
fn init_instance_props(props: &PropertySet, instance: &Instance) {
	props.set_one(PROP_TYPE, Value::String(cs("OfxTypeImageEffectInstance")));
	props.set_one(PROP_CONTEXT, Value::String(cs(&instance.context)));
	props.set_one(
		PROP_PLUGIN_HANDLE,
		Value::Pointer(instance.plugin.ofx_plugin),
	);
	props.define(
		PROP_PROJECT_SIZE,
		vec![Value::Double(1920.0), Value::Double(1080.0)],
	);
	props.define(
		PROP_PROJECT_OFFSET,
		vec![Value::Double(0.0), Value::Double(0.0)],
	);
	props.define(
		PROP_PROJECT_EXTENT,
		vec![Value::Double(1920.0), Value::Double(1080.0)],
	);
	props.set_one(PROP_PROJECT_PAR, Value::Double(1.0));
	props.set_one(PROP_EFFECT_DURATION, Value::Double(1.0));
	props.set_one(PROP_SEQUENTIAL, Value::Int(0));
	props.set_one(PROP_FRAME_RATE, Value::Double(24.0));
	props.set_one("OfxPropIsInteractive", Value::Int(0));
	props.set_one(PROP_SUPPORTS_TILES, Value::Int(1));
	// ofxColour 实例期协商结果（M11 §4；ofxColour.h：宿主必须在实例
	// 上设置 style/config/OCIOConfig）。
	props.set_one(PROP_COLOUR_STYLE, Value::String(cs(COLOUR_STYLE_OCIO)));
	props.set_one(PROP_COLOUR_CONFIG, Value::String(cs(NATIVE_CONFIG_ID)));
	props.set_one(PROP_OCIO_CONFIG, Value::String(cs(OCIO_CONFIG_URI)));
}
