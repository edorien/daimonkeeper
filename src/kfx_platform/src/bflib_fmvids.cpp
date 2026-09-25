#include "pre_inc.h"
#include "bflib_fmvids.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"
#include "bflib_inputctrl.h"
#include "bflib_keybrd.h"
#include "bflib_vidsurface.h"

// See: https://trac.ffmpeg.org/ticket/3626
extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/imgutils.h>
	#include <libswresample/swresample.h>
    #pragma GCC diagnostic warning "-Wdeprecated-declarations"
}

#include <cstdio>
#include <string>
#include <memory>
#include <stdexcept>
#include <chrono>
#include "thread.hpp"
#include <vector>
#include <SDL3/SDL.h>
#include "post_inc.h"

namespace {

/* This used to read four 8-bit palette indices at a time as one uint32_t and
 * write pixel-doubled output by byte-shuffling within a 32-bit word -- an
 * aliasing trick that only worked while a pixel was exactly one byte. With
 * TbPixel four bytes wide the packing is meaningless, so this is a
 * straightforward per-pixel loop that expands each source index through the
 * movie's own decoded palette. Same output geometry as before; the width no
 * longer has to be a multiple of 4 for the loop to terminate correctly. */

/* The movie's per-frame palette (built in output_video_frame() from the
 * AVFrame's own PAL8 data) is already 8-bit-per-channel -- unlike the
 * game's own palette, which is 6-bit VGA scale and goes through
 * chan6_to_8() inside expand_indexed_pixel()/resolve_indexed_pixel(). Also,
 * a video frame has no transparency concept, so unlike
 * expand_indexed_pixel(), index 0 isn't special-cased here -- every byte,
 * including 0, is just an opaque colour to look up. */
inline TbPixel expand_pal8_pixel(uint8_t index, const unsigned char *pal8)
{
	return TbPixel_RGB(pal8[3 * index + 0], pal8[3 * index + 1], pal8[3 * index + 2]);
}

/** Copies one source row into dstbuf, optionally doubling each pixel
 * horizontally (double_w) and/or duplicating the whole row dst_shift pixels
 * further down (double_h) -- the four SMK_PixelDoubleWidth / SMK_PixelDoubleLine
 * combinations copy_to_screen() dispatches on all reduce to this one loop.
 * dst_shift is unused when double_h is false. */
void copy_to_screen_row_ex(unsigned char *srcbuf, TbPixel *dstbuf, int64_t width, int64_t dst_shift,
                            const unsigned char *palette, bool double_w, bool double_h)
{
	for (int64_t i = 0; i < width; i++) {
		const TbPixel px = expand_pal8_pixel(srcbuf[i], palette);
		if (double_w) {
			dstbuf[2*i]     = px;
			dstbuf[2*i + 1] = px;
			if (double_h) {
				dstbuf[dst_shift + 2*i]     = px;
				dstbuf[dst_shift + 2*i + 1] = px;
			}
		} else {
			dstbuf[i] = px;
			if (double_h) {
				dstbuf[dst_shift + i] = px;
			}
		}
	}
}

void copy_to_screen(const AVFrame & frame, const int64_t flags, const unsigned char *palette)
{
	const auto src_pitch = frame.linesize[0];
	auto srcbuf = frame.data[0];
	int64_t screen_buffer_center_offset;
	if (flags & (SMK_PixelDoubleLine | SMK_InterlaceLine)) {
		screen_buffer_center_offset = lbDisplay.GraphicsScreenWidth * ((LbScreenHeight() - 2 * frame.height) >> 1);
	} else {
		screen_buffer_center_offset = lbDisplay.GraphicsScreenWidth * ((LbScreenHeight() - frame.height) >> 1);
	}
	auto w = frame.width;
	if (flags & SMK_PixelDoubleWidth) {
		w = 2 * frame.width;
	}
	auto dstbuf = &RendererGetFramebuffer()[screen_buffer_center_offset + ((LbScreenWidth() - w) >> 1)];
	if (flags & SMK_PixelDoubleLine) {
		const bool double_w = (flags & SMK_PixelDoubleWidth) != 0;
		for (int64_t h = frame.height; h > 0; h--) {
			copy_to_screen_row_ex(srcbuf, dstbuf, frame.width, lbDisplay.GraphicsScreenWidth, palette, double_w, true);
			dstbuf += 2 * lbDisplay.GraphicsScreenWidth;
			srcbuf += src_pitch;
		}
	} else {
		const bool double_w = (flags & SMK_PixelDoubleWidth) != 0;
		const int64_t dstbuf_step = (flags & SMK_InterlaceLine) ? 2 * lbDisplay.GraphicsScreenWidth : lbDisplay.GraphicsScreenWidth;
		for (int64_t h = frame.height; h > 0; h--) {
			copy_to_screen_row_ex(srcbuf, dstbuf, frame.width, lbDisplay.GraphicsScreenWidth, palette, double_w, false);
			dstbuf += dstbuf_step;
			srcbuf += src_pitch;
		}
	}
}

void copy_to_screen_scaled(const AVFrame & frame, const int64_t flags, const unsigned char *palette)
{
	const auto src_pitch = frame.linesize[0];
	const auto src_buf = frame.data[0];
	const auto dst_buf = &RendererGetFramebuffer()[0];
	// Compute scaling ratio -> Output co-ordinates and output size
	const int64_t scanline = lbDisplay.GraphicsScreenWidth;
	const int64_t nlines = lbDisplay.GraphicsScreenHeight;
	int64_t spw = 0;
	int64_t sph = 0;
	int64_t dst_width = 0;
	int64_t dst_height = 0;

	if ((flags & SMK_FullscreenStretch) && !(flags & SMK_FullscreenFit)) {
		// Use full screen resolution and fill the whole canvas by "stretching"
		dst_width = scanline;
		dst_height = nlines;
	} else {
		// Calculate the correct output size
		int64_t in_width = frame.width;
		int64_t in_height = frame.height;
		double units_per_px = 0;
		// relative aspect ratio difference between the source frame and destination frame
		const double relative_ar_difference = (in_width * 1.0 / in_height * 1.0) / (scanline * 1.0 / nlines * 1.0);
		// when keeping aspect ratio, instead of stretching, this is inverted depending on if we want to crop or fit
		double comparison_ratio = 1;
		if ((flags & SMK_FullscreenStretch) && (flags & SMK_FullscreenFit)) {
			// stretch source from 320x200(16:10) to 320x240 (4:3) (i.e. vertical x 1.2) - "preserve *original* aspect ratio mode"
			if (frame.width == 320 && frame.height == 200) {
				in_height = (int64_t)(in_height * 1.2);
			}
		}
		if ((flags & SMK_FullscreenCrop) && !(flags & SMK_FullscreenFit)) {
			// fill screen (will crop)
			comparison_ratio = relative_ar_difference;
		} else {
			// fit to full screen, preserve aspect ratio (pillar/letter boxed)
			comparison_ratio = 1.0 / relative_ar_difference;
		}
		// take either the destination width or height, depending on whether
		// the destination is wider or narrower than the source
		// (same aspect ratio is treated the same as wider),
		// and also if we want to crop or fit
		if (comparison_ratio <= 1.0) {
			units_per_px = (scanline>nlines?scanline:nlines)/((in_width>in_height?in_width:in_height)/16.0);
		} else {
			units_per_px = (scanline>nlines?nlines:scanline)/((in_width>in_height?in_height:in_width)/16.0);
		}
		if ((flags & SMK_FullscreenCrop) && (flags & SMK_FullscreenFit)) {
			// Find the highest integer scale possible
			if (flags & SMK_FullscreenStretch) {
				//4:3 stretch mode (crop off to the nearest 5x/6x scale
				if (frame.width == 320 && frame.height == 200) {
					// make sure the multiple is integer divisible by 5. Use 5x as a minimum,
					// otherwise there will be no video (resolutions smaller than 1600x1200
					// will have a cropped image from a buffer of that size).
					units_per_px = (max(5, (int64_t)(units_per_px / 16.0 / 5.0) * 5) * 16);
				}
			}
			// scale to the nearest integer multiple of the source resolution.
			units_per_px = ((int64_t)(units_per_px / 16.0) * 16);
		}
		// Starting point coords and width for the destination buffer (based on desired aspect ratio)
		spw = (int64_t)((scanline - in_width * units_per_px / 16.0) / 2.0);
		sph = (int64_t)((nlines - in_height * units_per_px / 16.0) / 2.0);
		dst_width = (int64_t)(in_width * units_per_px / 16.0);
		dst_height = (int64_t)(in_height * units_per_px / 16.0);
	}

	/* Letterbox bars. Was memset(...,0,...) writing palette index 0 to a
	 * one-byte-per-pixel framebuffer, which presented as index 0's colour
	 * (black by convention here) -- NOT as sprite-style transparency, so
	 * this uses an opaque black rather than expand_indexed_pixel(0, ...). */
	const TbPixel clear_px = TbPixel_RGB(0, 0, 0);
	// Clearing top of the canvas
	for (int64_t sh = 0; sh < sph; sh++) {
		for (int64_t i = 0; i < scanline; i++) dst_buf[sh * scanline + i] = clear_px;
	}
	// Clearing bottom of the canvas
	// (Note: it must be done before drawing, to make sure we won't overwrite last line)
	for (int64_t sh = sph + dst_height; sh < nlines; sh++) {
		for (int64_t i = 0; i < scanline; i++) dst_buf[sh * scanline + i] = clear_px;
	}
	// Now drawing
	auto dhstart = sph;
	for (int64_t sh = 0; sh < frame.height; sh++) {
		const auto dhend = sph + (dst_height * (sh + 1) / frame.height);
		const auto src = &src_buf[sh * src_pitch];
		// make for(k=0;k<dhend-dhstart;k++) but restrict k to draw area
		const auto mhmin = max(0, -dhstart);
		const auto mhmax = min(dhend - dhstart, nlines - dhstart);
		for (int64_t k = mhmin; k < mhmax; k++) {
			const auto dst = &dst_buf[(dhstart + k) * scanline];
			int64_t dwstart = spw;
			if (dwstart > 0) {
				for (int64_t i = 0; i < dwstart; i++) dst[i] = clear_px;
			}
			for (int64_t sw = 0; sw < frame.width; sw++) {
				const auto dwend = spw + (dst_width * (sw + 1) / frame.width);
				// make for(i=0;i<dwend-dwstart;i++) but restrict i to draw area
				const auto mwmin = max(0, -dwstart);
				const auto mwmax = min(dwend - dwstart, scanline - dwstart);
				const TbPixel src_px = expand_pal8_pixel(src[sw], palette);
				for (int64_t i = mwmin; i < mwmax; i++) {
					dst[dwstart+i] = src_px;
				}
				dwstart = dwend;
			}
			if (dwstart < scanline) {
				for (int64_t i = 0; i < scanline-dwstart; i++) dst[dwstart+i] = clear_px;
			}
		}
		dhstart = dhend;
	}
}

struct movie_t {

