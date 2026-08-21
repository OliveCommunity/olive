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

//! Time format node (C++ `src/node/src/time/timeformat/timeformat.{h,cpp}`,
//! `olive::TimeFormatNode`).

use std::ffi::{c_char, c_int, c_long};

use crate::factory::NodeMeta;
use crate::node::{Category, NodeBehavior, NodeCore};
use crate::value::{NodeValue, NodeValueRow, NodeValueTable};
use oakcore_rs::Rational;

/// Time input id (C++ `k_time_input`). Type: float; no explicit default
/// (seconds since the Unix epoch).
pub const TIME_INPUT: &str = "time_in";

/// Format input id (C++ `k_format_input`). Type: text; default
/// `"hh:mm:ss"`; uses Qt `QDateTime::toString` token syntax (d/dd, M/MM,
/// yy/yyyy, h/hh, H/HH, m/mm, s/ss, z/zz/zzz, AP/ap/A/a, single-quoted
/// literals).
pub const FORMAT_INPUT: &str = "format_in";

/// Local-time toggle input id (C++ `k_local_time_input`). Type: boolean;
/// no explicit default (false): when true the epoch time is interpreted in
/// the local timezone (C++ `localtime_r`), otherwise UTC (C++ `gmtime_r`).
pub const LOCAL_TIME_INPUT: &str = "localtime_in";

/// Time format node. Formats a Unix-epoch-seconds time into a text string.
/// The C++ class has no own data members (the `.cpp`'s anonymous-namespace
/// `format_date_time()` token expander becomes part of `value()`), so this
/// is a unit-like struct.
pub struct TimeFormatNode;

/// `struct tm` mirror with the macOS/glibc layout (9 ints, then
/// `long tm_gmtoff`, then `const char *tm_zone`) — enough for the
/// `localtime_r`/`gmtime_r` FFI below. Only the calendar fields are read
/// back.
#[repr(C)]
#[derive(Clone, Copy)]
struct Tm {
	tm_sec: c_int,
	tm_min: c_int,
	tm_hour: c_int,
	tm_mday: c_int,
	tm_mon: c_int,
	tm_year: c_int,
	tm_wday: c_int,
	tm_yday: c_int,
	tm_isdst: c_int,
	tm_gmtoff: c_long,
	tm_zone: *const c_char,
}

// `localtime_r` / `gmtime_r` (C `time.h`). Declared locally instead of
// pulling in a libc crate; both symbols live in the platform C library
// that `std` already links. Windows (UCRT) has the `_s` variants with
// reversed argument order and a 64-bit time_t.
#[cfg(not(target_os = "windows"))]
extern "C" {
	fn localtime_r(timep: *const c_long, result: *mut Tm) -> *mut Tm;
	fn gmtime_r(timep: *const c_long, result: *mut Tm) -> *mut Tm;
}
#[cfg(target_os = "windows")]
extern "C" {
	fn localtime_s(result: *mut Tm, timep: *const i64) -> i32;
	fn gmtime_s(result: *mut Tm, timep: *const i64) -> i32;
}

