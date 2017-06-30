#pragma once

#include "stdafx.h"

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/mem.h>
	#include <libavutil/time.h>
	#include <libswresample/swresample.h>
	#include <libavutil/opt.h>
}

#include <SDL.h>

#include "PacketQueue.hpp"

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000


class Audio
{
	public:

		Audio(AVStream *aStream)
		{
			quitEvent = false;
			packetQueue = new PacketQueue();
		
			audioStream = aStream;

			codecContext = audioStream->codec;
			codec = avcodec_find_decoder(codecContext->codec_id);

			// Hack to play S16P audio - SDL only plays S16
			if (codecContext->sample_fmt == AV_SAMPLE_FMT_S16P)
				codecContext->request_sample_fmt = AV_SAMPLE_FMT_S16;

			avcodec_open2(codecContext, codec, NULL);	

			SDL_AudioSpec desiredSpecs;
			SDL_AudioSpec specs;
			desiredSpecs.freq = codecContext->sample_rate;
			desiredSpecs.format = AUDIO_S16SYS;
			desiredSpecs.channels = codecContext->channels;
			desiredSpecs.silence = 0;
			desiredSpecs.samples = SDL_AUDIO_BUFFER_SIZE;
			desiredSpecs.callback = PlaybackCallback;
			desiredSpecs.userdata = this;
			
			if (SDL_OpenAudio(&desiredSpecs, &specs) < 0) {
				SDL_Log("Failed to open audio: %s", SDL_GetError());
			}
		}

		~Audio()
		{
			packetQueue->flush();
			delete packetQueue;

			Quit();

			SDL_PauseAudio(1);
			SDL_CloseAudio();
		}

		void Start()
		{
			setResampler();
			SDL_PauseAudio(0);

		}

		void Stop()
		{
			SDL_PauseAudio(1);
		}

		void Resume()
		{
			SDL_PauseAudio(0);
		}

		void PutPacket(AVPacket *pkt)
		{
			packetQueue->Put(pkt);
		}

		// Audio Clock with account of time to copy and process audio in buffer
		double AudioClock()
		{
			//return clock;
			int bytes_per_sec = codecContext->sample_rate * codecContext->channels * av_get_bytes_per_sample(codecContext->sample_fmt);

			int audioBufferDataSize = audioBufferSize - audioBufferIndex;
	
			if (bytes_per_sec != 0)
				return clock - (double)audioBufferDataSize / bytes_per_sec;
			else
				return clock;
		}

		void Quit()
		{
			quitEvent = true;

			if (codecContext)
			{
				avcodec_close(codecContext);
			}

		}
		
		int getPacketSize()
		{
			return packetQueue->getSize();
		}

		void flush_packet()
		{
			packetQueue->flush();
		}

private:
	static void PlaybackCallback(void *userdata, Uint8 *stream, int streamSize)
	{
		Audio *a = (Audio*)userdata;
		a->Playback(stream, streamSize);
		return;
	}

	unsigned int dataLeftInBuffer()
	{
		return audioBufferSize - audioBufferIndex;
	}

	void Playback(Uint8 *stream, int streamSize)
	{
		int dataSizeToCopy;

		while (streamSize > 0)
		{
			// Decide how much data the playback stream can hold
			dataSizeToCopy = dataLeftInBuffer();
			if (dataSizeToCopy > streamSize)
				dataSizeToCopy = streamSize;

			// Copy decoded data from buffer to the playback stream
			memcpy(stream, (uint8_t *)audioBuffer + audioBufferIndex, dataSizeToCopy);
			streamSize -= dataSizeToCopy;
			stream += dataSizeToCopy;
			audioBufferIndex += dataSizeToCopy;

			if (audioBufferIndex >= audioBufferSize)
			{
				// Already played all decoded data - get more
				audioBufferSize = DecodeAudio(audioBuffer);
				audioBufferIndex = 0;
			}
		}
	}

	int setResampler()
	{
		swr = swr_alloc();

		uint64_t channel_layout = codecContext->channel_layout;
		if (channel_layout == 0)
			channel_layout = av_get_default_channel_layout(codecContext->channels);

		av_opt_set_int(swr, "in_channel_layout", channel_layout, 0);
		av_opt_set_int(swr, "in_sample_rate", codecContext->sample_rate, 0);
		av_opt_set_sample_fmt(swr, "in_sample_fmt", codecContext->sample_fmt, 0);

		av_opt_set_int(swr, "out_channel_layout", channel_layout, 0);
		av_opt_set_int(swr, "out_sample_rate", codecContext->sample_rate, 0);
		av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

		return swr_init(swr);
	}

	int DecodeAudio(uint8_t *audioBuffer)
	{
		AVPacket audioPacket;
		AVFrame*  frame = av_frame_alloc();
		int frameFinished = 0;

		int audioDecodedSize, dataSize = 0;

		if (!quitEvent)
		{
			packetQueue->Get(&audioPacket);

			if (strcmp((char*)audioPacket.data, "LAST") == 0)
			{
				SDL_Event e;
				e.type = SDL_QUIT;
				SDL_PushEvent(&e);
			}
			else
			{
				audioDecodedSize = avcodec_decode_audio4(codecContext, frame, &frameFinished, &audioPacket);

				if (frameFinished)
				{
					if (codecContext->sample_fmt != AV_SAMPLE_FMT_S16) {
						dataSize = frame->nb_samples * codecContext->channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
						swr_convert(swr, (uint8_t **)&audioBuffer, frame->nb_samples, (const uint8_t **)frame->extended_data, frame->nb_samples);
					}
					else {
						// Frame loaded from packets - copy it to intermediary buffer
						dataSize = av_samples_get_buffer_size(NULL, codecContext->channels, frame->nb_samples, codecContext->sample_fmt, 1);
						memcpy(audioBuffer, frame->data[0], dataSize);
					}

					UpdateClock(&audioPacket, dataSize);
				}
			}

			//av_free_packet(&audioPacket);
			av_packet_unref(&audioPacket);
		}

		av_frame_free(&frame);

		return dataSize;
	}

	void UpdateClock(AVPacket *packet, int dataSize)
	{
		if (packet->dts != AV_NOPTS_VALUE)
			clock = av_q2d(audioStream->time_base) * packet->dts;
		else if (frame->pkt_pts != AV_NOPTS_VALUE)
			clock = av_q2d(audioStream->time_base) * frame->pkt_pts;
		else
		{
			// if no pts, then compute it
			clock += (double)dataSize / (codecContext->channels * codecContext->sample_rate * av_get_bytes_per_sample(codecContext->sample_fmt));
		}
	}

private:
	bool			quitEvent;

	AVCodecContext  *codecContext;
	AVCodec			*codec;
	AVStream		*audioStream;
	PacketQueue		*packetQueue;
	AVPacket		*audioPacket;
	AVFrame			*frame;
	SwrContext		*swr;
	double			clock;

	uint8_t			audioBuffer[MAX_AUDIO_FRAME_SIZE];
	unsigned int	audioBufferSize;
	unsigned int	audioBufferIndex;
};
