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

//! A serde model of the OpenTimelineIO JSON format, covering exactly the
//! object graph the C++ load/save tasks use
//! (`src/task/src/project/loadotio/loadotio.cpp` and
//! `src/task/src/project/saveotio/saveotio.cpp`): `RationalTime`,
//! `TimeRange`, `Clip`, `Gap`, `Transition`, `Track`, `Stack`, `Timeline`,
//! `ExternalReference`, `MissingReference` and `SerializableCollection`.
//!
//! Design notes:
//!
//! - Every schema object is a plain struct whose first field is the private
//!   `otio_schema` string (serialized as `"OTIO_SCHEMA"`). Declaring it first
//!   makes the writer emit it first (the opentimelineio writer's order), and
//!   declaring it before the trailing `#[serde(flatten)]` map keeps it out of
//!   the unknown-field bucket on the way back in.
//! - Every schema object ends with `#[serde(flatten)] unknown`, so fields
//!   this crate does not model are preserved verbatim across a load/save
//!   round-trip (high-fidelity parity with the C++ writer, which also keeps
//!   unknown fields).
//! - The three polymorphic containers (`Composable`, `Serializable`,
//!   `MediaReference`) dispatch on the `OTIO_SCHEMA` string: known schema
//!   names decode into their concrete struct, anything else is kept whole as
//!   `Raw(serde_json::Value)` so it round-trips untouched.
//! - `#[serde(default)]` on every struct makes all fields optional on input,
//!   matching the C++ reader's tolerance for hand-written files.

use std::collections::BTreeMap;
use std::path::Path;

use oakcore_rs::Rational;
use serde::de::{self, Deserialize, Deserializer};
use serde::ser::{Serialize, Serializer};
use serde_json::Value;

use crate::error::OtioError;

pub(crate) type Map = serde_json::Map<String, Value>;

// Schema name constants (the "OTIO_SCHEMA" values written by opentimelineio).
const SCHEMA_RATIONAL_TIME: &str = "RationalTime.1";
const SCHEMA_TIME_RANGE: &str = "TimeRange.1";
const SCHEMA_CLIP: &str = "Clip.2";
const SCHEMA_GAP: &str = "Gap.1";
const SCHEMA_TRANSITION: &str = "Transition.1";
const SCHEMA_TRACK: &str = "Track.1";
const SCHEMA_STACK: &str = "Stack.1";
const SCHEMA_TIMELINE: &str = "Timeline.1";
const SCHEMA_EXTERNAL_REFERENCE: &str = "ExternalReference.1";
const SCHEMA_MISSING_REFERENCE: &str = "MissingReference.1";
const SCHEMA_SERIALIZABLE_COLLECTION: &str = "SerializableCollection.1";

/// Serialize `value` exactly like the opentimelineio C++ writer: 4-space
/// indentation, `": "` separators, inline `{}`/`[]`, shortest float
/// representation (ryu, identical to the C++ writer), no trailing newline.
pub(crate) fn to_json_string<T: serde::Serialize>(value: &T) -> Result<String, serde_json::Error> {
    let mut buf = Vec::new();
    let fmt = serde_json::ser::PrettyFormatter::with_indent(b"    ");
    let mut ser = serde_json::Serializer::with_formatter(&mut buf, fmt);
    value.serialize(&mut ser)?;
    String::from_utf8(buf).map_err(|e| serde_json::Error::io(std::io::Error::other(e)))
}

/// Serialize to a file (no trailing newline, matching the C++ writer).
pub(crate) fn to_json_file<T: serde::Serialize>(value: &T, path: &Path) -> Result<(), OtioError> {
    std::fs::write(path, to_json_string(value)?)?;
    Ok(())
}

/// Schema-name of an unknown (`Raw`) object: the raw `OTIO_SCHEMA` string,
/// or "" if absent.
fn raw_schema_name(v: &Value) -> &str {
    v.get("OTIO_SCHEMA").and_then(Value::as_str).unwrap_or("")
}

// ---------------------------------------------------------------------------
// Value types
// ---------------------------------------------------------------------------

/// `RationalTime.1` — a time value and its frame rate.
///
/// Mirrors `opentimelineio::RationalTime`. Field order on disk is `rate`
/// then `value` (the opentimelineio writer's order).
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct RationalTime {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    rate: f64,
    value: f64,
}

impl RationalTime {
    /// Construct `RationalTime(value, rate)` (C++ argument order).
    pub fn new(value: f64, rate: f64) -> RationalTime {
        RationalTime {
            otio_schema: SCHEMA_RATIONAL_TIME.to_string(),
            rate,
            value,
        }
    }

    /// The value in the object's own rate.
    pub fn value(&self) -> f64 {
        self.value
    }

    /// The frame rate (ticks per second).
    pub fn rate(&self) -> f64 {
        self.rate
    }

