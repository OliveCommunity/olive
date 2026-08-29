// Temporary diagnostic: print the hardware decoder a video stream opens
// with (None = software fallback).
// Usage: hwcheck <mediafile> [stream_index]

use oak_codec::decoder::{CodecStream, Decoder};
use oak_codec::ffmpeg::FFmpegDecoder;

fn probe(device_type: ffmpeg_next::ffi::AVHWDeviceType, name: &str) {
	let mut dev: *mut ffmpeg_next::ffi::AVBufferRef = std::ptr::null_mut();
	let rc = unsafe {
		ffmpeg_next::ffi::av_hwdevice_ctx_create(
			&mut dev,
			device_type,
			std::ptr::null(),
			std::ptr::null_mut(),
			0,
		)
	};
	eprintln!("{name}: av_hwdevice_ctx_create rc = {rc}");
	if rc >= 0 && !dev.is_null() {
		unsafe { ffmpeg_next::ffi::av_buffer_unref(&mut dev) };
	}
}

fn main() {
	ffmpeg_next::log::set_level(ffmpeg_next::log::Level::Debug);
	probe(
		ffmpeg_next::ffi::AVHWDeviceType::AV_HWDEVICE_TYPE_CUDA,
		"CUDA",
	);
	probe(
		ffmpeg_next::ffi::AVHWDeviceType::AV_HWDEVICE_TYPE_VAAPI,
		"VAAPI",
	);
	let args: Vec<String> = std::env::args().collect();
	if args.len() < 2 {
		return;
	}
	let stream: i32 = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(0);
	let d = FFmpegDecoder::new();
	let s = CodecStream::with_block(args[1].clone(), stream, None);
	d.open(&s).expect("open video stream");
	println!("hw_decoder_name = {:?}", d.hw_decoder_name());
}
