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

//! C ABI export layer: implements `include/timeline/*.h` verbatim.
//!
//! One submodule per public header; the authoritative function list is the
//! header itself and each submodule carries the complete inventory comment
//! plus the export stubs. Bodies only unwrap handles, call safe Rust, and
//! map results through [`crate::handle::guard*`].
//!
//! `error.h` defines macros only and is mirrored by [`crate::error`], not
//! here. `displaymode.h` defines enums only; their values map through
//! [`crate::common`] and no `#[no_mangle]` exports exist for them.

use std::ffi::{c_char, c_int, CStr};

use oakcore_rs::{Rational, TimeRange};

use crate::bridge::{common as xml, node as onode, undo as oundo};
use crate::common::MovementMode;
use crate::error::{Error, Result};
use crate::handle::{get, get_mut, guard, guard_handle, guard_i32, guard_void, make_owned, CHandle};
use crate::marker as marker_mod;
use crate::workarea as workarea_mod;
use crate::undogeneral::{
  TimelineAddTrackCommand, TimelineRemoveTrackCommand, TrackListInsertGaps,
  TrackReplaceBlockWithGapCommand,
};
use crate::undopointer::{BlockTrimCommand, TrackPlaceBlockCommand, TrackSlideCommand};
use crate::undosplit::{BlockSplitCommand, BlockSplitPreservingLinksCommand};
use crate::undoripple::{TimelineRippleDeleteGapsAtRegionsCommand, TrackRippleRemoveAreaCommand};
use crate::util::free_detached_handle;

/// Shared handle used for every value/owned handle that crosses the ABI:
/// `OakTimelineMarkerList`, `OakTimelineWorkArea`, `OakUndoCommand`, and
/// the consumed `OakNode*` / `OakXmlReader` / `OakXmlWriter` arguments.
type H = CHandle;

/// `include/timeline/displaymode.h` — enums only. No functions; the enum
/// discriminants are mapped by `crate::common::ThumbnailMode`/`WaveformMode`
/// (`to_c_int`/`from_c_int`) and by `crate::common::MovementMode` for
/// `OakTimelineMovementMode` (include/timeline/edit.h).
pub mod displaymode {
  //! `OAK_TIMELINE_THUMBNAIL_OFF/IN_OUT/ON = 0/1/2` and
  //! `OAK_TIMELINE_WAVEFORMS_DISABLED/ENABLED = 0/1` are value-compatible
  //! with `crate::common`; see those types. No exports live here.
}

/// `include/timeline/marker.h` exports (complete inventory):
/// oaktimeline_marker_list_create / free / of / add / count / at /
/// add_command / remove_at_command / set_time_command /
/// set_props_command / list_load / list_save.
pub mod marker {
  use super::*;