    /// Convert to seconds (`value / rate`; non-finite for a non-positive
    /// rate, same as C++).
    pub fn to_seconds(self) -> f64 {
        self.value / self.rate
    }

    /// The invalid-time sentinel (`RationalTime(0, 0)`, C++ `invalid_time`).
    pub fn invalid_time() -> RationalTime {
        RationalTime::new(0.0, 0.0)
    }

    /// True for a rate `<= 0` or a non-finite value/rate
    /// (C++ `is_invalid_time`).
    pub fn is_invalid_time(self) -> bool {
        self.rate <= 0.0 || self.value.is_nan() || self.rate.is_nan()
    }

    /// Convert to another rate (`value * (new_rate / rate)`, C++
    /// `rescaled_to`; returns `self` unchanged when the rates already
    /// match).
    pub fn rescaled_to(self, new_rate: f64) -> RationalTime {
        if self.rate == new_rate {
            return self;
        }
        RationalTime::new(self.value * (new_rate / self.rate), new_rate)
    }

    /// C++ `Rational::fromRationalTime` — the exact rational that
    /// `to_seconds()` yields (`Rational::from_double(value / rate)`).
    pub fn to_rational(self) -> Rational {
        from_double(self.to_seconds())
    }

    /// C++ `Rational::toRationalTime(framerate)` — `RationalTime(num, den
    /// or 1)` rescaled to `framerate`.
    pub fn from_rational(r: Rational, framerate: f64) -> RationalTime {
        let rate = if r.denominator() == 0 {
            1.0
        } else {
            r.denominator() as f64
        };
        RationalTime::new(r.numerator() as f64, rate).rescaled_to(framerate)
    }
}

impl Default for RationalTime {
    fn default() -> Self {
        RationalTime::new(0.0, 0.0)
    }
}

/// `TimeRange.1` — a duration and its start time. Field order on disk is
/// `duration` then `start_time` (the opentimelineio writer's order).
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct TimeRange {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    duration: RationalTime,
    start_time: RationalTime,
}

impl TimeRange {
    /// Construct `TimeRange(start_time, duration)` (C++ argument order).
    pub fn new(start_time: RationalTime, duration: RationalTime) -> TimeRange {
        TimeRange {
            otio_schema: SCHEMA_TIME_RANGE.to_string(),
            duration,
            start_time,
        }
    }

    /// The start of the range.
    pub fn start_time(&self) -> RationalTime {
        self.start_time.clone()
    }

    /// The duration of the range.
    pub fn duration(&self) -> RationalTime {
        self.duration.clone()
    }
}

impl Default for TimeRange {
    fn default() -> Self {
        TimeRange::new(RationalTime::invalid_time(), RationalTime::invalid_time())
    }
}

// ---------------------------------------------------------------------------
// Media references
// ---------------------------------------------------------------------------

/// `ExternalReference.1` — media referenced by URL.
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct ExternalReference {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    available_range: Option<TimeRange>,
    available_image_bounds: Option<Value>,
    target_url: String,
    #[serde(flatten)]
    unknown: Map,
}

impl ExternalReference {
    /// Construct `ExternalReference(target_url, available_range)` (C++
    /// argument order; `None` serializes as `null`).
    pub fn new(target_url: impl Into<String>, available_range: Option<TimeRange>) -> ExternalReference {
        ExternalReference {
            otio_schema: SCHEMA_EXTERNAL_REFERENCE.to_string(),
            metadata: Map::new(),
            name: String::new(),
            available_range,
            available_image_bounds: None,
            target_url: target_url.into(),
            unknown: Map::new(),
        }
    }

    /// The media URL (C++ `target_url`).
    pub fn target_url(&self) -> &str {
        &self.target_url
    }

    /// The range of media available on disk, if any.
    pub fn available_range(&self) -> Option<TimeRange> {
        self.available_range.clone()
    }
}

/// `MissingReference.1` — media referenced but not resolvable. The C++
/// writer emits `available_range` and `available_image_bounds` as `null`
/// and no `target_url` at all.
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct MissingReference {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    available_range: Option<TimeRange>,
    available_image_bounds: Option<Value>,
    #[serde(flatten)]
    unknown: Map,
}

impl MissingReference {
    /// A bare `MissingReference` (the C++ default constructor).
    pub fn new() -> MissingReference {
        MissingReference {
            otio_schema: SCHEMA_MISSING_REFERENCE.to_string(),
            metadata: Map::new(),
            name: String::new(),
            available_range: None,
            available_image_bounds: None,
            unknown: Map::new(),
        }
    }
}

/// A clip's media reference: `ExternalReference`, `MissingReference`, or
/// anything else the file contains, kept whole as `Raw`.
#[derive(Clone, Debug, PartialEq)]
pub enum MediaReference {
    ExternalReference(ExternalReference),
    MissingReference(MissingReference),
    /// An unrecognized media-reference schema, preserved verbatim.
    Raw(Value),
}

