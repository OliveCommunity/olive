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

//! Semantic tests over `golden_timeline.json`: walk the parsed object graph
//! and assert the values Oak's C++ load task depends on.

use oakotio::{Clip, ExternalReference, MediaReference, Serializable, Timeline};

fn golden_timeline() -> Timeline {
    let text = std::fs::read_to_string(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/tests/data/golden_timeline.json"
    ))
    .expect("read golden_timeline.json");
    match oakotio::from_json_string(&text).expect("parse golden_timeline.json") {
        Serializable::Timeline(t) => t,
        other => panic!("expected Timeline root, got {}", other.schema_name()),
    }
}

#[test]
fn root_timeline_fields() {
    let tl = golden_timeline();
    assert_eq!(tl.name(), "My Sequence");
    assert_eq!(tl.global_start_time(), None);
    assert_eq!(tl.tracks().children().len(), 2);
}

#[test]
fn video_track_contents() {
    let tl = golden_timeline();
    let video = &tl.tracks().children()[0];
    let track = video
        .as_track()
        .expect("first child of tracks stack is a Track");
    assert_eq!(track.kind(), "Video");
    assert_eq!(track.children().len(), 3);

    // Clip -> Gap -> Transition, in order.
    let clip = track.children()[0]
        .as_clip()
        .expect("video track child 0 is a Clip");
    assert_eq!(clip.name(), "My Sequence Clip");
    let range = clip.source_range().expect("clip has a source_range");
    assert_eq!((range.duration().value(), range.duration().rate()), (1152.0, 24.0));
    assert_eq!((range.start_time().value(), range.start_time().rate()), (0.0, 24.0));

    let gap = track.children()[1]
        .as_gap()
        .expect("video track child 1 is a Gap");
    assert_eq!(gap.name(), "My Sequence Gap");
    let grange = gap.source_range().expect("gap has a source_range");
    assert_eq!((grange.duration().value(), grange.duration().rate()), (576.0, 24.0));

    let trans = track.children()[2]
        .as_transition()
        .expect("video track child 2 is a Transition");
    assert_eq!(trans.name(), "My Sequence Transition");
    assert_eq!((trans.in_offset().value(), trans.in_offset().rate()), (12.0, 24.0));
    assert_eq!((trans.out_offset().value(), trans.out_offset().rate()), (12.0, 24.0));
    assert_eq!(trans.transition_type(), "");
}

#[test]
fn video_clip_media_reference() {
    let tl = golden_timeline();
    let clip = &tl.tracks().children()[0].as_track().unwrap().children()[0];
    let clip = clip.as_clip().unwrap();

    let reference = clip.media_reference().expect("clip resolves a media reference");
    assert_eq!(reference.schema_name(), "ExternalReference");
    let external: &ExternalReference = match reference {
        MediaReference::ExternalReference(e) => e,
        other => panic!("expected ExternalReference, got {}", other.schema_name()),
    };
    assert_eq!(external.target_url(), "file:///tmp/My Sequence.mp4");
    let available = external.available_range().expect("available_range is set");
    assert_eq!((available.duration().value(), available.duration().rate()), (100.0, 25.0));
    assert_eq!((available.start_time().value(), available.start_time().rate()), (0.0, 25.0));
}

#[test]
fn audio_track_contents() {
    let tl = golden_timeline();
    let audio = &tl.tracks().children()[1];
    let track = audio
        .as_track()
        .expect("second child of tracks stack is a Track");
    assert_eq!(track.kind(), "Audio");
    assert_eq!(track.children().len(), 2);

    let clip: &Clip = track.children()[0]
        .as_clip()
        .expect("audio track child 0 is a Clip");
    assert_eq!(clip.name(), "My Sequence Audio");
    let external: &ExternalReference = match clip.media_reference().unwrap() {
        MediaReference::ExternalReference(e) => e,
        other => panic!("expected ExternalReference, got {}", other.schema_name()),
    };
    assert_eq!(external.target_url(), "file:///tmp/My Sequence.wav");
    let available = external.available_range().unwrap();
    assert_eq!((available.duration().value(), available.duration().rate()), (0.0, 48000.0));
    assert_eq!((available.start_time().value(), available.start_time().rate()), (0.0, 48000.0));

    let gap = track.children()[1]
        .as_gap()
        .expect("audio track child 1 is a Gap");
    assert_eq!(gap.name(), "");
    let grange = gap.source_range().unwrap();
    assert_eq!((grange.duration().value(), grange.duration().rate()), (12.0, 1.0));
    assert_eq!((grange.start_time().value(), grange.start_time().rate()), (1152.0, 24.0));
}