  /// `oaktimeline_marker_list_create`: new owning list, count 1; empty on
  /// allocation failure.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_list_create() -> H {
    guard_handle(|| -> Result<CHandle> {
      Ok(make_owned(marker_mod::TimelineMarkerList::new()))
    })
  }

  /// `oaktimeline_marker_list_of`: borrowed list of a viewer node; empty for
  /// an empty/non-viewer node.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_list_of(owner: H) -> H {
    guard_handle(|| -> Result<CHandle> {
      if owner.is_null() {
        return Err(Error::Invalid);
      }
      let mut out = CHandle::null();
      // SAFETY: `out` is a valid out pointer; `owner` is a valid handle.
      let r = unsafe { onode::oaknode_node_get_markers(owner, &mut out) };
      if r != 0 {
        return Err(Error::Failed(format!("oaknode_node_get_markers: {r}")));
      }
      Ok(out)
    })
  }

  /// `oaktimeline_marker_list_free`: NULL/empty no-op; clears ctx after.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_list_free(list: *mut H) {
    guard_void(|| free_detached_handle(list))
  }

  /// `oaktimeline_marker_add`: append a marker directly (no command).
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_add(
    list: H,
    in_num: c_int,
    in_den: c_int,
    out_num: c_int,
    out_den: c_int,
    name: *const c_char,
    color: c_int,
  ) -> c_int {
    guard(|| {
      if list.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineMarkerList`; the caller holds
      // exclusive access for the duration of the call.
      let l = unsafe { get_mut::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
      let name = cstr_to_string(name);
      let range = TimeRange::new(
        Rational::new(in_num as i64, in_den as i64),
        Rational::new(out_num as i64, out_den as i64),
      );
      l.add_marker(marker_mod::TimelineMarker::with_time(color, range, &name));
      Ok(())
    })
  }

  /// `oaktimeline_marker_count`: number of markers.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_count(list: H, out_count: *mut c_int) -> c_int {
    guard(|| {
      if list.is_null() || out_count.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineMarkerList`.
      let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
      // SAFETY: `out_count` is a valid out pointer.
      unsafe { *out_count = l.size() as c_int; }
      Ok(())
    })
  }

  /// `oaktimeline_marker_at`: marker at index, two-stage string for the name.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_at(
    list: H,
    index: c_int,
    in_num: *mut c_int,
    in_den: *mut c_int,
    out_num: *mut c_int,
    out_den: *mut c_int,
    color: *mut c_int,
    name_buf: *mut c_char,
    buf_size: c_int,
  ) -> c_int {
    guard_i32(|| {
      if list.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineMarkerList`.
      let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
      if index < 0 || (index as usize) >= l.size() {
        return Err(Error::NotFound);
      }
      // SAFETY: `index` was bounds-checked above.
      let m = l.at(index as usize).ok_or(Error::NotFound)?;
      let t = m.time();
      if !in_num.is_null() {
        // SAFETY: `in_num` is a valid out pointer.
        unsafe { *in_num = t.in_().numerator() as c_int; }
      }
      if !in_den.is_null() {
        // SAFETY: `in_den` is a valid out pointer.
        unsafe { *in_den = t.in_().denominator() as c_int; }
      }
      if !out_num.is_null() {
        // SAFETY: `out_num` is a valid out pointer.
        unsafe { *out_num = t.out().numerator() as c_int; }
      }
      if !out_den.is_null() {
        // SAFETY: `out_den` is a valid out pointer.
        unsafe { *out_den = t.out().denominator() as c_int; }
      }
      if !color.is_null() {
        // SAFETY: `color` is a valid out pointer.
        unsafe { *color = m.color(); }
      }
      let needed = m.name().len() + 1;
      if !name_buf.is_null() && buf_size >= needed as c_int {
        write_cstr(name_buf, buf_size, m.name());
      }
      Ok(needed as c_int)
    })
  }

  /// `oaktimeline_marker_add_command`: owned `MarkerAddCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_add_command(
    list: H,
    in_num: c_int,
    in_den: c_int,
    out_num: c_int,
    out_den: c_int,
    name: *const c_char,
    color: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if list.is_null() {
        return Err(Error::Invalid);
      }
      let name = cstr_to_string(name);
      let range = TimeRange::new(
        Rational::new(in_num as i64, in_den as i64),
        Rational::new(out_num as i64, out_den as i64),
      );
      Ok(marker_mod::MarkerAddCommand::new(list, range, &name, color).to_command())
    })
  }

  /// `oaktimeline_marker_remove_at_command`: owned `MarkerRemoveCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_remove_at_command(list: H, index: c_int) -> H {
    guard_handle(|| -> Result<CHandle> {
      let in_bounds = {
        if list.is_null() {
          return Err(Error::Invalid);
        }
        // SAFETY: the boxed value is a `TimelineMarkerList`.
        let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
        index >= 0 && (index as usize) < l.size()
      };
      if !in_bounds {
        return Err(Error::NotFound);
      }
      Ok(marker_mod::MarkerRemoveCommand::new(list, index as usize).to_command())
    })
  }

  /// `oaktimeline_marker_set_time_command`: owned `MarkerChangeTimeCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_set_time_command(
    list: H,
    index: c_int,
    in_num: c_int,
    in_den: c_int,
    out_num: c_int,
    out_den: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      let in_bounds = {
        if list.is_null() {
          return Err(Error::Invalid);
        }
        // SAFETY: the boxed value is a `TimelineMarkerList`.
        let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
        index >= 0 && (index as usize) < l.size()
      };
      if !in_bounds {
        return Err(Error::NotFound);
      }
      let range = TimeRange::new(
        Rational::new(in_num as i64, in_den as i64),
        Rational::new(out_num as i64, out_den as i64),
      );
      Ok(marker_mod::MarkerChangeTimeCommand::new(list, index as usize, range).to_command())
    })
  }

  /// `oaktimeline_marker_set_props_command`: owned command setting color
  /// and/or name.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_set_props_command(
    list: H,
    index: c_int,
    color: c_int,
    name: *const c_char,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      let in_bounds = {
        if list.is_null() {
          return Err(Error::Invalid);
        }
        // SAFETY: the boxed value is a `TimelineMarkerList`.
        let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
        index >= 0 && (index as usize) < l.size()
      };
      if !in_bounds {
        return Err(Error::NotFound);
      }
      if color < 0 && name.is_null() {
        return Err(Error::Invalid);
      }
      let name_str = cstr_to_string(name);
      if color >= 0 && !name.is_null() {
        let multi = unsafe { oundo::oakundo_command_init_multi() };
        if multi.is_null() {
          return Err(Error::Failed("oakundo_command_init_multi".to_string()));
        }
        let color_child =
          marker_mod::MarkerChangeColorCommand::new(list.clone(), index as usize, color).to_command();
        // SAFETY: `multi` is a valid multi command handle.
        let _ = unsafe { oundo::oakundo_command_multi_add_child(multi.clone(), color_child) };
        let name_child =
          marker_mod::MarkerChangeNameCommand::new(list, index as usize, &name_str).to_command();
        // SAFETY: `multi` is a valid multi command handle.
        let _ = unsafe { oundo::oakundo_command_multi_add_child(multi.clone(), name_child) };
        return Ok(multi);
      }
      if color >= 0 {
        return Ok(marker_mod::MarkerChangeColorCommand::new(list, index as usize, color).to_command());
      }
      Ok(marker_mod::MarkerChangeNameCommand::new(list, index as usize, &name_str).to_command())
    })
  }

  /// `oaktimeline_marker_list_load`: read the list from an oakcommon reader.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_list_load(list: H, reader: H) -> c_int {
    guard(|| {
      if list.is_null() || reader.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineMarkerList`; the caller holds
      // exclusive access for the duration of the call.
      let l = unsafe { get_mut::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
      let group = cstr_arg("MarkerColor");
      // SAFETY: `group` is a valid NUL-terminated string for the duration of
      // the call.
      let default_color = unsafe { xml::oakcommon_config_get_int(std::ptr::null(), group.as_ptr(), 0) };
      loop {
        let element = xml_read_next(&reader);
        if element.is_empty() {
          break;
        }
        if element == "marker" {
          let mut marker_name = String::new();
          let mut range_in = Rational::new(0, 1);
          let mut range_out = Rational::new(0, 1);
          let mut marker_color = default_color;
          let mut count: c_int = 0;
          // SAFETY: `count` is a valid out pointer; `reader` is a valid handle.
          let _ = unsafe { xml::oakcommon_xml_reader_attribute_count(reader.clone(), &mut count) };
          for i in 0..count {
            let (an, av) = xml_attribute(&reader, i);
            if an == "name" {
              marker_name = av;
            } else if an == "in" {
              range_in = Rational::from_string(&av);
            } else if an == "out" {
              range_out = Rational::from_string(&av);
            } else if an == "color" {
              // C++ `atoi`: garbage parses to 0, not the default color.
              marker_color = av.parse::<i32>().unwrap_or(0);
            }
          }
          l.add_marker(marker_mod::TimelineMarker::with_time(
            marker_color,
            TimeRange::new(range_in, range_out),
            &marker_name,
          ));
        }
        // SAFETY: `reader` is a valid handle; every branch of the C++ load
        // ends by skipping the current element.
        unsafe { xml::oakcommon_xml_reader_skip_current_element(reader.clone()) };
      }
      Ok(())
    })
  }

  /// `oaktimeline_marker_list_save`: write the list to an oakcommon writer.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_marker_list_save(list: H, writer: H) -> c_int {
    guard(|| {
      if list.is_null() || writer.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineMarkerList`.
      let l = unsafe { get::<marker_mod::TimelineMarkerList>(&list) }.ok_or(Error::Invalid)?;
      for i in 0..l.size() {
        // SAFETY: `i` is in range by the loop bound.
        let m = l.at(i).ok_or(Error::NotFound)?;
        let tag = cstr_arg("marker");
        // SAFETY: `writer` is a valid handle; `tag` is valid for the call.
        unsafe { xml::oakcommon_xml_writer_write_start_element(writer.clone(), tag.as_ptr()) };
        let name_key = cstr_arg("name");
        let name_val = cstr_arg(m.name());
        // SAFETY: `writer` and both C strings are valid for the call.
        unsafe { xml::oakcommon_xml_writer_write_attribute(writer.clone(), name_key.as_ptr(), name_val.as_ptr()) };
        let in_key = cstr_arg("in");
        let in_val = cstr_arg(&m.time().in_().to_display_string());
        // SAFETY: as above.
        unsafe { xml::oakcommon_xml_writer_write_attribute(writer.clone(), in_key.as_ptr(), in_val.as_ptr()) };
        let out_key = cstr_arg("out");
        let out_val = cstr_arg(&m.time().out().to_display_string());
        // SAFETY: as above.
        unsafe { xml::oakcommon_xml_writer_write_attribute(writer.clone(), out_key.as_ptr(), out_val.as_ptr()) };
        let color_key = cstr_arg("color");
        let color_val = cstr_arg(&m.color().to_string());
        // SAFETY: as above.
        unsafe { xml::oakcommon_xml_writer_write_attribute(writer.clone(), color_key.as_ptr(), color_val.as_ptr()) };
        // SAFETY: `writer` is a valid handle.
        unsafe { xml::oakcommon_xml_writer_write_end_element(writer.clone()) };
      }
      Ok(())
    })
  }
}