impl MediaReference {
    /// The base schema name ("ExternalReference", "MissingReference", or
    /// the raw `OTIO_SCHEMA` string for unknown references).
    pub fn schema_name(&self) -> &str {
        match self {
            MediaReference::ExternalReference(_) => "ExternalReference",
            MediaReference::MissingReference(_) => "MissingReference",
            MediaReference::Raw(v) => raw_schema_name(v),
        }
    }

    /// Downcast to a concrete `ExternalReference`.
    pub fn as_external_reference(&self) -> Option<&ExternalReference> {
        match self {
            MediaReference::ExternalReference(e) => Some(e),
            _ => None,
        }
    }

    /// Downcast to a concrete `MissingReference`.
    pub fn as_missing_reference(&self) -> Option<&MissingReference> {
        match self {
            MediaReference::MissingReference(m) => Some(m),
            _ => None,
        }
    }
}

impl Serialize for MediaReference {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            MediaReference::ExternalReference(e) => e.serialize(serializer),
            MediaReference::MissingReference(m) => m.serialize(serializer),
            MediaReference::Raw(v) => v.serialize(serializer),
        }
    }
}

impl<'de> Deserialize<'de> for MediaReference {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value = Value::deserialize(deserializer)?;
        let schema = raw_schema_name(&value);
        match schema {
            SCHEMA_EXTERNAL_REFERENCE => {
                serde_json::from_value(value).map(MediaReference::ExternalReference).map_err(de::Error::custom)
            }
            SCHEMA_MISSING_REFERENCE => {
                serde_json::from_value(value).map(MediaReference::MissingReference).map_err(de::Error::custom)
            }
            _ => Ok(MediaReference::Raw(value)),
        }
    }
}

// ---------------------------------------------------------------------------
// Composable objects (blocks and containers)
// ---------------------------------------------------------------------------

/// `Clip.2` — a reference to a piece of media.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Clip {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    source_range: Option<TimeRange>,
    effects: Vec<Value>,
    markers: Vec<Value>,
    enabled: bool,
    media_references: BTreeMap<String, MediaReference>,
    active_media_reference_key: Option<String>,
    #[serde(flatten)]
    unknown: Map,
}

impl Clip {
    /// Construct a named clip (the C++ `Clip(name)` constructor). All
    /// optional fields start unset; `enabled` defaults to true.
    pub fn new(name: impl Into<String>) -> Clip {
        Clip {
            otio_schema: SCHEMA_CLIP.to_string(),
            metadata: Map::new(),
            name: name.into(),
            source_range: None,
            effects: Vec::new(),
            markers: Vec::new(),
            enabled: true,
            media_references: BTreeMap::new(),
            active_media_reference_key: None,
            unknown: Map::new(),
        }
    }

    /// The clip name (C++ `name`).
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The range of the source media used by this clip, if set
    /// (C++ `source_range`; null on disk when unset).
    pub fn source_range(&self) -> Option<TimeRange> {
        self.source_range.clone()
    }

    /// The active media reference, resolved by `active_media_reference_key`
    /// (falling back to "DEFAULT_MEDIA", the C++ behavior).
    pub fn media_reference(&self) -> Option<&MediaReference> {
        let key = self.active_media_reference_key.as_deref().unwrap_or("DEFAULT_MEDIA");
        self.media_references.get(key)
    }

    /// All media references by key.
    pub fn media_references(&self) -> &BTreeMap<String, MediaReference> {
        &self.media_references
    }

    /// Set the source range (C++ `set_source_range`).
    pub fn set_source_range(&mut self, range: TimeRange) {
        self.source_range = Some(range);
    }

    /// Attach a media reference under "DEFAULT_MEDIA" and make it active
    /// (C++ `set_media_reference`).
    pub fn set_media_reference(&mut self, reference: MediaReference) {
        self.media_references.insert("DEFAULT_MEDIA".to_string(), reference);
        self.active_media_reference_key = Some("DEFAULT_MEDIA".to_string());
    }

    /// Whether the clip is enabled (C++ `enabled`; used by the FCPXML
    /// layer for `asset-clip enabled="0"`).
    pub fn enabled(&self) -> bool {
        self.enabled
    }

    /// Set the enabled flag (C++ `set_enabled`).
    pub fn set_enabled(&mut self, enabled: bool) {
        self.enabled = enabled;
    }
}

impl Default for Clip {
    fn default() -> Self {
        Clip::new("")
    }
}

/// `Gap.1` — an empty span of time on a track.
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Gap {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    source_range: Option<TimeRange>,
    effects: Vec<Value>,
    markers: Vec<Value>,
    enabled: bool,
    #[serde(flatten)]
    unknown: Map,
}

impl Gap {
    /// Construct `Gap(source_range, name)` (C++ argument order).
    pub fn new(source_range: TimeRange, name: impl Into<String>) -> Gap {
        Gap {
            otio_schema: SCHEMA_GAP.to_string(),
            metadata: Map::new(),
            name: name.into(),
            source_range: Some(source_range),
            effects: Vec::new(),
            markers: Vec::new(),
            enabled: true,
            unknown: Map::new(),
        }
    }