/// Expand Qt date/time format tokens (`QDateTime::toString` syntax):
/// the field tokens d/dd, M/MM, yy/yyyy, h/hh, H/HH, m/mm, s/ss, z/zz/zzz,
/// AP/ap/A/a, and single-quoted literal sections, mirroring Qt's
/// longest-run matching (C++ anonymous-namespace `format_date_time()` in
/// `timeformat.cpp`).
fn format_date_time(tm: &Tm, ms: i32, format: &str) -> String {
	// Qt displays h/hh on the 12-hour clock only when the format contains an
	// AM/PM token (AP, ap, A or a); otherwise it is the 24-hour clock.
	let has_am_pm = format.contains("AP")
		|| format.contains("ap")
		|| format.contains('A')
		|| format.contains('a');

	let bytes = format.as_bytes();
	let mut out = String::new();
	let mut i = 0;
	while i < bytes.len() {
		let c = bytes[i] as char;
		if c == '\'' {
			// Single-quoted literal section: copy through to the closing
			// quote (or to the end of the format when unterminated).
			match format[i + 1..].find('\'') {
				Some(rel) => {
					out.push_str(&format[i + 1..i + 1 + rel]);
					i += 1 + rel + 1;
				}
				None => {
					out.push_str(&format[i + 1..]);
					break;
				}
			}
			continue;
		}
		let mut run = 1;
		while i + run < bytes.len() && bytes[i + run] == bytes[i] {
			run += 1;
		}
		match c {
			'd' => {
				let s = if run >= 2 {
					format!("{:02}", tm.tm_mday)
				} else {
					format!("{}", tm.tm_mday)
				};
				out.push_str(&s);
			}
			'M' => {
				let mon = tm.tm_mon + 1;
				let s = if run >= 2 {
					format!("{:02}", mon)
				} else {
					format!("{}", mon)
				};
				out.push_str(&s);
			}
			'y' => {
				let year = tm.tm_year + 1900;
				if run >= 4 {
					out.push_str(&format!("{:04}", year));
				} else {
					out.push_str(&format!("{:02}", year % 100));
				}
			}
			'H' => {
				let s = if run >= 2 {
					format!("{:02}", tm.tm_hour)
				} else {
					format!("{}", tm.tm_hour)
				};
				out.push_str(&s);
			}
			'h' => {
				let mut hour = tm.tm_hour;
				if has_am_pm {
					hour %= 12;
					if hour == 0 {
						hour = 12;
					}
				}
				let s = if run >= 2 {
					format!("{:02}", hour)
				} else {
					format!("{}", hour)
				};
				out.push_str(&s);
			}
			'm' => {
				let s = if run >= 2 {
					format!("{:02}", tm.tm_min)
				} else {
					format!("{}", tm.tm_min)
				};
				out.push_str(&s);
			}
			's' => {
				let s = if run >= 2 {
					format!("{:02}", tm.tm_sec)
				} else {
					format!("{}", tm.tm_sec)
				};
				out.push_str(&s);
			}
			'z' => {
				let s = if run >= 3 {
					format!("{:03}", ms)
				} else {
					format!("{}", ms)
				};
				out.push_str(&s);
			}
			'A' | 'a' => {
				// Qt: A/AP/ap/a are all replaced by the full AM/PM string;
				// the two-letter form is a single token.
				if i + run < bytes.len()
					&& bytes[i + run] as char == (c as u8 + (b'P' - b'A')) as char
				{
					run += 1;
				}
				let am = if c == 'A' { "AM" } else { "am" };
				let pm = if c == 'A' { "PM" } else { "pm" };
				out.push_str(if tm.tm_hour < 12 { am } else { pm });
			}
			_ => {
				out.push_str(&format[i..i + run]);
			}
		}
		i += run;
	}
	out
}

/// `Variant::to_bool()` for the local-time toggle (Boolean payload, with a
/// numeric fallback for mis-typed connections).
fn to_bool(v: &NodeValue) -> bool {
	match v {
		NodeValue::Boolean(b) => *b,
		other => other.to_double() != 0.0,
	}
}

/// `Variant::to_string()` for the format string (Text payload, with a
/// numeric fallback for mis-typed connections).
fn to_text(v: &NodeValue) -> String {
	match v {
		NodeValue::Text(s) => s.clone(),
		other => other.to_double().to_string(),
	}
}

