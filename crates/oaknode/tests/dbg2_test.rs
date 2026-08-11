#[test]
fn dbg_dlsym() {
	let p = oaknode::bridge::dlsym::resolve("oaknode_xml_writer_init");
	eprintln!("writer init symbol: {:?}", p);
	let p2 = oaknode::bridge::dlsym::resolve("oakundo_command_init_multi");
	eprintln!("undo multi symbol: {:?}", p2);
}