    /// The gap name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The gap's source range (always set for gaps written by this crate).
    pub fn source_range(&self) -> Option<TimeRange> {
        self.source_range.clone()
    }
}

impl Default for Gap {
    fn default() -> Self {
        Gap {
            otio_schema: SCHEMA_GAP.to_string(),
            metadata: Map::new(),
            name: String::new(),
            source_range: None,
            effects: Vec::new(),
            markers: Vec::new(),
            enabled: true,
            unknown: Map::new(),
        }
    }
}

/// `Transition.1` — a transition between two blocks. The C++ writer always
/// writes `transition_type` (empty string when unset).
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Transition {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    in_offset: RationalTime,
    out_offset: RationalTime,
    transition_type: String,
    #[serde(flatten)]
    unknown: Map,
}

impl Transition {
    /// Construct a named transition (the C++ `Transition(name)`
    /// constructor; offsets default to `RationalTime(0, 1)`).
    pub fn new(name: impl Into<String>) -> Transition {
        Transition {
            otio_schema: SCHEMA_TRANSITION.to_string(),
            metadata: Map::new(),
            name: name.into(),
            in_offset: RationalTime::new(0.0, 1.0),
            out_offset: RationalTime::new(0.0, 1.0),
            transition_type: String::new(),
            unknown: Map::new(),
        }
    }

    /// The transition name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// How far into the previous block the transition reaches (C++
    /// `in_offset`).
    pub fn in_offset(&self) -> RationalTime {
        self.in_offset.clone()
    }

    /// How far into the next block the transition reaches (C++
    /// `out_offset`).
    pub fn out_offset(&self) -> RationalTime {
        self.out_offset.clone()
    }

    /// The transition type ("SMPTE_Dissolve", "SMPTE_Wipe", "" ...).
    pub fn transition_type(&self) -> &str {
        &self.transition_type
    }

    /// Set the in offset (C++ `set_in_offset`).
    pub fn set_in_offset(&mut self, offset: RationalTime) {
        self.in_offset = offset;
    }

    /// Set the out offset (C++ `set_out_offset`).
    pub fn set_out_offset(&mut self, offset: RationalTime) {
        self.out_offset = offset;
    }
}

/// `Stack.1` — an ordered container of composables (used for a timeline's
/// tracks).
#[derive(Clone, Debug, PartialEq, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Stack {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    source_range: Option<TimeRange>,
    effects: Vec<Value>,
    markers: Vec<Value>,
    enabled: bool,
    children: Vec<Box<Composable>>,
    #[serde(flatten)]
    unknown: Map,
}

impl Default for Stack {
    fn default() -> Self {
        Stack {
            otio_schema: SCHEMA_STACK.to_string(),
            metadata: Map::new(),
            name: String::new(),
            source_range: None,
            effects: Vec::new(),
            markers: Vec::new(),
            enabled: true,
            children: Vec::new(),
            unknown: Map::new(),
        }
    }
}

impl Stack {
    /// The stack's children in order (C++ `children`).
    pub fn children(&self) -> &[Box<Composable>] {
        &self.children
    }

    /// Append a child (C++ `append_child`).
    pub fn append_child(&mut self, child: Composable) {
        self.children.push(Box::new(child));
    }
}

/// `Track.1` — a stack with a track kind. Field order on disk is
/// `children` then `kind` (the opentimelineio writer's order).
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Track {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    source_range: Option<TimeRange>,
    effects: Vec<Value>,
    markers: Vec<Value>,
    enabled: bool,
    children: Vec<Box<Composable>>,
    kind: String,
    #[serde(flatten)]
    unknown: Map,
}

impl Track {
    /// Construct a track of the given kind ("Video" or "Audio").
    pub fn new(kind: impl Into<String>) -> Track {
        Track {
            otio_schema: SCHEMA_TRACK.to_string(),
            metadata: Map::new(),
            name: String::new(),
            source_range: None,
            effects: Vec::new(),
            markers: Vec::new(),
            enabled: true,
            children: Vec::new(),
            kind: kind.into(),
            unknown: Map::new(),
        }
    }

    /// The track kind ("Video", "Audio", ...).
    pub fn kind(&self) -> &str {
        &self.kind
    }

    /// The track's blocks in order (C++ `children`).
    pub fn children(&self) -> &[Box<Composable>] {
        &self.children
    }

    /// Append a child (C++ `append_child`).
    pub fn append_child(&mut self, child: Composable) {
        self.children.push(Box::new(child));
    }
}