impl NodeBehavior for TimeFormatNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Time Format"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.timeformat"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Generator]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Format time (in Unix epoch seconds) into a string."
	}

	/// Localized input names (C++ `retranslate()`): `time_in` -> "Time",
	/// `format_in` -> "Format", `localtime_in` -> "Interpret time as local
	/// time".
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		match id {
			TIME_INPUT => "Time",
			FORMAT_INPUT => "Format",
			LOCAL_TIME_INPUT => "Interpret time as local time",
			_ => id,
		}
	}

	/// Evaluate outputs (C++ `value()`): converts `time_in` (float seconds)
	/// to epoch milliseconds, splits into `std::tm` via `localtime_r` or
	/// `gmtime_r` depending on `localtime_in`, expands the Qt-style format
	/// tokens of `format_in` with longest-run matching (the C++
	/// anonymous-namespace `format_date_time()`: h/hh is 12-hour only when
	/// an AM/PM token is present; A/AP/ap/a emit the full AM/PM string),
	/// and pushes the result as a text value.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &NodeValueRow,
		time: Rational,
		table: &mut NodeValueTable,
	) {
		let time_val = inputs
			.get(TIME_INPUT)
			.cloned()
			.unwrap_or_else(|| core.value_at_time(TIME_INPUT, -1, time));
		let format_val = inputs
			.get(FORMAT_INPUT)
			.cloned()
			.unwrap_or_else(|| core.value_at_time(FORMAT_INPUT, -1, time));
		let local_val = inputs
			.get(LOCAL_TIME_INPUT)
			.cloned()
			.unwrap_or_else(|| core.value_at_time(LOCAL_TIME_INPUT, -1, time));

		let ms_since_epoch = (time_val.to_double() * 1000.0) as i64;
		let secs = (ms_since_epoch / 1000) as c_long;
		let ms = (ms_since_epoch % 1000) as i32;

		let mut tm: Tm = unsafe { std::mem::zeroed() };
		#[cfg(not(target_os = "windows"))]
		unsafe {
			if to_bool(&local_val) {
				localtime_r(&secs, &mut tm);
			} else {
				gmtime_r(&secs, &mut tm);
			}
		}
		#[cfg(target_os = "windows")]
		unsafe {
			let secs64 = secs as i64;
			if to_bool(&local_val) {
				localtime_s(&mut tm, &secs64);
			} else {
				gmtime_s(&mut tm, &secs64);
			}
		}

		let output = format_date_time(&tm, ms, &to_text(&format_val));
		table.push(crate::value::ValueType::Text, NodeValue::Text(output), None);
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, _core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		Some(Box::new(TimeFormatNode))
	}
}

/// Constructor (C++ `TimeFormatNode::TimeFormatNode()`): adds `time_in`
/// (float), `format_in` (text, default `"hh:mm:ss"`), and `localtime_in`
/// (boolean), all with default flags.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	let mut core = NodeCore::new();
	let mut time = crate::input::Input::new(
		TIME_INPUT,
		crate::value::ValueType::Float,
		crate::value::NodeValue::Float(0.0),
	);
	time.properties = vec![
		("min".to_string(), crate::value::NodeValue::Float(0.0)),
		(
			"max".to_string(),
			crate::value::NodeValue::Float(2147483647.0),
		),
	];
	core.add_input(time);
	core.add_input(crate::input::Input::new(
		FORMAT_INPUT,
		crate::value::ValueType::Text,
		crate::value::NodeValue::Text("hh:mm:ss".to_string()),
	));
	core.add_input(crate::input::Input::new(
		LOCAL_TIME_INPUT,
		crate::value::ValueType::Boolean,
		crate::value::NodeValue::Boolean(false),
	));
	(core, Box::new(TimeFormatNode))
}

/// Register this node type (C++ factory listing for
/// `org.olivevideoeditor.Olive.timeformat`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.timeformat",
		name: "Time Format",
		categories: &[Category::Generator],
		create,
	});
}

#[cfg(test)]
mod tests {
	use super::*;
	use crate::node::NodeBehavior;
	use crate::value::{NodeValueTable, ValueType};
	use oakcore_rs::Rational;

