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

//! Byte-for-byte round-trip tests against the golden files captured from
//! the opentimelineio C++ writer: parse, re-serialize, and require the
//! output to be identical (4-space indent, `": "` separators, inline empty
//! containers, shortest floats, no trailing newline).

use std::fs;
use std::path::PathBuf;

fn read_golden(name: &str) -> String {
	let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
		.join("tests/data")
		.join(name);
	fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {path:?}: {e}"))
}

fn assert_round_trip(name: &str) {
	let text = read_golden(name);
	let doc = oakotio::from_json_string(&text).unwrap_or_else(|e| panic!("parse {name}: {e}"));
	let out = doc
		.to_json_string()
		.unwrap_or_else(|e| panic!("serialize {name}: {e}"));
	assert_eq!(out, text, "round-trip mismatch for {name}");
}

#[test]
fn golden_timeline_round_trips() {
	assert_round_trip("golden_timeline.json");
}

#[test]
fn golden_collection_round_trips() {
	assert_round_trip("golden_collection.json");
}

#[test]
fn golden_typed_transition_round_trips() {
	assert_round_trip("golden_typed_transition.json");
}

#[test]
fn floatfmt_round_trips() {
	assert_round_trip("floatfmt.json");
}

#[test]
fn golden_timeline_parses_as_timeline_root() {
	let doc = oakotio::from_json_string(&read_golden("golden_timeline.json")).unwrap();
	assert_eq!(doc.schema_name(), "Timeline");
	assert!(doc.as_timeline().is_some());
}

#[test]
fn golden_collection_parses_as_collection_root() {
	let doc = oakotio::from_json_string(&read_golden("golden_collection.json")).unwrap();
	assert_eq!(doc.schema_name(), "SerializableCollection");
	assert!(doc.as_collection().is_some());
}
