#pragma once

#include "stdafx.h"

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/mem.h>
	#include <libavutil/time.h>
}

#include <SDL.h>
#include <SDL_ttf.h>
#include "PacketQueue.hpp"
#include "ThreadQueue.hpp"
#include <cstdio>
#include <string>
#include "Util.hpp"

struct SubTitleInfo
{
	uint32_t start_display_time;
	uint32_t end_display_time; 
	char text[256];
};

class SubTitle
{

public:

	SubTitle(AVStream *sStream, bool bSmi = false)
	{
		quitEvent = false;
		packetQueue = new PacketQueue();
		this->bSmi = bSmi;
		codecContext = 0;
		codec = 0;
		subtitleStream = 0;
		bStop = false;

		if (bSmi)
		{

		}
		else
		{
			subtitleStream = sStream;
			codecContext = subtitleStream->codec;
			codec = avcodec_find_decoder(codecContext->codec_id);
			avcodec_open2(codecContext, codec, NULL);
		}
	}

	~SubTitle()
	{
		packetQueue->flush();
		delete packetQueue;

		Quit();		
	}

	SDL_Thread * Start()
	{
		if (bSmi == false)
			return SDL_CreateThread(DecodeVideoThread, "subtitle", this);
	}

	void PutPacket(AVPacket *pkt)
	{
		packetQueue->Put(pkt);
	}
	
	void Quit()
	{
		bStop = false;
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

	SubTitleInfo* getSubTitle()
	{
		return dataQueue.pop();
	}

	bool useSMI()
	{
		return bSmi;
	}

	void Stop()
	{
		bStop = true;
	}

	void Resume()
	{
		bStop = false;
	}

private:

	static int DecodeVideoThread(void *arg)
	{
		SubTitle *s = (SubTitle*)arg;
		s->DecodeSubTitle();
		return 0;
	}

	int DecodeSubTitle()
	{
		AVPacket subtitlePacket;
		AVSubtitle* sub = (AVSubtitle*)av_malloc(sizeof(AVSubtitle));
		int frameFinished;

		while (!quitEvent)
		{
			if (bStop)
			{
			
				SDL_Delay(100);
				continue;
			}

			packetQueue->Get(&subtitlePacket);

			// 마지막 프레임 종료
			if (strcmp((char*)subtitlePacket.data, "LAST") == 0)
			{
				SDL_Event e;
				e.type = SDL_QUIT;
				SDL_PushEvent(&e);
				break;
			}
			else
			{
				int ret = avcodec_decode_subtitle2(codecContext, sub, &frameFinished, &subtitlePacket);
				if (frameFinished)
				{
					if (sub->format == 0) //이미지
					{

					}
					else // 나머지
					{
						SubTitleInfo* subInfo = (SubTitleInfo*)av_malloc(sizeof(SubTitleInfo));

						double sClock = av_q2d(subtitleStream->time_base) * sub->pts / 1000;
						subInfo->start_display_time = sClock + (sub->start_display_time / 1000);
						subInfo->end_display_time = sClock + (sub->end_display_time / 1000);
						strcpy(subInfo->text, Util::ANSIToUTF8((char*)subtitlePacket.data));

						dataQueue.push(subInfo);
					}
				}
			}

			av_packet_unref(&subtitlePacket);
		}

		av_free(&sub);

		return 0;
	}

private:

	bool			quitEvent;
	AVCodecContext  *codecContext;
	AVCodec			*codec;
	AVStream		*subtitleStream;
	PacketQueue		*packetQueue;
	ThreadQueue<SubTitleInfo*>     dataQueue;
	bool			bSmi;
	bool			bStop;
};
