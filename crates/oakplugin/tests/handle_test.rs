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

//! handle.rs 的契约测试：引用计数语义、free 容错、借用盒、Registry。
//!
//! 对应实现：crate::handle。每个测试只验一条规则，命名即规约。

mod common;

use std::ptr;
use std::sync::atomic::{AtomicU32, AtomicUsize, Ordering};
use std::sync::Arc;

use oakplugin::error::{Error, OAKPLUGIN_E_FAILED, OAKPLUGIN_E_NOT_FOUND, OAKPLUGIN_OK};
use oakplugin::handle::{
	get, guard, guard_handle, make_borrowed, make_owned, RefBox, Registry, CHandle,
};

/// 析构标志：以"被析构次数"断言对象的销毁时机（引用计数语义的
/// 行为探针）。
struct DropFlag(Arc<AtomicUsize>);

impl Drop for DropFlag {
	fn drop(&mut self) {
		self.0.fetch_add(1, Ordering::Relaxed);
	}
}

/// 模拟 C 侧 addref（头文件契约：复制句柄时先 addref）。
fn addref(h: &CHandle) {
	unsafe { (h.addref.expect("addref fn 缺失"))(h.ctx) };
}

/// 模拟 C 侧 free（`oakplugin_instance_free` 语义：ctx 非空才调
/// release，随后清空 ctx；空句柄/已清空句柄是 no-op）。
fn free(h: &mut CHandle) {
	if !h.is_null() {
		unsafe { (h.release.expect("release fn 缺失"))(h.ctx) };
	}
	h.ctx = ptr::null_mut();
}

/// 拥有型句柄：创建计数为 1；addref 后 release 一次对象仍活；
/// 再 release 对象销毁（用析构标志位断言）。
#[test]
fn owned_handle_refcount_lifecycle() {
	let drops = Arc::new(AtomicUsize::new(0));

	// 按值传句柄的位级复制（C 侧 `OakPluginInstance` 结构体拷贝），
	// 复制方必须先 addref：refs 1 -> 2。
	let h1 = make_owned(DropFlag(drops.clone()));
	let mut h2 = unsafe { ptr::read(&h1) };
	addref(&h2);

	// 释放一份：2 -> 1，对象仍活。
	let mut h1 = h1;
	free(&mut h1);
	assert_eq!(drops.load(Ordering::Relaxed), 0);

	// 释放最后一份：1 -> 0，对象恰好销毁一次。
	free(&mut h2);
	assert_eq!(drops.load(Ordering::Relaxed), 1);
}

/// free(NULL)/free(空句柄)/重复 free 同一个已清空句柄：全部 no-op，
/// 不崩、不计数变化（alive 计数前后一致）。
#[test]
fn free_null_and_empty_is_noop() {
	let drops = Arc::new(AtomicUsize::new(0));

	// free(空句柄)：no-op，不崩。
	let mut null = CHandle::null();
	free(&mut null);
	assert!(null.is_null());

	// 正常销毁后句柄已清空；重复 free 是 no-op，计数不再变化。
	let mut h = make_owned(DropFlag(drops.clone()));
	free(&mut h);
	assert_eq!(drops.load(Ordering::Relaxed), 1);
	free(&mut h);
	free(&mut h);
	assert_eq!(drops.load(Ordering::Relaxed), 1);
}

/// 借用句柄：release 只释放盒子，被借用的对象仍然存活
/// （用外部栈对象的析构标志断言）。
#[test]
fn borrowed_handle_never_destroys_object() {
	let drops = Arc::new(AtomicUsize::new(0));
	let mut obj = DropFlag(drops.clone());

	let mut h = unsafe { make_borrowed(&mut obj as *mut DropFlag) };
	assert!(!h.is_null());

	free(&mut h);
	// 盒子释放了，但对象归调用方：析构不在 release 时发生。
	assert_eq!(drops.load(Ordering::Relaxed), 0);
	assert!(h.is_null());

	// 对象最终由调用方析构，恰好一次。
	drop(obj);
	assert_eq!(drops.load(Ordering::Relaxed), 1);
}

/// 空句柄的 get::<T>() 返回 None；类型不符的 get 是调用方责任
/// （文档约定），此处只验空句柄路径。
#[test]
fn get_on_empty_handle_is_none() {
	assert!(unsafe { get::<u32>(&CHandle::null()) }.is_none());

	// 对照：正常句柄能取回引用。
	let mut h = make_owned(42u32);
	assert_eq!(unsafe { get::<u32>(&h) }, Some(&42));
	free(&mut h);
}