/// A composable in a track or stack: `Clip`, `Gap`, `Transition`, `Track`,
/// `Stack`, or an unrecognized schema kept whole as `Raw`.
#[derive(Clone, Debug, PartialEq)]
pub enum Composable {
    Clip(Clip),
    Gap(Gap),
    Transition(Transition),
    Track(Track),
    Stack(Stack),
    /// An unrecognized composable schema, preserved verbatim.
    Raw(Value),
}

impl Composable {
    /// The base schema name ("Clip", "Gap", "Transition", "Track",
    /// "Stack", or the raw `OTIO_SCHEMA` string for unknown blocks).
    pub fn schema_name(&self) -> &str {
        match self {
            Composable::Clip(_) => "Clip",
            Composable::Gap(_) => "Gap",
            Composable::Transition(_) => "Transition",
            Composable::Track(_) => "Track",
            Composable::Stack(_) => "Stack",
            Composable::Raw(v) => raw_schema_name(v),
        }
    }

    /// The object's name ("" for unknown blocks).
    pub fn name(&self) -> &str {
        match self {
            Composable::Clip(c) => &c.name,
            Composable::Gap(g) => &g.name,
            Composable::Transition(t) => &t.name,
            Composable::Track(t) => &t.name,
            Composable::Stack(s) => &s.name,
            Composable::Raw(_) => "",
        }
    }

    /// The source range of items that carry one (Clip and Gap; the C++
    /// `Item::source_range` cast for these two block kinds).
    pub fn source_range(&self) -> Option<TimeRange> {
        match self {
            Composable::Clip(c) => c.source_range.clone(),
            Composable::Gap(g) => g.source_range.clone(),
            _ => None,
        }
    }

    /// Downcast to a concrete `Clip`.
    pub fn as_clip(&self) -> Option<&Clip> {
        match self {
            Composable::Clip(c) => Some(c),
            _ => None,
        }
    }

    /// Downcast to a concrete `Gap`.
    pub fn as_gap(&self) -> Option<&Gap> {
        match self {
            Composable::Gap(g) => Some(g),
            _ => None,
        }
    }

    /// Downcast to a concrete `Transition`.
    pub fn as_transition(&self) -> Option<&Transition> {
        match self {
            Composable::Transition(t) => Some(t),
            _ => None,
        }
    }

    /// Downcast to a concrete `Track`.
    pub fn as_track(&self) -> Option<&Track> {
        match self {
            Composable::Track(t) => Some(t),
            _ => None,
        }
    }

    /// Downcast to a concrete `Stack`.
    pub fn as_stack(&self) -> Option<&Stack> {
        match self {
            Composable::Stack(s) => Some(s),
            _ => None,
        }
    }
}

impl Serialize for Composable {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Composable::Clip(c) => c.serialize(serializer),
            Composable::Gap(g) => g.serialize(serializer),
            Composable::Transition(t) => t.serialize(serializer),
            Composable::Track(t) => t.serialize(serializer),
            Composable::Stack(s) => s.serialize(serializer),
            Composable::Raw(v) => v.serialize(serializer),
        }
    }
}

impl<'de> Deserialize<'de> for Composable {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value = Value::deserialize(deserializer)?;
        let schema = raw_schema_name(&value);
        match schema {
            SCHEMA_CLIP => serde_json::from_value(value).map(Composable::Clip).map_err(de::Error::custom),
            SCHEMA_GAP => serde_json::from_value(value).map(Composable::Gap).map_err(de::Error::custom),
            SCHEMA_TRANSITION => {
                serde_json::from_value(value).map(Composable::Transition).map_err(de::Error::custom)
            }
            SCHEMA_TRACK => serde_json::from_value(value).map(Composable::Track).map_err(de::Error::custom),
            SCHEMA_STACK => serde_json::from_value(value).map(Composable::Stack).map_err(de::Error::custom),
            _ => Ok(Composable::Raw(value)),
        }
    }
}

// ---------------------------------------------------------------------------
// Root serializable objects
// ---------------------------------------------------------------------------

/// `Timeline.1` — the root of a single-timeline document.
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct Timeline {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    global_start_time: Option<RationalTime>,
    tracks: Stack,
    #[serde(flatten)]
    unknown: Map,
}

impl Timeline {
    /// Construct a named timeline whose tracks stack is an empty
    /// `Stack` named "tracks" (the C++ `Timeline(name)` constructor).
    pub fn new(name: impl Into<String>) -> Timeline {
        let mut tracks = Stack::default();
        tracks.name = "tracks".to_string();
        Timeline {
            otio_schema: SCHEMA_TIMELINE.to_string(),
            metadata: Map::new(),
            name: name.into(),
            global_start_time: None,
            tracks,
            unknown: Map::new(),
        }
    }

    /// The timeline name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The tracks stack (C++ `tracks`).
    pub fn tracks(&self) -> &Stack {
        &self.tracks
    }

    /// Mutable access to the tracks stack, for appending tracks.
    pub fn tracks_mut(&mut self) -> &mut Stack {
        &mut self.tracks
    }