	/// A `Tm` with just the calendar fields set (rest zeroed).
	fn tm(sec: i32, min: i32, hour: i32, mday: i32, mon: i32, year: i32) -> Tm {
		Tm {
			tm_sec: sec,
			tm_min: min,
			tm_hour: hour,
			tm_mday: mday,
			tm_mon: mon,
			tm_year: year,
			..unsafe { std::mem::zeroed() }
		}
	}

	#[test]
	fn input_names() {
		let n = TimeFormatNode;
		assert_eq!(n.input_name(TIME_INPUT), "Time");
		assert_eq!(n.input_name(FORMAT_INPUT), "Format");
		assert_eq!(
			n.input_name(LOCAL_TIME_INPUT),
			"Interpret time as local time"
		);
		assert_eq!(n.input_name("other_in"), "other_in");
	}

	#[test]
	fn create_wires_inputs() {
		let (core, behavior) = create();
		assert_eq!(behavior.type_id(), "org.olivevideoeditor.Olive.timeformat");
		assert_eq!(
			core.get_input(TIME_INPUT).unwrap().value_type,
			ValueType::Float
		);
		assert_eq!(
			core.get_input(FORMAT_INPUT).unwrap().value_type,
			ValueType::Text
		);
		assert_eq!(
			core.get_input(FORMAT_INPUT).unwrap().default,
			NodeValue::Text("hh:mm:ss".to_string())
		);
		assert_eq!(
			core.get_input(LOCAL_TIME_INPUT).unwrap().value_type,
			ValueType::Boolean
		);
	}

	#[test]
	fn format_24_hour_clock_without_am_pm_token() {
		let t = tm(6, 5, 13, 9, 7, 124); // 2024-08-09 13:05:06
		assert_eq!(format_date_time(&t, 0, "hh:mm:ss"), "13:05:06");
		assert_eq!(format_date_time(&t, 0, "HH"), "13");
	}

	#[test]
	fn format_12_hour_clock_with_am_pm_token() {
		let pm = tm(0, 0, 13, 1, 0, 124);
		assert_eq!(format_date_time(&pm, 0, "h:mm AP"), "1:00 PM");
		assert_eq!(format_date_time(&pm, 0, "h:mm ap"), "1:00 pm");
		assert_eq!(format_date_time(&pm, 0, "h:mm A"), "1:00 PM");
		assert_eq!(format_date_time(&pm, 0, "h:mm a"), "1:00 pm");
		let am = tm(0, 0, 0, 1, 0, 124);
		assert_eq!(format_date_time(&am, 0, "h AP"), "12 AM");
		let midnight = tm(0, 0, 0, 1, 0, 124);
		assert_eq!(format_date_time(&midnight, 0, "hh AP"), "12 AM");
	}

	#[test]
	fn format_date_tokens() {
		let t = tm(6, 5, 13, 9, 7, 124); // 2024-08-09 13:05:06
		assert_eq!(format_date_time(&t, 0, "yyyy-MM-dd"), "2024-08-09");
		assert_eq!(format_date_time(&t, 0, "yy-M-d"), "24-8-9");
		assert_eq!(format_date_time(&t, 0, "yyyy"), "2024");
	}

	#[test]
	fn format_millisecond_tokens() {
		let t = tm(0, 0, 0, 1, 0, 124);
		assert_eq!(format_date_time(&t, 5, "z"), "5");
		assert_eq!(format_date_time(&t, 5, "zz"), "5");
		assert_eq!(format_date_time(&t, 5, "zzz"), "005");
		assert_eq!(format_date_time(&t, 123, "zzz"), "123");
	}

	#[test]
	fn format_literal_sections() {
		let t = tm(0, 0, 13, 9, 7, 124);
		assert_eq!(format_date_time(&t, 0, "'Literal text'"), "Literal text");
		assert_eq!(format_date_time(&t, 0, "'It''s' HH"), "Its 13");
		assert_eq!(
			format_date_time(&t, 0, "yyyy'unterminated"),
			"2024unterminated"
		);
	}