	using clock = std::chrono::high_resolution_clock;
	using duration = clock::duration;
	using time_point = clock::time_point;
	using nanoseconds = std::chrono::nanoseconds;

	AVFormatContext * m_format_context = nullptr;
	const AVCodec * m_audio_codec = nullptr;
	const AVCodec * m_video_codec = nullptr;
	AVStream * m_audio_stream = nullptr;
	AVStream * m_video_stream = nullptr;
	AVCodecContext * m_audio_context = nullptr;
	AVCodecContext * m_video_context = nullptr;
	AVPacket * m_packet = nullptr;
	AVFrame * m_frame = nullptr;
	SwrContext * m_resampler = nullptr;
	time_point m_video_start;
	AVRational m_time_base;
	SDL_AudioDeviceID m_audio_device = 0;
	// SDL3 pushes audio through an SDL_AudioStream rather than SDL_QueueAudio().
	SDL_AudioStream* m_sdl_audio_stream = nullptr;

	int64_t m_audio_index;
	int64_t m_video_index;
	int64_t m_flags;

	int64_t m_output_audio_channels;
	int64_t m_output_audio_frequency;
	AVChannelLayout m_output_audio_layout;
	AVSampleFormat m_output_audio_format;

	MoviePollInputsFn m_poll_inputs;
	MovieClearKeyPressedFn m_clear_key_pressed;

