# Oak Video Editor

[![CI](https://github.com/OakVideoEditorCommunity/oak/actions/workflows/ci.yml/badge.svg)](https://github.com/OakVideoEditorCommunity/oak/actions/workflows/ci.yml)
[中文](docs/zh/README.new.md)

Oak Video Editor is a free, open-source **non-linear video editor** for Windows, macOS, and Linux.

This project is a community-maintained fork of Olive Video Editor.

> **NOTE: Oak Video Editor is alpha software and is considered highly unstable. We appreciate users testing it and sharing feedback, but please use it at your own risk.**

<!-- SCREENSHOT: main editing window (timeline + viewer) -->
![Screenshot – main window](docs/images/screenshot-main.png)

## Features

- Responsive timeline editing with smart disk/playback caching
- Node-based compositing and effects, including an OpenFX (OFX) plugin host
- Full color management (OpenColorIO): `.cube`/`.3dl` LUTs, configurable display/view/look transforms
- Scopes: waveform, vectorscope, histogram, and audio meters (LUFS/VU)
- Bézier keyframe animation with a curve editor
- Multicam editing and waveform-based audio sync
- Proxy media workflow for smooth 4K/8K editing
- Hardware-accelerated and batch export (H.264/H.265, image sequences, audio)
- Project crash recovery and autosave

<!-- SCREENSHOT: node editor -->
![Screenshot – node editor](docs/images/screenshot-node.png)

## Download

Pre-built binaries for Windows, macOS, and Linux are on the [Releases](https://github.com/OakVideoEditorCommunity/oak/releases) page.

Latest: [v0.4.2-alpha](https://github.com/OakVideoEditorCommunity/oak/releases/tag/v0.4.2-alpha)

## Architecture

Oak is split into small, independently testable components with a pure C ABI at the boundary:

| Component | Kind | Purpose |
|---|---|---|
| `liboakcore` | shared library | Qt-free core types (rational, timecode, bezier, sample buffer, audio/video params) with a pure C ABI |
| `liboakengine` | shared library | the editing engine (node graph, timeline, render, codec, tasks), exposed only through the `oakengine_*` C ABI facade |
| `oak-editor` | application | the Qt GUI; talks to the engine **only** through the C ABI |
| `oak-render-worker` | process | headless render process that executes frames off the GUI thread (NDJSON IPC) |
| `oak-cli` | tool | command-line frontend for the engine: media info, probing, rendering, and transcoding without the GUI |

The C ABI boundary is what makes the engine embeddable and is the foundation for a planned module-by-module rewrite of the engine in Rust (see [`docs/zh/plans/completed/riir.md`](docs/zh/plans/completed/riir.md)).

<!-- DIAGRAM: component / ABI layout -->
![Architecture diagram](docs/images/architecture.png)

## Command-Line Tools

`oak-cli` is a standalone, pure-C-ABI consumer of the engine:

```bash
oak-cli info <file>                    # media information
oak-cli probe <file>                   # stream/decoder probe
oak-cli render <project.ove> <out>     # render a project range
oak-cli transcode <in> <out>           # transcode media
```

## Building from Source

See [`docs/build.md`](docs/build.md) for full instructions (Windows/MSYS2, Linux Debian/Ubuntu/Fedora/Arch, and [`docs/build_macos.md`](docs/build_macos.md) for macOS). In short:

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Roadmap

| Version | Theme | Core Deliverables |
|:--|:--|:--|
| **0.3** | **Plugin Architecture** | Production-ready OpenFX host support — "any OFX plugin loads without crashing" |
| **0.4** | **Color, Audio & Performance** | `.cube`/`.3dl`, scopes, three-way color wheels, waveform auto-sync, BWF timecode sync, audio meters, proxy media, hardware-accelerated export, batch render queue |
| **0.5** | **Animation, Tracking & Collaboration** | Bézier keyframe curve editor, point tracking, image stabilizer, full multicam, OpenTimelineIO, EDL/XML interchange |
| **0.6** | **Stability** | Project file format freeze (backward compatibility), crash recovery, autosave, memory optimization |
| **1.0** | **Production Ready** | Complete documentation, installers, known-issues list, community support |

## Contributing

Contributions are welcome. Please read [`CONTRIBUTING.md`](CONTRIBUTING.md) first — it covers:

- the code style (naming rules, including `PascalCase` struct typedefs),
- the **Google Test** requirement for all tests,
- the C ABI boundary contract for engine-facing code.

Useful project docs: [`docs/zh/`](docs/zh/) (中文文档), [`docs/zh/facade-migration-roadmap.md`](docs/zh/facade-migration-roadmap.md), [`docs/zh/plans/completed/riir.md`](docs/zh/plans/completed/riir.md).

## License

Oak Video Editor is free software licensed under the [GNU General Public License v3](LICENSE).
