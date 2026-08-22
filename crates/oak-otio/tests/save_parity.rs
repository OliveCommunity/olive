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

//! Save-side parity: rebuild `golden_timeline.json` through the public
//! builder API and require the serialized bytes to match the golden file
//! byte for byte. This proves the writer (not just the reader) reproduces
//! the opentimelineio C++ output.

use oak_otio::{
	Clip, Composable, ExternalReference, Gap, MediaReference, RationalTime, TimeRange, Timeline,
	Track, Transition,
};

fn build_golden_timeline() -> Timeline {
	let mut timeline = Timeline::new("My Sequence");

	let mut video = Track::new("Video");

	let mut clip = Clip::new("My Sequence Clip");
	clip.set_source_range(TimeRange::new(
		RationalTime::new(0.0, 24.0),
		RationalTime::new(1152.0, 24.0),
	));
	clip.set_media_reference(MediaReference::ExternalReference(ExternalReference::new(
		"file:///tmp/My Sequence.mp4",
		Some(TimeRange::new(
			RationalTime::new(0.0, 25.0),
			RationalTime::new(100.0, 25.0),
		)),
	)));
	video.append_child(Composable::Clip(clip));

	video.append_child(Composable::Gap(Gap::new(
		TimeRange::new(RationalTime::new(0.0, 24.0), RationalTime::new(576.0, 24.0)),
		"My Sequence Gap",
	)));

	let mut transition = Transition::new("My Sequence Transition");
	transition.set_in_offset(RationalTime::new(12.0, 24.0));
	transition.set_out_offset(RationalTime::new(12.0, 24.0));
	video.append_child(Composable::Transition(transition));

	let mut audio = Track::new("Audio");

	let mut audio_clip = Clip::new("My Sequence Audio");
	audio_clip.set_source_range(TimeRange::new(
		RationalTime::new(0.0, 24.0),
		RationalTime::new(1152.0, 24.0),
	));
	audio_clip.set_media_reference(MediaReference::ExternalReference(ExternalReference::new(
		"file:///tmp/My Sequence.wav",
		Some(TimeRange::new(
			RationalTime::new(0.0, 48000.0),
			RationalTime::new(0.0, 48000.0),
		)),
	)));
	audio.append_child(Composable::Clip(audio_clip));

	audio.append_child(Composable::Gap(Gap::new(
		TimeRange::new(
			RationalTime::new(1152.0, 24.0),
			RationalTime::new(12.0, 1.0),
		),
		"",
	)));

	timeline.tracks_mut().append_child(Composable::Track(video));
	timeline.tracks_mut().append_child(Composable::Track(audio));

	timeline
}

#[test]
fn saved_timeline_matches_golden_bytes() {
	let golden = std::fs::read_to_string(concat!(
		env!("CARGO_MANIFEST_DIR"),
		"/tests/data/golden_timeline.json"
	))
	.expect("read golden_timeline.json");

	let built = build_golden_timeline();
	let out = built.to_json_string().expect("serialize built timeline");

	assert_eq!(out, golden);
}

#[test]
fn saved_timeline_reparses_identically() {
	// The builder output must also parse back into an equivalent graph.
	let built = build_golden_timeline();
	let out = built.to_json_string().unwrap();
	let reparsed = oak_otio::from_json_string(&out).unwrap();
	assert_eq!(
		reparsed.as_timeline().unwrap().name(),
		"My Sequence",
		"reparsed timeline keeps the name"
	);
	assert_eq!(reparsed.as_timeline().unwrap().tracks().children().len(), 2);
}