	movie_t(const char * filename, const int64_t flags, MoviePollInputsFn poll_inputs_fn, MovieClearKeyPressedFn clear_key_pressed_fn) {
		m_flags = flags;
		m_poll_inputs = poll_inputs_fn;
		m_clear_key_pressed = clear_key_pressed_fn;
		m_video_start = time_point();
		open_input(filename);
		open_audio_device();
		find_stream_info();
		setup_audio();
		setup_video();
		make_packet();
		make_frame();
		if (m_audio_context) {
			make_resampler();
		}
		m_time_base = m_format_context->streams[m_video_index]->time_base;
	}

	~movie_t() noexcept {
		if (m_format_context) {
			avformat_close_input(&m_format_context);
		}
		if (m_audio_context) {
			avcodec_free_context(&m_audio_context);
		}
		if (m_video_context) {
			avcodec_free_context(&m_video_context);
		}
		if (m_frame) {
			av_frame_free(&m_frame);
		}
		if (m_packet) {
			av_packet_free(&m_packet);
		}
		if (m_resampler) {
			swr_free(&m_resampler);
		}
		if (m_audio_device > 0) {
			SDL_CloseAudioDevice(m_audio_device); // also destroys bound streams
			m_audio_device = 0;
			m_sdl_audio_stream = nullptr;
		}
	}

