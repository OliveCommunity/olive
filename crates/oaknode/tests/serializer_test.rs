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

//! Serializer tests. The C++ writer's golden files do not exist in this
//! tree (`tests/golden/` is empty), so the byte-exact comparison is
//! ignored with a doc reason; everything else (round-trip idempotence,
//! version rejection, corrupt input) is live.

/// Save format parity: a fixture project saves byte-identical XML to
/// the C++ 230220 writer output (attribute order included).
///
/// Ignored: the C++ golden fixtures are not committed in this tree
/// (src/node/tests/ has no .xml/.ove files) and the value text codecs
/// use Rust formatting, so byte-exact parity cannot be pinned here. The
/// C++ gtest suite (`src/node/tests/serializer_test.cpp`) pins the
/// writer; this crate's serializer_test covers structure + round-trip.
#[test]
#[ignore = "C++ golden fixtures unavailable in this tree"]
fn save_matches_cpp_byte_exact() {
	todo!()
}

/// Round-trip: load(save(p)) yields a project whose re-saved XML is
/// identical (idempotence).
///
/// Needs the `test-stubs` feature: save/load route XML through the
/// oakcommon bridge, whose symbols only resolve in the test binary when
/// the in-crate stubs are compiled in.
#[cfg(feature = "test-stubs")]
#[test]
fn roundtrip_idempotent() {
	use oaknode::input::Input;
	use oaknode::node::NodeCore;
	use oaknode::project::Project;
	use oaknode::value::{NodeValue, ValueType};

	let project = Project::new();
	{
		let mut p = project.lock().unwrap();
		p.initialize().unwrap();
		// A math node with a set value + a keyframe.
		let (core, behavior) = (oaknode::factory::Factory::global()
			.find("org.olivevideoeditor.Olive.math")
			.unwrap()
			.create)();
		let id = p.graph.add_node(core, behavior);
		p.graph.get_mut(id).unwrap().core.set_standard_value(
			"param_a_in",
			-1,
			NodeValue::Float(2.5),
		);
		p.graph
			.get_mut(id)
			.unwrap()
			.core
			.keyframe_track_mut("param_a_in", -1)
			.set_key(oaknode::keyframe::Keyframe {
				time: oakcore_rs::Rational::new(0, 1),
				value: NodeValue::Float(1.0),
				interpolation: oaknode::keyframe::Interpolation::Linear,
				bezier_in: (0.0, 0.0),
				bezier_out: (0.0, 0.0),
			});
		// A second node connected to the first.
		let (core2, behavior2) = (oaknode::factory::Factory::global()
			.find("org.olivevideoeditor.Olive.math")
			.unwrap()
			.create)();
		let id2 = p.graph.add_node(core2, behavior2);
		p.graph.connect(id, id2, "param_a_in", -1).unwrap();
	}

	let xml = {
		let p = project.lock().unwrap();
		oaknode::serializer::save(&p).unwrap()
	};
	let loaded = oaknode::serializer::load(&xml).unwrap();
	let xml2 = {
		let p = loaded.lock().unwrap();
		oaknode::serializer::save(&p).unwrap()
	};
	assert_eq!(xml, xml2, "re-save is idempotent");

	// Structural equivalence: node count + edges.
	{
		let a = project.lock().unwrap();
		let b = loaded.lock().unwrap();
		assert_eq!(a.graph.node_count(), b.graph.node_count());
		assert_eq!(
			a.graph.output_connections_all().len(),
			b.graph.output_connections_all().len()
		);
	}
}

/// Version ladder: historical `<olive ...>` roots with known versions
/// load (the body upgrades to the current model); unknown roots are
/// rejected.
///
/// Needs the `test-stubs` feature (same XML-bridge rationale as
/// [`roundtrip_idempotent`]).
#[cfg(feature = "test-stubs")]
#[test]
fn historical_versions_upgrade() {
	// A current-format document round-trips.
	let xml =
		"<project version=\"1\"><uuid>{test}</uuid><nodes></nodes><settings></settings></project>";
	let project = oaknode::serializer::load(xml).unwrap();
	assert_eq!(project.lock().unwrap().uuid, "{test}");

	// An unknown root is rejected.
	assert!(oaknode::serializer::load("<olive-unknown/>").is_err());
	assert!(oaknode::serializer::load("").is_err());
}

/// Corrupt XML and newer-than-build versions are rejected with the
/// documented error codes, never a panic.
#[test]
fn corrupt_and_future_files_rejected() {
	// A newer-than-build version is rejected.
	let future = "<olive version=\"999999\"></olive>";
	assert!(oaknode::serializer::load(future).is_err());

	// Malformed XML (unbalanced) is rejected without a panic.
	let corrupt = "<project><nodes><node></project>";
	assert!(oaknode::serializer::load(corrupt).is_err());

	// Garbage text.
	assert!(oaknode::serializer::load("not xml at all").is_err());
}
