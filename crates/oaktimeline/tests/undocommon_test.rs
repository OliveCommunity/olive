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

//! Coverage for the shared node-removal helpers and command wrappers
//! (`src/undocommon.rs`): removal capability checks, remove-command
//! factories, `CHandleCommandWrapper`, and `MultiUndoCommand`.
#![cfg(feature = "test-stubs")]

use oakcore_rs::{Rational, TimeRange};

use oaktimeline::bridge::node::{oaknode_block_clip_create, oaknode_block_gap_create};
use oaktimeline::bridge::teststubs::MockNode;
use oaktimeline::handle::{CHandle, get, make_owned};
use oaktimeline::marker::{MarkerAddCommand, TimelineMarkerList};
use oaktimeline::undocommon::{
	CHandleCommandWrapper, Command, MultiUndoCommand, block_can_be_removed,
	create_and_run_block_remove_command, create_and_run_remove_command,
	create_block_remove_command, create_remove_command, free_command_handle, node_can_be_removed,
};
use oaktimeline::workarea::{TimelineWorkArea, WorkareaSetEnabledCommand};

/// `node_can_be_removed` is true for a node with no output connections and
/// false once it has any.
#[test]
fn node_can_be_removed_toggles_on_connections() {
	let free = make_owned(MockNode {
		output_conns: 0,
		..Default::default()
	});
	assert!(node_can_be_removed(free));

	let busy = make_owned(MockNode {
		output_conns: 1,
		..Default::default()
	});
	assert!(!node_can_be_removed(busy));
}

/// `block_can_be_removed` delegates through the block's generic-node view.
#[test]
fn block_can_be_removed_by_connections() {
	let clip = make_owned(MockNode {
		output_conns: 0,
		..Default::default()
	});
	assert!(block_can_be_removed(clip));

	let gap = make_owned(MockNode {
		output_conns: 2,
		..Default::default()
	});
	assert!(!block_can_be_removed(gap));
}

/// Remove-command factories box a non-null command handle.
#[test]
fn create_remove_commands_box_non_null() {
	let node = make_owned(MockNode::default());
	assert!(!create_remove_command(node.clone()).is_null());

	let block = unsafe { oaknode_block_clip_create() };
	assert!(!create_block_remove_command(block).is_null());

	assert!(!create_and_run_remove_command(node).is_null());

	let gap = unsafe { oaknode_block_gap_create() };
	assert!(!create_and_run_block_remove_command(gap).is_null());
}

/// `free_command_handle` is a no-op on a null pointer and clears a live
/// command handle.
#[test]
fn free_command_handle_null_and_live() {
	free_command_handle(std::ptr::null_mut());

	let node = make_owned(MockNode::default());
	let mut cmd = create_remove_command(node);
	assert!(!cmd.is_null());
	free_command_handle(&mut cmd);
	assert!(cmd.ctx.is_null());
}

/// `CHandleCommandWrapper` forwards redo/undo to the wrapped command and
/// reports validity; an empty wrapper is inert.
#[test]
fn command_wrapper_forwards_redo_undo() {
	let wa_h = make_owned(TimelineWorkArea::new());
	let cmd = WorkareaSetEnabledCommand::new(wa_h.clone(), true).to_command();

	let mut w = CHandleCommandWrapper::new(cmd);
	assert!(w.is_valid());
	w.redo();
	assert!(unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());
	w.undo();
	assert!(!unsafe { get::<TimelineWorkArea>(&wa_h) }.unwrap().enabled());

	// An empty wrapper is invalid and its methods are no-ops.
	let mut empty = CHandleCommandWrapper::new(CHandle::null());
	assert!(!empty.is_valid());
	empty.redo();
	empty.undo();
	// Dropping `empty` and `w` frees the handles without panicking.
}

/// `MultiUndoCommand` runs children in order on redo and reverse on undo.
#[test]
fn multi_undo_command_runs_children() {
	let list_h = make_owned(TimelineMarkerList::new());
	let mut multi = MultiUndoCommand::new();
	assert!(multi.empty());

	multi.add_child(Box::new(MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"m",
		0,
	)) as Box<dyn Command>);
	multi.add_child(Box::new(MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(5, 1), Rational::new(6, 1)),
		"n",
		0,
	)) as Box<dyn Command>);
	assert!(!multi.empty());

	multi.redo();
	let l = unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap();
	assert_eq!(l.size(), 2);

	multi.undo();
	assert_eq!(unsafe { get::<TimelineMarkerList>(&list_h) }.unwrap().size(), 0);
}

/// `MultiUndoCommand::to_command` boxes the group into a command handle.
#[test]
fn multi_undo_command_boxes_to_chandle() {
	let list_h = make_owned(TimelineMarkerList::new());
	let mut multi = MultiUndoCommand::new();
	multi.add_child(Box::new(MarkerAddCommand::new(
		list_h.clone(),
		TimeRange::new(Rational::new(1, 1), Rational::new(2, 1)),
		"m",
		0,
	)) as Box<dyn Command>);

	let h = multi.to_command();
	assert!(!h.is_null());
	assert_eq!(h.abi_version, 1);
}