	void open_input(const char * filename) {
		if (avformat_open_input(&m_format_context, filename, nullptr, nullptr) != 0) {
			throw std::runtime_error("Cannot open source file");
		}
	}

	AVSampleFormat sdl_to_ffmpeg_format(SDL_AudioFormat format) {
		switch (format) {
			case SDL_AUDIO_S8:  return AV_SAMPLE_FMT_U8;
			case SDL_AUDIO_S16: return AV_SAMPLE_FMT_S16;
			case SDL_AUDIO_S32: return AV_SAMPLE_FMT_S32;
			case SDL_AUDIO_F32: return AV_SAMPLE_FMT_FLT;
			default: return AV_SAMPLE_FMT_NONE;
		}
	}

	AVChannelLayout channels_to_ffmpeg_layout(int64_t channels) {
		switch (channels) {
			case 1: return AV_CHANNEL_LAYOUT_MONO;
			case 2: return AV_CHANNEL_LAYOUT_STEREO;
			case 3: return AV_CHANNEL_LAYOUT_SURROUND;
			case 4: return AV_CHANNEL_LAYOUT_QUAD;
			case 5: return AV_CHANNEL_LAYOUT_4POINT1;
			case 6: return AV_CHANNEL_LAYOUT_5POINT1;
			case 7: return AV_CHANNEL_LAYOUT_6POINT1;
			case 8: return AV_CHANNEL_LAYOUT_7POINT1;
			default: return {};
		}
	}

	void open_audio_device() {
        if (!flag_is_set(m_flags, SMK_NoSound))
        {
            SDL_AudioSpec desired;
            desired.freq = 44100;
            desired.format = SDL_AUDIO_F32;
            desired.channels = 2;
            m_sdl_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);
            if (!m_sdl_audio_stream) {
                throw std::runtime_error("Cannot open audio device");
            }
            m_audio_device = SDL_GetAudioStreamDevice(m_sdl_audio_stream);
            // We push data in the desired format, so configure FFmpeg to match it.
            m_output_audio_channels = desired.channels;
            m_output_audio_frequency = desired.freq;
            m_output_audio_format = sdl_to_ffmpeg_format(desired.format);
            m_output_audio_layout = channels_to_ffmpeg_layout(desired.channels);
        }
	}

	void find_stream_info() {
		if (avformat_find_stream_info(m_format_context, nullptr) < 0) {
			throw std::runtime_error("Could not find stream information");
		}
	}

	AVCodecContext * make_context(const AVCodec * codec) {
		const auto context = avcodec_alloc_context3(codec);
		if (!context) {
			throw std::runtime_error("Failed to allocate codec context");
		}
		return context;
	}

	const AVCodec * find_codec(const AVStream * stream) {
		return avcodec_find_decoder(stream->codecpar->codec_id);
	}