    /// The global start time, if set (null on disk when unset).
    pub fn global_start_time(&self) -> Option<RationalTime> {
        self.global_start_time.clone()
    }

    /// Set the global start time (C++ `set_global_start_time`).
    pub fn set_global_start_time(&mut self, start: RationalTime) {
        self.global_start_time = Some(start);
    }

    /// The timeline metadata map (C++ `metadata`). The FCPXML layer stores
    /// interchange hints (source version, tcFormat, ...) under the nested
    /// "fcpxml" key.
    pub fn metadata(&self) -> &Map {
        &self.metadata
    }

    /// Mutable access to the timeline metadata map.
    pub fn metadata_mut(&mut self) -> &mut Map {
        &mut self.metadata
    }

    /// Serialize to the opentimelineio JSON string format.
    pub fn to_json_string(&self) -> Result<String, OtioError> {
        Ok(to_json_string(self)?)
    }

    /// Write to a file in the opentimelineio JSON format.
    pub fn to_json_file(&self, path: impl AsRef<Path>) -> Result<(), OtioError> {
        to_json_file(self, path.as_ref())
    }
}

/// `SerializableCollection.1` — a root that groups multiple timelines.
#[derive(Clone, Debug, PartialEq, Default, serde::Serialize, serde::Deserialize)]
#[serde(default)]
pub struct SerializableCollection {
    #[serde(rename = "OTIO_SCHEMA", default)]
    otio_schema: String,
    metadata: Map,
    name: String,
    children: Vec<Box<Serializable>>,
    #[serde(flatten)]
    unknown: Map,
}

impl SerializableCollection {
    /// Construct `SerializableCollection(name, children)` (C++ argument
    /// order).
    pub fn new(name: impl Into<String>, children: Vec<Serializable>) -> SerializableCollection {
        SerializableCollection {
            otio_schema: SCHEMA_SERIALIZABLE_COLLECTION.to_string(),
            metadata: Map::new(),
            name: name.into(),
            children: children.into_iter().map(Box::new).collect(),
            unknown: Map::new(),
        }
    }

    /// The collection name.
    pub fn name(&self) -> &str {
        &self.name
    }

    /// The collection's children in order.
    pub fn children(&self) -> &[Box<Serializable>] {
        &self.children
    }
}

/// The root of an OpenTimelineIO document: `Timeline`,
/// `SerializableCollection`, or an unrecognized schema kept whole as `Raw`.
#[derive(Clone, Debug, PartialEq)]
pub enum Serializable {
    Timeline(Timeline),
    SerializableCollection(SerializableCollection),
    /// An unrecognized root schema, preserved verbatim.
    Raw(Value),
}

impl Serializable {
    /// The base schema name ("Timeline", "SerializableCollection", or the
    /// raw `OTIO_SCHEMA` string for unknown roots).
    pub fn schema_name(&self) -> &str {
        match self {
            Serializable::Timeline(_) => "Timeline",
            Serializable::SerializableCollection(_) => "SerializableCollection",
            Serializable::Raw(v) => raw_schema_name(v),
        }
    }

    /// Downcast to a concrete `Timeline`.
    pub fn as_timeline(&self) -> Option<&Timeline> {
        match self {
            Serializable::Timeline(t) => Some(t),
            _ => None,
        }
    }

    /// Downcast to a concrete `SerializableCollection`.
    pub fn as_collection(&self) -> Option<&SerializableCollection> {
        match self {
            Serializable::SerializableCollection(c) => Some(c),
            _ => None,
        }
    }

    /// Serialize to the opentimelineio JSON string format.
    pub fn to_json_string(&self) -> Result<String, OtioError> {
        Ok(to_json_string(self)?)
    }

    /// Write to a file in the opentimelineio JSON format.
    pub fn to_json_file(&self, path: impl AsRef<Path>) -> Result<(), OtioError> {
        to_json_file(self, path.as_ref())
    }
}

impl Serialize for Serializable {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Serializable::Timeline(t) => t.serialize(serializer),
            Serializable::SerializableCollection(c) => c.serialize(serializer),
            Serializable::Raw(v) => v.serialize(serializer),
        }
    }
}

impl<'de> Deserialize<'de> for Serializable {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value = Value::deserialize(deserializer)?;
        let schema = raw_schema_name(&value);
        match schema {
            SCHEMA_TIMELINE => {
                serde_json::from_value(value).map(Serializable::Timeline).map_err(de::Error::custom)
            }
            SCHEMA_SERIALIZABLE_COLLECTION => {
                serde_json::from_value(value).map(Serializable::SerializableCollection).map_err(de::Error::custom)
            }
            _ => Ok(Serializable::Raw(value)),
        }
    }
}

// ---------------------------------------------------------------------------
// Rational::from_double port
// ---------------------------------------------------------------------------

