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

//! Tile distort effect (C++
//! `src/node/src/distort/tile/tiledistortnode.{h,cpp}`,
//! `olive::TileDistortNode`).

use crate::factory::NodeMeta;
use crate::node::{Category, Gizmo, NodeBehavior, NodeCore};

/// Texture input id (C++ `k_texture_input`). Type: texture; flags:
/// not-keyframable; this is the node's effect input.
pub const TEXTURE_INPUT: &str = "tex_in";

/// Scale input id (C++ `k_scale_input`). Type: float; default `0.5`;
/// properties: `min = 0`, `view = percentage`.
pub const SCALE_INPUT: &str = "scale_in";

/// Position input id (C++ `k_position_input`). Type: vec2; default
/// `(0, 0)`.
pub const POSITION_INPUT: &str = "position_in";

/// Anchor combo input id (C++ `k_anchor_input`). Type: combo; default
/// `4` (C++ `k_middle_center`); combo strings: "Top-Left",
/// "Top-Center", "Top-Right", "Middle-Left", "Middle-Center",
/// "Middle-Right", "Bottom-Left", "Bottom-Center", "Bottom-Right".
pub const ANCHOR_INPUT: &str = "anchor_in";

/// Horizontal mirror input id (C++ `k_mirror_x_input`). Type: bool;
/// default `false`.
pub const MIRROR_X_INPUT: &str = "mirrorx_in";

/// Vertical mirror input id (C++ `k_mirror_y_input`). Type: bool;
/// default `false`.
pub const MIRROR_Y_INPUT: &str = "mirrory_in";

/// Anchor point for tiling (C++ private enum `Anchor`); values match
/// the `anchor_in` combo indices and the shader's `anchor_in` defines.
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum Anchor {
	/// Top-left corner.
	TopLeft = 0,
	/// Top edge center.
	TopCenter = 1,
	/// Top-right corner.
	TopRight = 2,
	/// Left edge center.
	MiddleLeft = 3,
	/// Image center.
	MiddleCenter = 4,
	/// Right edge center.
	MiddleRight = 5,
	/// Bottom-left corner.
	BottomLeft = 6,
	/// Bottom edge center.
	BottomCenter = 7,
	/// Bottom-right corner.
	BottomRight = 8,
}

/// Tile distort node. Repeats the image infinitely in both directions,
/// optionally mirroring alternate tiles.
pub struct TileDistortNode {
	/// Position drag handle (C++ `PointGizmo *gizmo_`; anchor point
	/// shape, drags both tracks of `position_in`).
	gizmo: Gizmo,
}

/// Fragment shader (C++ loads the `:/shaders/tile.frag` resource in
/// `get_shader_code`). Text copied verbatim from
/// `engine/shaders/tile.frag`.
const SHADER_FRAG: &str = r#"uniform float scale_in;
uniform vec2 position_in;
uniform vec2 resolution_in;
uniform bool mirrorx_in;
uniform bool mirrory_in;
uniform int anchor_in;

uniform sampler2D tex_in;

in vec2 ove_texcoord;
out vec4 frag_color;

#define TOP_LEFT        0
#define TOP_CENTER      1
#define TOP_RIGHT       2
#define MIDDLE_LEFT     3
#define MIDDLE_CENTER   4
#define MIDDLE_RIGHT    5
#define BOTTOM_LEFT     6
#define BOTTOM_CENTER   7
#define BOTTOM_RIGHT    8

void main(void) {
  vec2 coord = ove_texcoord;

  vec2 offset;

  if (anchor_in == TOP_LEFT || anchor_in == TOP_CENTER || anchor_in == TOP_RIGHT) {
    offset.y = 0.0;
  } else if (anchor_in == MIDDLE_LEFT || anchor_in == MIDDLE_CENTER || anchor_in == MIDDLE_RIGHT) {
    offset.y = 0.5;
  } else if (anchor_in == BOTTOM_LEFT || anchor_in == BOTTOM_CENTER || anchor_in == BOTTOM_RIGHT) {
    offset.y = 1.0;
  }

  if (anchor_in == TOP_LEFT || anchor_in == MIDDLE_LEFT || anchor_in == BOTTOM_LEFT) {
    offset.x = 0.0;
  } else if (anchor_in == TOP_CENTER || anchor_in == MIDDLE_CENTER || anchor_in == BOTTOM_CENTER) {
    offset.x = 0.5;
  } else if (anchor_in == TOP_RIGHT || anchor_in == MIDDLE_RIGHT || anchor_in == BOTTOM_RIGHT) {
    offset.x = 1.0;
  }

  coord -= position_in/resolution_in;

  coord -= offset;
  coord /= scale_in;
  coord += offset;

  vec2 modcoord = mod(coord, 1.0);

  if (mirrorx_in && mod(coord.x, 2.0) > 1.0) {
    modcoord.x = 1.0 - modcoord.x;
  }

  if (mirrory_in && mod(coord.y, 2.0) > 1.0) {
    modcoord.y = 1.0 - modcoord.y;
  }

  frag_color = vec4(texture(tex_in, modcoord));
}
"#;