/// `include/timeline/workarea.h` exports (complete inventory):
/// oaktimeline_workarea_create / free / of / set_enabled / get / set_range /
/// set_range_command / set_enabled_command / reset / load / save.
pub mod workarea {
  use super::*;

  /// `oaktimeline_workarea_create`: new owning work area, count 1; empty on
  /// allocation failure.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_create() -> H {
    guard_handle(|| -> Result<CHandle> {
      Ok(make_owned(workarea_mod::TimelineWorkArea::new()))
    })
  }

  /// `oaktimeline_workarea_of`: borrowed work area of a viewer node; empty
  /// for an empty/non-viewer node.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_of(owner: H) -> H {
    guard_handle(|| -> Result<CHandle> {
      if owner.is_null() {
        return Err(Error::Invalid);
      }
      let mut out = CHandle::null();
      // SAFETY: `out` is a valid out pointer; `owner` is a valid handle.
      let r = unsafe { onode::oaknode_node_get_work_area(owner, &mut out) };
      if r != 0 {
        return Err(Error::Failed(format!("oaknode_node_get_work_area: {r}")));
      }
      Ok(out)
    })
  }

  /// `oaktimeline_workarea_free`: NULL/empty no-op; clears ctx after.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_free(w: *mut H) {
    guard_void(|| free_detached_handle(w))
  }

  /// `oaktimeline_workarea_set_enabled`: set enabled live.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_set_enabled(w: H, enabled: c_int) -> c_int {
    guard(|| {
      if w.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineWorkArea`; the caller holds
      // exclusive access for the duration of the call.
      let wa = unsafe { get_mut::<workarea_mod::TimelineWorkArea>(&w) }.ok_or(Error::Invalid)?;
      wa.set_enabled(enabled != 0);
      Ok(())
    })
  }

  /// `oaktimeline_workarea_get`: read state; out params may be NULL.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_get(
    w: H,
    in_num: *mut c_int,
    in_den: *mut c_int,
    out_num: *mut c_int,
    out_den: *mut c_int,
    enabled: *mut c_int,
  ) -> c_int {
    guard(|| {
      if w.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineWorkArea`.
      let wa = unsafe { get::<workarea_mod::TimelineWorkArea>(&w) }.ok_or(Error::Invalid)?;
      if !in_num.is_null() {
        // SAFETY: `in_num` is a valid out pointer.
        unsafe { *in_num = wa.in_().numerator() as c_int; }
      }
      if !in_den.is_null() {
        // SAFETY: `in_den` is a valid out pointer.
        unsafe { *in_den = wa.in_().denominator() as c_int; }
      }
      if !out_num.is_null() {
        // SAFETY: `out_num` is a valid out pointer.
        unsafe { *out_num = wa.out().numerator() as c_int; }
      }
      if !out_den.is_null() {
        // SAFETY: `out_den` is a valid out pointer.
        unsafe { *out_den = wa.out().denominator() as c_int; }
      }
      if !enabled.is_null() {
        // SAFETY: `enabled` is a valid out pointer.
        unsafe { *enabled = wa.enabled() as c_int; }
      }
      Ok(())
    })
  }

  /// `oaktimeline_workarea_set_range`: set the range live.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_set_range(
    w: H,
    in_num: c_int,
    in_den: c_int,
    out_num: c_int,
    out_den: c_int,
  ) -> c_int {
    guard(|| {
      if w.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineWorkArea`; the caller holds
      // exclusive access for the duration of the call.
      let wa = unsafe { get_mut::<workarea_mod::TimelineWorkArea>(&w) }.ok_or(Error::Invalid)?;
      wa.set_range(TimeRange::new(
        Rational::new(in_num as i64, in_den as i64),
        Rational::new(out_num as i64, out_den as i64),
      ));
      Ok(())
    })
  }

  /// `oaktimeline_workarea_set_range_command`: owned `WorkareaSetRangeCommand`
  /// with caller-supplied old range.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_set_range_command(
    w: H,
    in_num: c_int,
    in_den: c_int,
    out_num: c_int,
    out_den: c_int,
    old_in_num: c_int,
    old_in_den: c_int,
    old_out_num: c_int,
    old_out_den: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if w.is_null() {
        return Err(Error::Invalid);
      }
      let range = TimeRange::new(
        Rational::new(in_num as i64, in_den as i64),
        Rational::new(out_num as i64, out_den as i64),
      );
      let old_range = TimeRange::new(
        Rational::new(old_in_num as i64, old_in_den as i64),
        Rational::new(old_out_num as i64, old_out_den as i64),
      );
      Ok(workarea_mod::WorkareaSetRangeCommand::new_with_old(w, range, old_range).to_command())
    })
  }

  /// `oaktimeline_workarea_set_enabled_command`: owned
  /// `WorkareaSetEnabledCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_set_enabled_command(w: H, enabled: c_int) -> H {
    guard_handle(|| -> Result<CHandle> {
      if w.is_null() {
        return Err(Error::Invalid);
      }
      Ok(workarea_mod::WorkareaSetEnabledCommand::new(w, enabled != 0).to_command())
    })
  }

  /// `oaktimeline_workarea_reset`: the reset sentinel range
  /// (k_reset_in = 0/1 .. k_reset_out = -1/1).
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_reset(
    in_num: *mut c_int,
    in_den: *mut c_int,
    out_num: *mut c_int,
    out_den: *mut c_int,
  ) -> c_int {
    guard(|| {
      if in_num.is_null() || in_den.is_null() || out_num.is_null() || out_den.is_null() {
        return Err(Error::Invalid);
      }
      let rin = workarea_mod::reset_in();
      let rout = workarea_mod::reset_out();
      // SAFETY: all four pointers were checked non-NULL above.
      unsafe {
        *in_num = rin.numerator() as c_int;
        *in_den = rin.denominator() as c_int;
        *out_num = rout.numerator() as c_int;
        *out_den = rout.denominator() as c_int;
      }
      Ok(())
    })
  }

  /// `oaktimeline_workarea_load`: read from an oakcommon reader.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_load(w: H, reader: H) -> c_int {
    guard(|| {
      if w.is_null() || reader.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineWorkArea`; the caller holds
      // exclusive access for the duration of the call.
      let wa = unsafe { get_mut::<workarea_mod::TimelineWorkArea>(&w) }.ok_or(Error::Invalid)?;
      let mut range_in = wa.in_();
      let mut range_out = wa.out();
      loop {
        let element = xml_read_next(&reader);
        if element.is_empty() {
          break;
        }
        if element == "enabled" {
          wa.set_enabled(xml_element_text(&reader) != "0");
        } else if element == "in" {
          range_in = Rational::from_string(&xml_element_text(&reader));
        } else if element == "out" {
          range_out = Rational::from_string(&xml_element_text(&reader));
        } else {
          // SAFETY: `reader` is a valid handle.
          unsafe { xml::oakcommon_xml_reader_skip_current_element(reader.clone()) };
        }
      }
      let loaded = TimeRange::new(range_in, range_out);
      if loaded != *wa.range() {
        wa.set_range(loaded);
      }
      Ok(())
    })
  }

  /// `oaktimeline_workarea_save`: write to an oakcommon writer.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_workarea_save(w: H, writer: H) -> c_int {
    guard(|| {
      if w.is_null() || writer.is_null() {
        return Err(Error::Invalid);
      }
      // SAFETY: the boxed value is a `TimelineWorkArea`.
      let wa = unsafe { get::<workarea_mod::TimelineWorkArea>(&w) }.ok_or(Error::Invalid)?;
      let version_key = cstr_arg("version");
      let version_val = cstr_arg("1");
      // SAFETY: `writer` and both C strings are valid for the call.
      unsafe { xml::oakcommon_xml_writer_write_attribute(writer.clone(), version_key.as_ptr(), version_val.as_ptr()) };
      let enabled_key = cstr_arg("enabled");
      let enabled_val = cstr_arg(if wa.enabled() { "1" } else { "0" });
      // SAFETY: `writer` and both C strings are valid for the call.
      unsafe { xml::oakcommon_xml_writer_write_text_element(writer.clone(), enabled_key.as_ptr(), enabled_val.as_ptr()) };
      let in_key = cstr_arg("in");
      let in_val = cstr_arg(&wa.in_().to_display_string());
      // SAFETY: as above.
      unsafe { xml::oakcommon_xml_writer_write_text_element(writer.clone(), in_key.as_ptr(), in_val.as_ptr()) };
      let out_key = cstr_arg("out");
      let out_val = cstr_arg(&wa.out().to_display_string());
      // SAFETY: as above.
      unsafe { xml::oakcommon_xml_writer_write_text_element(writer.clone(), out_key.as_ptr(), out_val.as_ptr()) };
      Ok(())
    })
  }
}

/// `include/timeline/edit.h` exports (complete inventory):
/// oaktimeline_add_track_command / remove_track_command /
/// place_block_command / replace_block_with_gap_command / trim_command /
/// split_command / split_preserving_links_command /
/// ripple_delete_gaps_command / slide_command / ripple_remove_area_command /
/// insert_gaps_command. `OakTimelineMovementMode` maps through
/// `crate::common::MovementMode::from_c_int`.
pub mod edit {
  use super::*;

  /// `oaktimeline_add_track_command`: owned `TimelineAddTrackCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_add_track_command(list: H) -> H {
    guard_handle(|| -> Result<CHandle> {
      if list.is_null() {
        return Err(Error::Invalid);
      }
      Ok(TimelineAddTrackCommand::new(list).to_command())
    })
  }

  /// `oaktimeline_remove_track_command`: owned `TimelineRemoveTrackCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_remove_track_command(track: H) -> H {
    guard_handle(|| -> Result<CHandle> {
      if track.is_null() {
        return Err(Error::Invalid);
      }
      Ok(TimelineRemoveTrackCommand::new(track).to_command())
    })
  }

  /// `oaktimeline_place_block_command`: owned `TrackPlaceBlockCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_place_block_command(
    list: H,
    track_index: c_int,
    block: H,
    in_num: i64,
    in_den: i64,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if list.is_null() || block.is_null() {
        return Err(Error::Invalid);
      }
      Ok(TrackPlaceBlockCommand::new(
        list,
        track_index,
        block,
        Rational::new(in_num, in_den),
      )
      .to_command())
    })
  }

  /// `oaktimeline_replace_block_with_gap_command`: owned
  /// `TrackReplaceBlockWithGapCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_replace_block_with_gap_command(track: H, block: H) -> H {
    guard_handle(|| -> Result<CHandle> {
      if track.is_null() || block.is_null() {
        return Err(Error::Invalid);
      }
      // C++ `handle_transitions` defaults to true.
      Ok(TrackReplaceBlockWithGapCommand::new(track, block, true).to_command())
    })
  }

  /// `oaktimeline_trim_command`: owned `BlockTrimCommand`; `mode` is an
  /// `OakTimelineMovementMode` value.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_trim_command(
    track: H,
    block: H,
    new_length_num: i64,
    new_length_den: i64,
    mode: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if track.is_null() || block.is_null() {
        return Err(Error::Invalid);
      }
      let mode = MovementMode::from_c_int(mode).ok_or(Error::Invalid)?;
      // C++ rejects NONE and MOVE as well.
      if !mode.is_a_trim_mode() {
        return Err(Error::Invalid);
      }
      Ok(BlockTrimCommand::new(
        track,
        block,
        Rational::new(new_length_num, new_length_den),
        mode,
      )
      .to_command())
    })
  }

  /// `oaktimeline_split_command`: owned `BlockSplitCommand` on a set of
  /// blocks at one point.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_split_command(
    blocks: *const H,
    count: c_int,
    point_num: i64,
    point_den: i64,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if blocks.is_null() || count <= 0 {
        return Err(Error::Invalid);
      }
      let point = Rational::new(point_num, point_den);
      // SAFETY: `blocks` is a valid array of `count` handles (checked
      // non-NULL and positive above), readable for the duration of the call.
      let slice = unsafe { std::slice::from_raw_parts(blocks, count as usize) };
      let mut children: Vec<CHandle> = Vec::new();
      for b in slice {
        // C++ skips null block handles in the loop.
        if !b.is_null() {
          children.push(BlockSplitCommand::new(b.clone(), point).to_command());
        }
      }
      let multi = unsafe { oundo::oakundo_command_init_multi() };
      if multi.is_null() {
        return Err(Error::Failed("oakundo_command_init_multi".to_string()));
      }
      for child in children {
        // SAFETY: `multi` is a valid multi command handle.
        let _ = unsafe { oundo::oakundo_command_multi_add_child(multi.clone(), child) };
      }
      Ok(multi)
    })
  }

  /// `oaktimeline_split_preserving_links_command`: owned
  /// `BlockSplitPreservingLinksCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_split_preserving_links_command(
    blocks: *const H,
    count: c_int,
    point_nums: *const i64,
    point_dens: *const i64,
    time_count: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if blocks.is_null() || count <= 0 || point_nums.is_null() || point_dens.is_null() || time_count <= 0 {
        return Err(Error::Invalid);
      }
      // SAFETY: `blocks` is a valid array of `count` handles; `point_nums`
      // and `point_dens` are valid arrays of `time_count` i64s; all readable
      // for the duration of the call.
      let block_vec: Vec<CHandle> = unsafe { std::slice::from_raw_parts(blocks, count as usize) }.to_vec();
      let nums = unsafe { std::slice::from_raw_parts(point_nums, time_count as usize) };
      let dens = unsafe { std::slice::from_raw_parts(point_dens, time_count as usize) };
      let times: Vec<Rational> = nums
        .iter()
        .zip(dens.iter())
        .map(|(n, d)| Rational::new(*n, *d))
        .collect();
      Ok(BlockSplitPreservingLinksCommand::new(block_vec, times).to_command())
    })
  }

  /// `oaktimeline_ripple_delete_gaps_command`: owned
  /// `TimelineRippleDeleteGapsAtRegionsCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_ripple_delete_gaps_command(
    sequence: H,
    in_nums: *const i64,
    in_dens: *const i64,
    out_nums: *const i64,
    out_dens: *const i64,
    tracks: *const H,
    range_count: c_int,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if sequence.is_null()
        || in_nums.is_null()
        || in_dens.is_null()
        || out_nums.is_null()
        || out_dens.is_null()
        || tracks.is_null()
        || range_count <= 0
      {
        return Err(Error::Invalid);
      }
      // SAFETY: all five arrays are valid and `range_count`-long (checked
      // non-NULL and positive above), readable for the duration of the call.
      let in_nums = unsafe { std::slice::from_raw_parts(in_nums, range_count as usize) };
      let in_dens = unsafe { std::slice::from_raw_parts(in_dens, range_count as usize) };
      let out_nums = unsafe { std::slice::from_raw_parts(out_nums, range_count as usize) };
      let out_dens = unsafe { std::slice::from_raw_parts(out_dens, range_count as usize) };
      let tracks = unsafe { std::slice::from_raw_parts(tracks, range_count as usize) };
      let regions: Vec<(CHandle, TimeRange)> = (0..range_count as usize)
        .map(|i| {
          (
            tracks[i].clone(),
            TimeRange::new(
              Rational::new(in_nums[i], in_dens[i]),
              Rational::new(out_nums[i], out_dens[i]),
            ),
          )
        })
        .collect();
      Ok(TimelineRippleDeleteGapsAtRegionsCommand::new(sequence, regions).to_command())
    })
  }

  /// `oaktimeline_slide_command`: owned `TrackSlideCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_slide_command(
    track: H,
    blocks: *const H,
    block_count: c_int,
    in_adjacent: H,
    out_adjacent: H,
    movement_num: i64,
    movement_den: i64,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if track.is_null() || blocks.is_null() || block_count <= 0 {
        return Err(Error::Invalid);
      }
      // SAFETY: `blocks` is a valid array of `block_count` handles (checked
      // non-NULL and positive above), readable for the duration of the call.
      let block_vec: Vec<CHandle> = unsafe { std::slice::from_raw_parts(blocks, block_count as usize) }.to_vec();
      Ok(TrackSlideCommand::new(
        track,
        block_vec,
        in_adjacent,
        out_adjacent,
        Rational::new(movement_num, movement_den),
      )
      .to_command())
    })
  }

  /// `oaktimeline_ripple_remove_area_command`: owned
  /// `TrackRippleRemoveAreaCommand`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_ripple_remove_area_command(
    track: H,
    in_num: i64,
    in_den: i64,
    out_num: i64,
    out_den: i64,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if track.is_null() {
        return Err(Error::Invalid);
      }
      let range = TimeRange::new(
        Rational::new(in_num, in_den),
        Rational::new(out_num, out_den),
      );
      Ok(TrackRippleRemoveAreaCommand::new(track, range).to_command())
    })
  }

  /// `oaktimeline_insert_gaps_command`: owned `TrackListInsertGaps`.
  #[no_mangle]
  pub unsafe extern "C" fn oaktimeline_insert_gaps_command(
    list: H,
    point_num: i64,
    point_den: i64,
    length_num: i64,
    length_den: i64,
  ) -> H {
    guard_handle(|| -> Result<CHandle> {
      if list.is_null() {
        return Err(Error::Invalid);
      }
      Ok(TrackListInsertGaps::new(
        list,
        Rational::new(point_num, point_den),
        Rational::new(length_num, length_den),
      )
      .to_command())
    })
  }
}

