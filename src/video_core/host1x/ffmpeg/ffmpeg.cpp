// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/memory.h"
#include "video_core/host1x/ffmpeg/ffmpeg.h"
#include "video_core/memory_manager.h"

extern "C" {
#ifdef LIBVA_FOUND
// for querying VAAPI driver information
#include <libavutil/hwcontext_vaapi.h>
#endif
}

namespace FFmpeg {

namespace {

constexpr std::array PreferredGpuDecoders = {
#if defined (_WIN32)
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__FreeBSD__)
    AV_HWDEVICE_TYPE_VDPAU,
#elif defined(__unix__)
	AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_VAAPI,
	AV_HWDEVICE_TYPE_VDPAU,
#endif
    AV_HWDEVICE_TYPE_VULKAN,
};

AVPixelFormat GetGpuFormat(AVCodecContext* codec_context, const AVPixelFormat* pix_fmts) {
	// Check if there is a pixel format supported by the GPU decoder.
	const auto desc = av_pix_fmt_desc_get(codec_context->pix_fmt);
	if (desc && desc->flags & AV_PIX_FMT_FLAG_HWACCEL) {
		for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
			if (*p == codec_context->pix_fmt) {
				return codec_context->pix_fmt;
			}
		}
	}

	// Another check to confirm if there is a pixel format supported by specific GPU decoders.
	for (int i = 0;; i++) {
		const AVCodecHWConfig* config = avcodec_get_hw_config(codec_context->codec, i);
		if (!config) {
			break;
		}

		if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && (config->device_type == AV_HWDEVICE_TYPE_CUDA || config->device_type == AV_HWDEVICE_TYPE_VAAPI)) {
			return config->pix_fmt;
		}
	}

	// Fallback to CPU decoder.
    LOG_INFO(HW_GPU, "Could not find compatible GPU pixel format, falling back to CPU");
    av_buffer_unref(&codec_context->hw_device_ctx);

    return codec_context->pix_fmt;
}

std::string AVError(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(errbuf, sizeof(errbuf) - 1, errnum);
    return errbuf;
}

}

Packet::Packet(std::span<const u8> data) {
    m_packet = av_packet_alloc();
    m_packet->data = const_cast<u8*>(data.data());
    m_packet->size = static_cast<s32>(data.size());
}

Packet::~Packet() {
    av_packet_free(&m_packet);
}

Frame::Frame() {
    m_frame = av_frame_alloc();
}

Frame::~Frame() {
    av_frame_free(&m_frame);
}

Decoder::Decoder(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
    const AVCodecID av_codec = [&] {
        switch (codec) {
			case Tegra::Host1x::NvdecCommon::VideoCodec::H264:
				return AV_CODEC_ID_H264;
			case Tegra::Host1x::NvdecCommon::VideoCodec::VP8:
				return AV_CODEC_ID_VP8;
			case Tegra::Host1x::NvdecCommon::VideoCodec::VP9:
				return AV_CODEC_ID_VP9;
			default:
				UNIMPLEMENTED_MSG("Unknown codec {}", codec);
				return AV_CODEC_ID_NONE;
			}
    }();

    m_codec = avcodec_find_decoder(av_codec);
}

bool Decoder::SupportsDecodingOnDevice(AVPixelFormat* out_pix_fmt, AVHWDeviceType type) const {
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_codec, i);
        if (!config) {
            LOG_DEBUG(HW_GPU, "{} decoder does not support device type {}", m_codec->name, av_hwdevice_get_type_name(type));
            break;
        }

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
            LOG_INFO(HW_GPU, "Using {} GPU decoder", av_hwdevice_get_type_name(type));
            *out_pix_fmt = config->pix_fmt;
            return true;
        }
    }

    return false;
}

std::vector<AVHWDeviceType> HardwareContext::GetSupportedDeviceTypes() {
    std::vector<AVHWDeviceType> types;
    AVHWDeviceType current_device_type = AV_HWDEVICE_TYPE_NONE;

    while (true) {
        current_device_type = av_hwdevice_iterate_types(current_device_type);
        if (current_device_type == AV_HWDEVICE_TYPE_NONE) {
            return types;
        }

        types.push_back(current_device_type);
    }
}

HardwareContext::~HardwareContext() {
    av_buffer_unref(&m_gpu_decoder);
}

bool HardwareContext::InitializeForDecoder(DecoderContext& decoder_context, const Decoder& decoder) {
    const auto supported_types = GetSupportedDeviceTypes();
    for (const auto type : PreferredGpuDecoders) {
        AVPixelFormat hw_pix_fmt;

        if (std::ranges::find(supported_types, type) == supported_types.end()) {
            LOG_DEBUG(HW_GPU, "{} explicitly unsupported", av_hwdevice_get_type_name(type));
            continue;
        }

        if (!this->InitializeWithType(type)) {
            continue;
        }

        if (decoder.SupportsDecodingOnDevice(&hw_pix_fmt, type)) {
            decoder_context.InitializeHardwareDecoder(*this, hw_pix_fmt);
            return true;
        }
    }

    return false;
}