	#[test]
	fn format_unknown_tokens_copied_verbatim() {
		let t = tm(0, 0, 13, 9, 7, 124);
		assert_eq!(format_date_time(&t, 0, "Q % 5"), "Q % 5");
	}

	#[test]
	fn value_formats_utc_epoch() {
		let (mut core, behavior) = create();
		// 12:34:56 on 1970-01-01 UTC = 45296 seconds.
		core.set_standard_value(TIME_INPUT, -1, NodeValue::Float(45296.0));
		core.set_standard_value(
			FORMAT_INPUT,
			-1,
			NodeValue::Text("yyyy-MM-dd HH:mm:ss".to_string()),
		);
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert_eq!(
			table.get(ValueType::Text),
			Some(&NodeValue::Text("1970-01-01 12:34:56".to_string()))
		);
	}

	#[test]
	fn value_formats_milliseconds_utc() {
		let (mut core, behavior) = create();
		// Same instant plus 789 ms.
		core.set_standard_value(TIME_INPUT, -1, NodeValue::Float(45296.789));
		core.set_standard_value(
			FORMAT_INPUT,
			-1,
			NodeValue::Text("HH:mm:ss.zzz".to_string()),
		);
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert_eq!(
			table.get(ValueType::Text),
			Some(&NodeValue::Text("12:34:56.789".to_string()))
		);
	}

	#[test]
	fn value_uses_connected_inputs() {
		let (core, behavior) = create();
		let mut row = crate::value::NodeValueRow::default();
		row.insert(TIME_INPUT.to_string(), NodeValue::Float(45296.0));
		row.insert(
			FORMAT_INPUT.to_string(),
			NodeValue::Text("yyyy".to_string()),
		);
		row.insert(LOCAL_TIME_INPUT.to_string(), NodeValue::Boolean(false));
		let mut table = NodeValueTable::default();
		behavior.value(&core, &row, Rational::new(0, 1), &mut table);
		assert_eq!(
			table.get(ValueType::Text),
			Some(&NodeValue::Text("1970".to_string()))
		);
	}

	#[test]
	fn value_localtime_flag_routes_to_localtime_r() {
		let (mut core, behavior) = create();
		core.set_standard_value(TIME_INPUT, -1, NodeValue::Float(45296.0));
		core.set_standard_value(FORMAT_INPUT, -1, NodeValue::Text("yyyy".to_string()));

		// Expected values computed through the same C library calls the C++
		// makes — this validates the routing (which function is called for
		// each flag value), not the C library itself.
		let mut secs: c_long = 45296;
		let mut local: Tm = unsafe { std::mem::zeroed() };
		unsafe {
			localtime_r(&secs, &mut local);
		}
		let mut utc: Tm = unsafe { std::mem::zeroed() };
		unsafe {
			gmtime_r(&secs, &mut utc);
		}
		let local_expected = format!("{:04}", local.tm_year + 1900);
		let utc_expected = format!("{:04}", utc.tm_year + 1900);
		assert_eq!(utc_expected, "1970");

		core.set_standard_value(LOCAL_TIME_INPUT, -1, NodeValue::Boolean(true));
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert_eq!(
			table.get(ValueType::Text),
			Some(&NodeValue::Text(local_expected))
		);

		core.set_standard_value(LOCAL_TIME_INPUT, -1, NodeValue::Boolean(false));
		let mut table = NodeValueTable::default();
		behavior.value(
			&core,
			&crate::value::NodeValueRow::default(),
			Rational::new(0, 1),
			&mut table,
		);
		assert_eq!(
			table.get(ValueType::Text),
			Some(&NodeValue::Text(utc_expected))
		);
	}

	#[test]
	fn duplicate_copies_node() {
		let (_core, behavior) = create();
		let copy = behavior.duplicate(&_core).unwrap();
		assert_eq!(copy.type_id(), "org.olivevideoeditor.Olive.timeformat");
		assert_eq!(copy.name(), "Time Format");
	}
}