/// Convert a NUL-terminated C string to `String`; `NULL` yields the empty
/// string (mirrors the C++ callers, which treat a null name as "").
fn cstr_to_string(p: *const c_char) -> String {
  if p.is_null() {
    return String::new();
  }
  // SAFETY: the caller guarantees `p` is a valid NUL-terminated string.
  let s = unsafe { CStr::from_ptr(p) };
  s.to_string_lossy().into_owned()
}

/// Build a temporary NUL-terminated C string for an extern argument.
fn cstr_arg(s: &str) -> std::ffi::CString {
  std::ffi::CString::new(s).unwrap_or_default()
}

/// Copy `s` into `dst` as a NUL-terminated C string, writing at most `size`-1
/// bytes; NULL buffer or non-positive size is a no-op.
fn write_cstr(dst: *mut c_char, size: c_int, s: &str) {
  if dst.is_null() || size <= 0 {
    return;
  }
  let cap = (size as usize).saturating_sub(1);
  let bytes = s.as_bytes();
  let n = bytes.len().min(cap);
  // SAFETY: `dst` points to a writable buffer of at least `size` bytes, so
  // `n <= size - 1` bytes plus the NUL terminator fit.
  unsafe {
    std::ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, dst, n);
    *dst.add(n) = 0;
  }
}