impl TileDistortNode {
	/// Fragment shader (C++ `get_shader_code()`; the request id is
	/// ignored, this is the only shader).
	fn shader_frag() -> &'static str {
		SHADER_FRAG
	}
}

impl NodeBehavior for TileDistortNode {
	/// Human-readable name (C++ `name()`).
	fn name(&self) -> &str {
		"Tile"
	}

	/// Stable type id (C++ `id()`).
	fn type_id(&self) -> &str {
		"org.olivevideoeditor.Olive.tile"
	}

	/// Categories (C++ `category()`).
	fn categories(&self) -> &[Category] {
		&[Category::Distort]
	}

	/// Description (C++ `description()`).
	fn description(&self) -> &str {
		"Infinitely tile an image horizontally and vertically."
	}

	/// Localized input names (C++ `retranslate()`): `tex_in` ->
	/// "Input", `scale_in` -> "Scale", `position_in` -> "Position",
	/// `mirrorx_in` -> "Mirror Horizontally", `mirrory_in` -> "Mirror
	/// Vertically", `anchor_in` -> "Anchor" (with the nine combo strings
	/// documented on [`ANCHOR_INPUT`]).
	fn input_name<'a>(&self, id: &'a str) -> &'a str {
		todo!()
	}

	/// Evaluate outputs (C++ `value()`): no texture -> push nothing;
	/// scale differs from 1.0 (an approximate-equality epsilon test:
	/// `abs(scale-1)*1e12 > min(abs(scale), 1)`) -> shader job over the
	/// whole value row with `resolution_in` inserted from the texture's
	/// virtual resolution; scale ~== 1.0 -> pass-through push of the
	/// input texture unchanged.
	fn value(
		&self,
		core: &NodeCore,
		inputs: &crate::value::NodeValueRow,
		time: oakcore_rs::Rational,
		table: &mut crate::value::NodeValueTable,
	) {
		todo!()
	}

	/// Shader code request (C++ `get_shader_code()`): ignores the
	/// request id and always returns the tile fragment shader.
	fn shader_code(&self, request: &str) -> Option<String> {
		todo!()
	}

	/// Gizmo positions (C++ `update_gizmo_positions()`): with a texture,
	/// places the position gizmo at `position_in` offset from the anchor
	/// point selected by `anchor_in` (rows add 0 / half / full height,
	/// columns add 0 / half / full width of the texture resolution).
	fn gizmo_update(&self, core: &NodeCore, row: &crate::value::NodeValueRow) {
		todo!()
	}

	/// Gizmo drag (C++ `gizmo_drag_move()`): drags the position input's
	/// X and Y track draggers by the mouse delta added to their
	/// drag-start values.
	fn gizmo_drag(&mut self, core: &mut NodeCore, start: bool, x: f64, y: f64, modifiers: u32) {
		todo!()
	}

	/// Deep copy (C++ `copy()`).
	fn duplicate(&self, core: &NodeCore) -> Option<Box<dyn NodeBehavior>> {
		todo!()
	}
}

/// Constructor (C++ `TileDistortNode::TileDistortNode()`): adds
/// `tex_in`, `scale_in`, `position_in`, `anchor_in`, `mirrorx_in` and
/// `mirrory_in` with the defaults and properties documented on the
/// constants; creates the anchor-shaped position point gizmo bound to
/// both tracks of `position_in`; sets the video-effect flag and the
/// effect input.
pub fn create() -> (NodeCore, Box<dyn NodeBehavior>) {
	todo!()
}

/// Register this node type (C++ factory entry for
/// `org.olivevideoeditor.Olive.tile`).
pub fn register(meta: &mut Vec<NodeMeta>) {
	meta.push(NodeMeta {
		type_id: "org.olivevideoeditor.Olive.tile",
		name: "Tile",
		categories: &[Category::Distort],
		create,
	});
}