	void copy_parameters(AVCodecContext * codec_context, const AVStream * stream) {
		if (avcodec_parameters_to_context(codec_context, stream->codecpar) != 0) {
			throw std::runtime_error("Failed to copy codec parameters to decoder context");
		}
	}

	void open_codec(AVCodecContext * codec_context, const AVCodec * codec) {
		if (avcodec_open2(codec_context, codec, nullptr) != 0) {
			throw std::runtime_error("Failed to open codec");
		}
	}

	int64_t find_best_stream(AVMediaType type) {
		return av_find_best_stream(m_format_context, type, -1, -1, nullptr, 0);
	}

	void setup_audio() {
        if (!flag_is_set(m_flags, SMK_NoSound))
        {
            m_audio_index = find_best_stream(AVMEDIA_TYPE_AUDIO);
            if (m_audio_index >= 0) {
                m_audio_stream = m_format_context->streams[m_audio_index];
                m_audio_codec = find_codec(m_audio_stream);
                if (m_audio_codec) {
                    m_audio_context = make_context(m_audio_codec);
                    copy_parameters(m_audio_context, m_audio_stream);
                    open_codec(m_audio_context, m_audio_codec);
                }
            }
        }
	}

	void setup_video() {
		m_video_index = find_best_stream(AVMEDIA_TYPE_VIDEO);
		if (m_video_index < 0) {
			throw std::runtime_error("Could not find video stream");
		}
		m_video_stream = m_format_context->streams[m_video_index];
		m_video_codec = find_codec(m_video_stream);
		if (!m_video_codec) {
			throw std::runtime_error("Failed to find codec");
		}
		m_video_context = make_context(m_video_codec);
		copy_parameters(m_video_context, m_video_stream);
		open_codec(m_video_context, m_video_codec);
	}

	void make_frame() {
		m_frame = av_frame_alloc();
		if (!m_frame) {
			throw std::runtime_error("Could not allocate frame");
		}
	}

	void make_packet() {
		m_packet = av_packet_alloc();
		if (!m_packet) {
			throw std::runtime_error("Could not allocate packet");
		}
	}

	void make_resampler() {
		if (swr_alloc_set_opts2(
			&m_resampler,
			&m_output_audio_layout,
			m_output_audio_format,
			m_output_audio_frequency,
			&m_audio_context->ch_layout,
			m_audio_context->sample_fmt,
			m_audio_context->sample_rate,
			0,
			nullptr
		) != 0) {
			throw std::runtime_error("Cannot create resampler");
		}
		if (swr_init(m_resampler) != 0) {
			throw std::runtime_error("Could not initialize resampler");
		}
	}

	duration time_since_video_start() {
		if (m_video_start == time_point()) {
			m_video_start = clock::now();
			return duration();
		}
		return clock::now() - m_video_start;
	}

	void output_audio_frame() {
		const auto sample_size = av_get_bytes_per_sample(m_output_audio_format);
		const auto buffer_samples = swr_get_out_samples(m_resampler, m_frame->nb_samples);
		uint8_t * buffer = nullptr;
		av_samples_alloc(
			&buffer,
			nullptr,
			m_output_audio_channels,
			buffer_samples,
			m_output_audio_format,
			1
		);
		const auto num_samples = m_output_audio_channels * swr_convert(
			m_resampler,
			&buffer,
			buffer_samples,
#if LIBSWRESAMPLE_VERSION_INT >= AV_VERSION_INT(4, 4, 100)
			// since 4.4.100, swr_convert expects a const pointer
			const_cast<const uint8_t **>(m_frame->data),
#else
			m_frame->data,
#endif
			m_frame->nb_samples
		);
		// SDL3: SDL_QueueAudio -> SDL_PutAudioStreamData; devices start paused.
		SDL_PutAudioStreamData(m_sdl_audio_stream, buffer, num_samples * sample_size);
		SDL_ResumeAudioDevice(m_audio_device);
		av_freep(&buffer);
	}