/// guard：闭包 panic 被捕获并映射为 OAKPLUGIN_E_FAILED，
/// 不 unwind 出 FFI；Err 映射为对应负码；Ok 映射为 OAKPLUGIN_OK。
#[test]
fn guard_maps_panic_err_ok() {
	// Ok -> OAKPLUGIN_OK。
	assert_eq!(guard(|| Ok(())), OAKPLUGIN_OK);

	// Err -> 对应负码（错误码与 include/plugin/error.h 一致，
	// 项目 -MMCCCC 方案：-90001..-90005）。
	assert_eq!(guard(|| Err(Error::NotFound)), OAKPLUGIN_E_NOT_FOUND);
	assert_eq!(
		guard(|| Err(Error::Invalid)),
		oakplugin::error::OAKPLUGIN_E_INVALID
	);
	assert_eq!(guard(|| Err(Error::State)), oakplugin::error::OAKPLUGIN_E_STATE);
	assert_eq!(
		guard(|| Err(Error::Failed("x".into()))),
		oakplugin::error::OAKPLUGIN_E_FAILED
	);
	assert_eq!(guard(|| Err(Error::NoMem)), oakplugin::error::OAKPLUGIN_E_NOMEM);

	// panic -> OAKPLUGIN_E_FAILED，且 panic 不越过 guard 边界
	// （catch_unwind 语义：本测试线程存活即证明未 unwind）。
	assert_eq!(guard(|| panic!("boom")), OAKPLUGIN_E_FAILED);
}

/// guard_handle：panic/Err 返回空句柄；Ok 透传非空句柄。
#[test]
fn guard_handle_maps_to_null_on_failure() {
	let mut ok = guard_handle(|| Ok(make_owned(7u32)));
	assert!(!ok.is_null());
	free(&mut ok);

	let err = guard_handle(|| Err::<CHandle, _>(Error::State));
	assert!(err.is_null());

	let panicked: CHandle = guard_handle(|| panic!("boom"));
	assert!(panicked.is_null());
}

/// Registry：register 返回唯一身份；lookup 命中；对象销毁后
/// lookup 返回 None（弱引用语义）；unregister 未知身份 no-op。
#[test]
fn registry_register_lookup_unregister() {
	let reg: Registry<u32> = Registry::new();

	let arc = Arc::new(RefBox {
		refs: AtomicU32::new(1),
		value: 7u32,
	});
	let id = reg.register(&arc);
	assert_eq!(id, Arc::as_ptr(&arc) as *const () as usize);
	assert_eq!(reg.lookup(id).unwrap().value, 7);

	// 同一对象重复登记：身份稳定（地址语义），值覆盖。
	let id2 = reg.register(&arc);
	assert_eq!(id2, id);

	// 摘除后 lookup 命中失败；再摘除未知身份是 no-op。
	reg.unregister(id);
	assert!(reg.lookup(id).is_none());
	reg.unregister(id);

	// 对象销毁后弱引用失效：lookup 返回 None。
	let dying = Arc::new(RefBox {
		refs: AtomicU32::new(1),
		value: 9u32,
	});
	let dying_id = reg.register(&dying);
	drop(dying);
	assert!(reg.lookup(dying_id).is_none());

	drop(arc);
}

/// 并发：64 线程对同一句柄 addref/release 各一千次，最终计数正确、
/// 对象恰好销毁一次（线程模型是 multithread suite 的直接投影）。
#[test]
fn refcount_is_thread_safe() {
	let drops = Arc::new(AtomicUsize::new(0));
	let h = make_owned(DropFlag(drops.clone()));

	// 每个线程持有 ctx + 函数指针的拷贝（对应 C 侧各线程各持一份
	// 句柄值），对同一对象做 1000 次 addref/release 配对。
	// 裸指针不可 Send，测试侧经 usize 搬运（C 侧本来也是整数传递）。
	let threads: Vec<_> = (0..64)
		.map(|_| {
			let ctx = h.ctx as usize;
			let addref = h.addref.expect("addref fn 缺失");
			let release = h.release.expect("release fn 缺失");
			std::thread::spawn(move || {
				for _ in 0..1000 {
					unsafe { addref(ctx as *mut std::ffi::c_void) };
					unsafe { release(ctx as *mut std::ffi::c_void) };
				}
			})
		})
		.collect();
	for t in threads {
		t.join().unwrap();
	}

	// 全部配对完成：计数回到创建值，对象存活。
	assert_eq!(drops.load(Ordering::Relaxed), 0);

	// 最终一次 release 恰好销毁。
	let mut h = h;
	free(&mut h);
	assert_eq!(drops.load(Ordering::Relaxed), 1);
}