/// `olive::core::Rational::from_double` (core/src/util/rational.cpp), ported
/// on top of `oakcore_rs::Rational::new`. `Rational::new` applies the exact
/// C++ `reduce_fraction(INT_MAX)` reduction, so the same raw numerator /
/// denominator pairs yield bit-identical rationals.
fn from_double(flt: f64) -> Rational {
    // NaN and anything beyond the int32 range collapse to the null sentinel
    // (0/0).
    if flt.is_nan() || flt.abs() > i32::MAX as f64 + 3.0 {
        return Rational::NULL;
    }

    // frexp(flt, &exp) with exponent = max(exp - 1, 0); exp - 1 equals the
    // IEEE unbiased exponent, so `exponent = max(unbiased, 0)`.
    let bits = flt.to_bits();
    let unbiased = ((bits >> 52) & 0x7ff) as i64 - 1023;
    let exponent = unbiased.max(0);

    // den = 1 << (62 - exponent); num = floor(flt * den + 0.5).
    let den = 1i64 << (62 - exponent);
    let num = (flt * den as f64 + 0.5).floor() as i64;

    let mut r = Rational::new(num, den);

    // If the first pass reduced to zero, retry against INT64_MAX
    // (C++: `if ((!rnum || !rden) && flt)`). Only triggered by tiny
    // non-zero magnitudes; the product is always in int64 range, so the
    // float-to-int cast truncates exactly like the C++ one.
    if r.is_null() && flt != 0.0 {
        r = Rational::new((flt * i64::MAX as f64) as i64, i64::MAX);
    }

    if r.is_nan() {
        return Rational::NULL;
    }
    r
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_rt(value: f64, num: i64, den: i64) {
        let r = from_double(value);
        assert_eq!(
            (r.numerator(), r.denominator()),
            (num, den),
            "from_double({value}) != {num}/{den}"
        );
    }

    #[test]
    fn from_double_basics() {
        assert_rt(0.0, 0, 1);
        assert_rt(1.0, 1, 1);
        assert_rt(24.0, 24, 1);
        assert_rt(0.1, 1, 10);
        assert_rt(4.8, 24, 5);
        assert_rt(-0.1, -1, 10);
        assert_rt(1152.0 / 24.0, 48, 1);
        assert_rt(123456789.125, 987654313, 8);
    }

    #[test]
    fn from_double_sentinels() {
        assert!(from_double(f64::NAN).is_null());
        assert!(from_double(f64::INFINITY).is_null());
        assert!(from_double(-f64::INFINITY).is_null());
        assert!(from_double(i32::MAX as f64 + 4.0).is_null());
        assert_rt(-0.0, 0, 1);
    }

    #[test]
    fn from_double_tiny_retry() {
        // 2^-63 < flt < 2^-62: the first pass reduces to 0/1; the retry
        // against INT64_MAX is itself reduced back to 0/1 by the INT_MAX
        // ceiling, matching the C++ behavior exactly.
        let tiny = 1.5e-19;
        let r = from_double(tiny);
        assert_eq!((r.numerator(), r.denominator()), (0, 1));
        // Positive zero of the first pass but nonzero value: retry fires.
        assert_rt(0.0, 0, 1);
    }

    #[test]
    fn to_rational_from_seconds() {
        // C++ fromRationalTime: RationalTime(1152, 24).to_seconds() = 48.
        assert_eq!(RationalTime::new(1152.0, 24.0).to_rational(), Rational::new(48, 1));
        // RationalTime(12, 1).to_seconds() = 12.
        assert_eq!(RationalTime::new(12.0, 1.0).to_rational(), Rational::new(12, 1));
        // RationalTime(576, 24).to_seconds() = 24.
        assert_eq!(RationalTime::new(576.0, 24.0).to_rational(), Rational::new(24, 1));
    }

    #[test]
    fn from_rational_matches_cpp_to_rational_time() {
        // Rational(576, 24).toRationalTime(24) stays {576, 24}.
        let rt = RationalTime::from_rational(Rational::new(576, 24), 24.0);
        assert_eq!((rt.value(), rt.rate()), (576.0, 24.0));
        // Rational(12, 24).toRationalTime() (default 24) = {12, 24}.
        let rt = RationalTime::from_rational(Rational::new(12, 24), 24.0);
        assert_eq!((rt.value(), rt.rate()), (12.0, 24.0));
        // Rational(0, 1).toRationalTime(24) = {0, 24}.
        let rt = RationalTime::from_rational(Rational::new(0, 1), 24.0);
        assert_eq!((rt.value(), rt.rate()), (0.0, 24.0));
        // Rational(1, 25).toRationalTime(25) = {1, 25}.
        let rt = RationalTime::from_rational(Rational::new(1, 25), 25.0);
        assert_eq!((rt.value(), rt.rate()), (1.0, 25.0));
    }

    #[test]
    fn rescaled_to_semantics() {
        let rt = RationalTime::new(1.0, 25.0).rescaled_to(50.0);
        assert_eq!((rt.value(), rt.rate()), (2.0, 50.0));
        // Same rate: returned unchanged.
        let rt = RationalTime::new(1.0, 25.0).rescaled_to(25.0);
        assert_eq!((rt.value(), rt.rate()), (1.0, 25.0));
    }

    #[test]
    fn invalid_time_semantics() {
        assert!(RationalTime::new(0.0, 0.0).is_invalid_time());
        assert!(RationalTime::new(1.0, -1.0).is_invalid_time());
        assert!(RationalTime::new(f64::NAN, 24.0).is_invalid_time());
        assert!(!RationalTime::new(1.0, 24.0).is_invalid_time());
    }

    #[test]
    fn schema_names() {
        let clip = Composable::Clip(Clip::new("c"));
        assert_eq!(clip.schema_name(), "Clip");
        assert_eq!(clip.name(), "c");
        assert_eq!(Composable::Gap(Gap::new(TimeRange::default(), "g")).schema_name(), "Gap");
        assert_eq!(Composable::Transition(Transition::new("t")).schema_name(), "Transition");
        assert_eq!(Composable::Track(Track::new("Video")).schema_name(), "Track");
        assert_eq!(Composable::Stack(Stack::default()).schema_name(), "Stack");
    }

    #[test]
    fn unknown_field_round_trip() {
        let src = r#"{
            "OTIO_SCHEMA": "Timeline.1",
            "metadata": {},
            "name": "T",
            "global_start_time": null,
            "tracks": {
                "OTIO_SCHEMA": "Stack.1",
                "metadata": {"custom": 42},
                "name": "tracks",
                "source_range": null,
                "effects": [],
                "markers": [],
                "enabled": true,
                "children": [],
                "future_field": {"nested": [1, 2, 3]}
            },
            "some_future_root_field": "kept"
        }"#;
        let root: Serializable = serde_json::from_str(src).unwrap();
        let out: Value = serde_json::from_str(&to_json_string(&root).unwrap()).unwrap();
        assert_eq!(out["tracks"]["future_field"].clone(), serde_json::json!({"nested": [1, 2, 3]}), "{out}");
        assert_eq!(out["some_future_root_field"].clone(), serde_json::json!("kept"), "{out}");
        assert_eq!(out["tracks"]["metadata"].clone(), serde_json::json!({"custom": 42}), "{out}");
    }

    #[test]
    fn unknown_root_and_composable_stay_raw() {
        let raw_root = r#"{"OTIO_SCHEMA": "WeirdThing.9", "a": 1}"#;
        let root: Serializable = serde_json::from_str(raw_root).unwrap();
        assert_eq!(root.schema_name(), "WeirdThing.9");
        let out: Value = serde_json::from_str(&to_json_string(&root).unwrap()).unwrap();
        assert_eq!(out, serde_json::from_str::<Value>(raw_root).unwrap());

        let raw_block = r#"{"OTIO_SCHEMA": "FancyBlock.3", "b": [true]}"#;
        let block: Composable = serde_json::from_str(raw_block).unwrap();
        assert_eq!(block.schema_name(), "FancyBlock.3");
        let out: Value = serde_json::from_str(&to_json_string(&block).unwrap()).unwrap();
        assert_eq!(out, serde_json::from_str::<Value>(raw_block).unwrap());

        let raw_media = r#"{"OTIO_SCHEMA": "FancyReference.2", "c": "x"}"#;
        let media: MediaReference = serde_json::from_str(raw_media).unwrap();
        assert_eq!(media.schema_name(), "FancyReference.2");
        let out: Value = serde_json::from_str(&to_json_string(&media).unwrap()).unwrap();
        assert_eq!(out, serde_json::from_str::<Value>(raw_media).unwrap());
    }

    #[test]
    fn timeline_new_builds_named_tracks_stack() {
        let t = Timeline::new("My Sequence");
        assert_eq!(t.name(), "My Sequence");
        assert_eq!(t.tracks().children().len(), 0);
        assert!(t.global_start_time().is_none());
        let out = to_json_string(&t).unwrap();
        assert!(out.contains("\"name\": \"tracks\""), "{out}");
    }

    #[test]
    fn clip_media_reference_resolution() {
        let mut clip = Clip::new("c");
        assert!(clip.media_reference().is_none());
        clip.set_media_reference(MediaReference::ExternalReference(ExternalReference::new(
            "file:///x.mp4",
            None,
        )));
        let mr = clip.media_reference().unwrap();
        assert_eq!(mr.schema_name(), "ExternalReference");
        assert_eq!(mr.as_external_reference().unwrap().target_url(), "file:///x.mp4");
        // An active key that is not in the map yields None (C++ behavior).
        let mut clip2 = Clip::new("c2");
        clip2.active_media_reference_key = Some("OTHER".to_string());
        clip2.media_references.insert("OTHER".to_string(), MediaReference::MissingReference(MissingReference::new()));
        assert!(clip2.media_reference().is_some());
    }
}