	void output_video_frame() {
		// FFMpeg used to provide m_frame->palette_has_changed but it has been deprecated
		// Assume the palette has changed every frame as there is no way for us to know anymore
		unsigned char rgb8[PALETTE_SIZE];
		for (size_t i = 0; i < PALETTE_COLORS; ++i) {
			rgb8[(i * 3) + 0] = m_frame->data[1][(i * 4) + 2]; // red
			rgb8[(i * 3) + 1] = m_frame->data[1][(i * 4) + 1]; // green
			rgb8[(i * 3) + 2] = m_frame->data[1][(i * 4) + 0]; // blue
		}
		LbScreenWaitVbi(); // this is a no-op today
		RendererSetDisplayPalette(rgb8);
		if (RendererLockFramebuffer() != Lb_SUCCESS) {
			return;
		} else if (m_flags & (SMK_FullscreenFit | SMK_FullscreenStretch | SMK_FullscreenCrop)) { // new scaling mode
			copy_to_screen_scaled(*m_frame, m_flags, rgb8);
		} else {
			copy_to_screen(*m_frame, m_flags, rgb8);
		}
		RendererUnlockFramebuffer();
		RendererPresentStepFrame();
	}

	bool output_audio_frames() {
		while (true) {
			const auto result = avcodec_receive_frame(m_audio_context, m_frame);
			if (result != 0) {
				if (result == AVERROR(EAGAIN)) {
					return true;
				} else {
					return false;
				}
			}
			output_audio_frame();
		}
	}

	void wait_for_pts() {
		const duration pts = nanoseconds((int64_t(m_frame->pts) * (1000000000 / m_time_base.den)) * m_time_base.num);
		const auto now = time_since_video_start();
		const auto delta = pts - now;
		if (delta > duration()) {
			std::this_thread::sleep_for(delta);
		}
	}

	bool output_video_frames() {
		while (true) {
			const auto result = avcodec_receive_frame(m_video_context, m_frame);
			if (result != 0) {
				if (result == AVERROR(EAGAIN)) {
					return true;
				} else {
					return false;
				}
			}
			wait_for_pts();
			output_video_frame();
			if (!m_poll_inputs()) {
				return false;
			} else if (m_flags & SMK_NoStopOnUserInput) {
				return true;
			} else if (lbKeyOn[KC_ESCAPE] || lbKeyOn[KC_RETURN] || lbKeyOn[KC_SPACE] || lbDisplay.LeftButton) {
				m_clear_key_pressed(lbInkey);
				return false;
			}
		}
	}

	bool decode_audio() {
		if (avcodec_send_packet(m_audio_context, m_packet) != 0) {
			return false;
		}
		return output_audio_frames();
	}

	bool decode_video() {
		if (avcodec_send_packet(m_video_context, m_packet) != 0) {
			return false;
		}
		return output_video_frames();
	}

	void flush_audio() {
		output_audio_frames();
	}

	void flush_video() {
		output_video_frames();
	}

	bool read_frame() {
		return av_read_frame(m_format_context, m_packet) >= 0;
	}

	void play() {
		while (read_frame()) {
			if (m_packet->stream_index == m_audio_index && m_audio_context) {
				if (!decode_audio()) {
					break;
				}
			} else if (m_packet->stream_index == m_video_index) {
				if (!decode_video()) {
					break;
				}
			}
		}
		if (m_audio_context) {
			flush_audio();
		}
		flush_video();
	}
};

} // local

extern "C" TbBool play_smk(const char * filename, const int64_t flags, MoviePollInputsFn poll_inputs_fn, MovieClearKeyPressedFn clear_key_pressed_fn) {
	try {
		lbDisplay.LeftButton = 0; // hack?
		movie_t movie(filename, flags, poll_inputs_fn, clear_key_pressed_fn);
		movie.play();
		return true;
	} catch (const std::exception & e) {
		ERRORLOG("Error playing %s: %s", filename, e.what());
	}
	return false;
}


// The legacy FLI/FLC movie-recording encoder (anim_record/anim_stop/
// anim_record_frame and their anim_make_FLI_*/anim_open/anim_write_data
// helpers) lived here, below play_smk() above -- retired
// (docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2) rather
// than ported to the new post-composite capture path. It had already been
// permanently broken since stage 2's true-colour migration: anim_record()
// unconditionally failed its own "LbGraphicsScreenBPP() != 8" guard (the
// draw surface has been RGBA32, never 8bpp, since 02-32bit-software-
// renderer.md landed), so movie_record_start() never actually recorded a
// single frame in any build a user could have run.