bool HardwareContext::InitializeWithType(AVHWDeviceType type) {
    av_buffer_unref(&m_gpu_decoder);

    if (const int ret = av_hwdevice_ctx_create(&m_gpu_decoder, type, nullptr, nullptr, 0); ret < 0) {
        LOG_DEBUG(HW_GPU, "av_hwdevice_ctx_create({}) failed: {}", av_hwdevice_get_type_name(type), AVError(ret));
        return false;
    }

#ifdef LIBVA_FOUND
    if (type == AV_HWDEVICE_TYPE_VAAPI) {
        // We need to determine if this is an impersonated VAAPI driver.
        auto* hwctx = reinterpret_cast<AVHWDeviceContext*>(m_gpu_decoder->data);
        auto* vactx = static_cast<AVVAAPIDeviceContext*>(hwctx->hwctx);
        const char* vendor_name = vaQueryVendorString(vactx->display);
        if (strstr(vendor_name, "VDPAU backend")) {
            // VDPAU impersonated VAAPI impls are super buggy, we need to skip them.
            LOG_DEBUG(HW_GPU, "Skipping VDPAU impersonated VAAPI driver");
            return false;
        } else {
            // According to some user testing, certain VAAPI drivers (Intel?) could be buggy.
            // Log the driver name just in case.
            LOG_DEBUG(HW_GPU, "Using VAAPI driver: {}", vendor_name);
        }
    }
#endif

    return true;
}

DecoderContext::DecoderContext(const Decoder& decoder) : m_decoder{decoder} {
    m_codec_context = avcodec_alloc_context3(m_decoder.GetCodec());
    av_opt_set(m_codec_context->priv_data, "tune", "zerolatency", 0);
    m_codec_context->thread_count = 0;
    m_codec_context->thread_type &= ~FF_THREAD_FRAME;
}

DecoderContext::~DecoderContext() {
    av_buffer_unref(&m_codec_context->hw_device_ctx);
    avcodec_free_context(&m_codec_context);
}

void DecoderContext::InitializeHardwareDecoder(const HardwareContext& context, AVPixelFormat hw_pix_fmt) {
    m_codec_context->hw_device_ctx = av_buffer_ref(context.GetBufferRef());
    m_codec_context->get_format = GetGpuFormat;
    m_codec_context->pix_fmt = hw_pix_fmt;
}

bool DecoderContext::OpenContext(const Decoder& decoder) {
    if (const int ret = avcodec_open2(m_codec_context, decoder.GetCodec(), nullptr); ret < 0) {
        LOG_ERROR(HW_GPU, "avcodec_open2 error: {}", AVError(ret));
        return false;
    }

    if (!m_codec_context->hw_device_ctx) {
        LOG_INFO(HW_GPU, "Using FFmpeg software decoding");
    }

    return true;
}

bool DecoderContext::SendPacket(const Packet& packet) {
    if (const int ret = avcodec_send_packet(m_codec_context, packet.GetPacket()); ret < 0 && ret != AVERROR_EOF) {
        LOG_ERROR(HW_GPU, "avcodec_send_packet error: {}", AVError(ret));
        return false;
    }

    return true;
}

std::shared_ptr<Frame> DecoderContext::ReceiveFrame() {
	auto ReceiveImpl = [&](AVFrame* frame) -> bool {
		if (const int ret = avcodec_receive_frame(m_codec_context, frame); ret < 0 && ret != AVERROR_EOF) {
			LOG_ERROR(HW_GPU, "avcodec_receive_frame error: {}", AVError(ret));
			return false;
		}
		return true;
	};

	std::shared_ptr<Frame> intermediate_frame = std::make_shared<Frame>();
	if (!ReceiveImpl(intermediate_frame->GetFrame())) {
		return {};
	}

	m_temp_frame = std::make_shared<Frame>();
	if (m_codec_context->hw_device_ctx) {
		m_temp_frame->SetFormat(AV_PIX_FMT_NV12);
		if (int ret = av_hwframe_transfer_data(m_temp_frame->GetFrame(), intermediate_frame->GetFrame(), 0); ret < 0) {
			LOG_ERROR(HW_GPU, "av_hwframe_transfer_data error: {}", AVError(ret));
			return {};
		}
	} else {
		m_temp_frame->SetFormat(AV_PIX_FMT_YUV420P);
		m_temp_frame = std::move(intermediate_frame);
	}

	return std::move(m_temp_frame);
}

void DecodeApi::Reset() {
    m_hardware_context.reset();
    m_decoder_context.reset();
    m_decoder.reset();
}

bool DecodeApi::Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
    this->Reset();
    m_decoder.emplace(codec);
    m_decoder_context.emplace(*m_decoder);

    // Enable GPU decoding if requested.
    if (Settings::values.nvdec_emulation.GetValue() == Settings::NvdecEmulation::Gpu) {
        m_hardware_context.emplace();
        m_hardware_context->InitializeForDecoder(*m_decoder_context, *m_decoder);
    }

    // Open the decoder context.
    if (!m_decoder_context->OpenContext(*m_decoder)) {
        this->Reset();
        return false;
    }

    return true;
}

bool DecodeApi::SendPacket(std::span<const u8> packet_data) {
    FFmpeg::Packet packet(packet_data);
    return m_decoder_context->SendPacket(packet);
}

std::shared_ptr<Frame> DecodeApi::ReceiveFrame() {
    // Receive raw frame from decoder.
    return m_decoder_context->ReceiveFrame();
}

}