/// Read the next start element's name from `reader`; empty at EOF/error.
fn xml_read_next(reader: &H) -> String {
  // Advance to the next start element (the real ABI writes a 1/0 flag).
  let mut found = 0;
  // SAFETY: `found` is a valid out pointer; `reader` is a valid handle.
  let r = unsafe { xml::oakcommon_xml_reader_read_next_start_element(reader.clone(), &mut found) };
  if r <= 0 || found == 0 {
    return String::new();
  }
  // Read the current element's name (two-stage).
  let mut buf = [0 as c_char; 4096];
  // SAFETY: `buf` is a valid writable buffer; `reader` is a valid handle.
  let n = unsafe { xml::oakcommon_xml_reader_name(reader.clone(), buf.as_mut_ptr(), buf.len() as c_int) };
  if n <= 0 {
    return String::new();
  }
  // SAFETY: the reader wrote a NUL-terminated string into `buf`.
  let s = unsafe { CStr::from_ptr(buf.as_ptr()) };
  s.to_string_lossy().into_owned()
}

/// Read the current element's text content from `reader`.
fn xml_element_text(reader: &H) -> String {
  let mut buf = [0 as c_char; 4096];
  // SAFETY: `buf` is a valid writable buffer; `reader` is a valid handle.
  let r = unsafe {
    xml::oakcommon_xml_reader_read_element_text(reader.clone(), buf.as_mut_ptr(), buf.len() as c_int)
  };
  if r <= 0 {
    return String::new();
  }
  // SAFETY: the reader wrote a NUL-terminated string into `buf`.
  let s = unsafe { CStr::from_ptr(buf.as_ptr()) };
  s.to_string_lossy().into_owned()
}

/// Read attribute `index`'s (name, value) pair from `reader`.
fn xml_attribute(reader: &H, index: c_int) -> (String, String) {
  let mut name_buf = [0 as c_char; 4096];
  let mut value_buf = [0 as c_char; 4096];
  // SAFETY: both buffers are valid writable; `reader` is a valid handle.
  let _ = unsafe {
    xml::oakcommon_xml_reader_attribute_name(reader.clone(), index, name_buf.as_mut_ptr(), name_buf.len() as c_int)
  };
  let _ = unsafe {
    xml::oakcommon_xml_reader_attribute_value(reader.clone(), index, value_buf.as_mut_ptr(), value_buf.len() as c_int)
  };
  // SAFETY: the reader wrote NUL-terminated strings into both buffers.
  let name = unsafe { CStr::from_ptr(name_buf.as_ptr()) };
  let value = unsafe { CStr::from_ptr(value_buf.as_ptr()) };
  (name.to_string_lossy().into_owned(), value.to_string_lossy().into_owned())
}
