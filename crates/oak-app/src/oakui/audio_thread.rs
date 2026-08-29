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

//! Dedicated audio-render thread (M16 S3): audio chunk renders never run
//! on the UI tick.
//!
//! Playback prefetch submits one audio chunk per frame tick. Those chunks
//! used to be rendered synchronously by the ticket arena's inline audio
//! dispatcher — i.e. inside the UI tick itself: a slow decode (opening a
//! file, hardware probe) froze the UI and delayed the next chunk, starving
//! the audio device (underruns → pops/clicks). This module owns a single
//! background thread that runs the same [`oak_render::eval::render_audio_samples`]
//! path the ticket arena uses; the UI tick only queues the job and returns
//! immediately.
//!
//! The thread starts lazily on the first submit and lives for the process.
//! A single thread is deliberate: the montage decoders are not safe to run
//! concurrently, and one chunk per frame tick stays far below real-time
//! decode throughput. Render errors degrade to silence of the exact length
//! the success path would produce, so the playback buffer never desyncs.

use std::sync::{mpsc, OnceLock};

use oak_render::ticket::{AudioTicketParams, TicketPayload};

use super::renderops::RenderedAudio;

/// One queued audio chunk: render `params`, then send `(start_ts, data)`
/// on `done`.
struct AudioJob {
	params: AudioTicketParams,
	start_ts: i64,
	done: mpsc::Sender<(i64, RenderedAudio)>,
	/// When the job was queued (diagnostics: submit→render latency).
	enqueued: std::time::Instant,
}

/// The audio-render thread's job queue (set on first use; the sender is
/// kept alive by this `OnceLock` for the whole process).
static TX: OnceLock<mpsc::Sender<AudioJob>> = OnceLock::new();

/// Queue `params` for rendering on the audio thread; the rendered samples
/// (or aligned silence on failure) are sent on `done` as
/// `(start_ts, data)`. Never blocks — a UI tick must not wait on a worker.
pub fn submit(
	params: AudioTicketParams,
	start_ts: i64,
	done: mpsc::Sender<(i64, RenderedAudio)>,
) -> Result<(), String> {
	let tx = ensure_thread();
	tx.send(AudioJob {
		params,
		start_ts,
		done,
		enqueued: std::time::Instant::now(),
	})
	.map_err(|_| "the audio render thread has exited".to_string())
}

/// The queue sender, starting the render thread on first use.
fn ensure_thread() -> mpsc::Sender<AudioJob> {
	if let Some(tx) = TX.get() {
		return tx.clone();
	}
	let (tx, rx) = mpsc::channel::<AudioJob>();
	if TX.set(tx.clone()).is_ok() {
		// We won the race and own the receiver; start the thread. The
		// loser's `rx` is dropped right here.
		std::thread::Builder::new()
			.name("oak-audio-render".to_string())
			.spawn(move || render_loop(rx))
			.expect("spawn the audio render thread");
	}
	TX.get().expect("audio thread sender was set above").clone()
}

/// The render loop: one job at a time; failures degrade to silence.
fn render_loop(rx: mpsc::Receiver<AudioJob>) {
	while let Ok(job) = rx.recv() {
		let wait_ms = job.enqueued.elapsed().as_millis();
		let t0 = std::time::Instant::now();
		let data = match oak_render::eval::render_audio_samples(&job.params) {
			Ok(TicketPayload::Audio(samples)) => RenderedAudio {
				data: samples.samples,
				sample_rate: samples.sample_rate,
				channel_count: samples.channel_count,
			},
			_ => silence_for(&job.params),
		};
		let ms = t0.elapsed().as_millis();
		super::real::audio_dbg(&format!(
			"chunk {} render: waited {wait_ms}ms, rendered in {ms}ms",
			job.start_ts
		));
		let _ = job.done.send((job.start_ts, data));
	}
}

/// Aligned silence for `params`: the same anchored-grid length
/// (`round(out·rate) − round(in·rate)`) the successful path produces, so
/// a failed chunk keeps the playback buffer aligned.
fn silence_for(params: &AudioTicketParams) -> RenderedAudio {
	let rate = params.sample_rate.max(1) as f64;
	let channels = params.channel_layout.count_ones().max(1) as i32;
	let frames = ((params.range.out().to_f64() * rate).round()
		- (params.range.in_().to_f64() * rate).round())
		.max(0.0) as usize;
	RenderedAudio {
		data: vec![0.0; frames * channels as usize],
		sample_rate: params.sample_rate.max(1),
		channel_count: channels,
	}
}

#[cfg(test)]
mod tests {
	use super::*;
	use oak_core::{Rational, TimeRange};
	use oak_render::ticket::MontageClip;

	/// Stereo 48 kHz empty-montage params over `[in_sec, out_sec)`.
	fn params(in_sec: i64, out_sec: i64) -> AudioTicketParams {
		AudioTicketParams {
			viewer: 7,
			range: TimeRange::new(Rational::new(in_sec, 1), Rational::new(out_sec, 1)),
			sample_rate: 48000,
			channel_layout: 0x3,
			montage: Vec::new(),
		}
	}

	/// Submitting returns immediately (no blocking render on the caller)
	/// and the result arrives asynchronously on the completion channel.
	#[test]
	fn submit_returns_immediately_and_delivers_async() {
		let (done_tx, done_rx) = mpsc::channel();
		let start = std::time::Instant::now();
		submit(params(0, 1), 12, done_tx).expect("submit");
		assert!(
			start.elapsed() < std::time::Duration::from_millis(50),
			"submit must not block"
		);

		let (start_ts, audio) = done_rx
			.recv_timeout(std::time::Duration::from_secs(10))
			.expect("rendered audio delivered");
		assert_eq!(start_ts, 12);
		assert_eq!(audio.sample_rate, 48000);
		assert_eq!(audio.channel_count, 2);
		// Empty montage → pure silence, anchored-grid length (1 s).
		assert_eq!(audio.data.len(), 48000 * 2);
		assert!(audio.data.iter().all(|&s| s == 0.0));
	}

	/// A clip that cannot be opened (missing file) degrades to silence of
	/// the expected anchored-grid length — the playback buffer stays
	/// aligned.
	#[test]
	fn error_produces_silence_of_expected_length() {
		let (done_tx, done_rx) = mpsc::channel();
		let mut p = params(0, 1);
		p.montage.push(MontageClip {
			filename: "/nonexistent/oak-audio-test.wav".to_string(),
			stream_index: 0,
			in_time: Rational::new(0, 1),
			out_time: Rational::new(1, 1),
			media_in: Rational::new(0, 1),
			gain: 1.0,
			effects: Vec::new(),
		});
		submit(p, 0, done_tx).expect("submit");

		let (_, audio) = done_rx
			.recv_timeout(std::time::Duration::from_secs(10))
			.expect("silence delivered");
		assert_eq!(audio.data.len(), 48000 * 2);
		assert!(audio.data.iter().all(|&s| s == 0.0));
	}
}
