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

//! Facade-family availability, mirroring `src/facade/rust/src/deferred.rs`.
//!
//! Every `oak-cli` subcommand depends on one or more families of the
//! `oakengine_*` C ABI. Those families live in the `oakengine` crate, and
//! some of them are **deferred**: the facade does not wrap them yet, so the
//! subcommands must report a clear "not yet available" error instead of
//! calling into the facade (the calls would not link, and faking behavior
//! would be worse).
//!
//! The entries below are kept field-for-field in sync with the facade's own
//! deferral documentation (`src/facade/rust/src/deferred.rs`). All families
//! this CLI consumes are currently deferred; when a family is wrapped, remove
//! its entry here and the subcommand's call-through (see `src/cmd/`) becomes
//! reachable.

/// One deferred facade family: what it covers, which engine headers define
/// it, and why the facade does not wrap it yet.
pub struct DeferredFamily {
    /// Short family name, as used in messages.
    pub name: &'static str,
    /// Engine headers involved.
    pub headers: &'static str,
    /// Why the family is not wrapped yet (from the facade's deferred.rs).
    pub reason: &'static str,
}

/// `init.h` — engine process initialization/shutdown.
///
/// Not even listed in the facade's scope table yet (`src/facade/rust/README.md`):
/// the facade currently wraps only undo/config/video_params/audio/plugin.
pub const INIT: DeferredFamily = DeferredFamily {
    name: "init",
    headers: "init.h",
    reason: "the facade shell (oakengine_init/shutdown) is not wrapped in oakengine yet (its scope table covers only undo/common/audio/plugin)",
};

/// `project.h` + `footage.h` — the oaknode module family.
///
/// Facade deferred.rs "node": all 30 exports of the oaknode Rust crate are
/// `todo!()` bodies, so the engine project/footage families have no module
/// backing to wrap.
pub const NODE: DeferredFamily = DeferredFamily {
    name: "node (project/footage)",
    headers: "project.h, footage.h",
    reason: "deferred: the oaknode crate is an unimplemented skeleton (every export is a todo!() body), so the project/footage families have no module backing",
};

/// `timeline.h` — sequence/track/clip family.
///
/// Facade deferred.rs "timeline": the oaktimeline crate's exports reference
/// ~80 oaknode C ABI symbols the skeletal oaknode crate does not define, and
/// its test-stubs collide with the real oakundo crate in the facade test
/// link.
pub const TIMELINE: DeferredFamily = DeferredFamily {
    name: "timeline",
    headers: "timeline.h",
    reason: "deferred: test linkage — the oaktimeline crate's exports reference oaknode C ABI symbols the skeletal oaknode crate does not define",
};

/// `renderer.h` — renderer/frame/audio-buffer family.
///
/// Facade deferred.rs "render": no structural blocker; the engine renderer.h
/// family simply was not wrapped in the facade's current pass.
pub const RENDER: DeferredFamily = DeferredFamily {
    name: "render",
    headers: "renderer.h",
    reason: "deferred for session scope: the engine renderer.h family is not wrapped in oakengine yet (no structural blocker)",
};

/// `exporter.h` — export/encode family.
///
/// Facade deferred.rs: exporter is a "genuinely facade-only area" (the
/// liboakengine assembly layer) with no files in the oakengine crate.
pub const EXPORT: DeferredFamily = DeferredFamily {
    name: "exporter",
    headers: "exporter.h",
    reason: "deferred: the exporter family is a facade-only assembly area with no Rust backing (src/facade/rust/src/deferred.rs)",
};

/// Check that every family in `families` is available in the facade.
///
/// Returns `Ok(())` when all are wrapped (none is today); otherwise `Err`
/// carries the composed "not yet available" message naming each deferred
/// family and its reason, for the subcommands to print and exit on.
pub fn require(families: &[&DeferredFamily]) -> Result<(), String> {
    if families.is_empty() {
        return Ok(());
    }
    let mut detail = String::new();
    for f in families {
        detail.push_str(&format!("\n  - {} ({}): {}", f.name, f.headers, f.reason));
    }
    Err(format!(
        "not yet available in the Rust facade (oakengine): these family(ies) are still deferred \
         (see src/facade/rust/src/deferred.rs):{detail}"
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_family_list_is_available() {
        assert!(require(&[]).is_ok());
    }

    #[test]
    fn deferred_family_lists_a_reason() {
        let err = require(&[&INIT]).unwrap_err();
        assert!(err.contains("not yet available"));
        assert!(err.contains("init"));
        assert!(err.contains("oakengine"));
    }

    #[test]
    fn all_cli_families_are_currently_deferred() {
        // Keeps this file honest: if any family the CLI depends on flips to
        // available, the subcommand ports in src/cmd/ become reachable and
        // the tests asserting "not yet available" must be revisited.
        let all: [&[&DeferredFamily]; 5] = [
            &[&INIT],
            &[&NODE],
            &[&TIMELINE],
            &[&RENDER],
            &[&EXPORT],
        ];
        for families in all {
            assert!(require(families).is_err());
        }
    }
}
