#pragma once
#include "stdafx.h"
#include <SDL.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/mem.h>
	#include <libavutil/time.h>
}

class PacketQueue
{
public:
	PacketQueue()
	{
		mutex = SDL_CreateMutex();
		cond = SDL_CreateCond();
		first_pkt = NULL;
		last_pkt = NULL;
		nb_packets = 0;
		size = 0;
	}

	~PacketQueue()
	{
		SDL_DestroyMutex(mutex);
		SDL_DestroyCond(cond);
	}

	int getSize() { return nb_packets; }

	int Put(AVPacket *pkt)
	{
		SDL_LockMutex(mutex);

		AVPacketList *newP;
		if (av_dup_packet(pkt) < 0)
		{
			SDL_UnlockMutex(mutex);
			return -1;
		}
			
		newP = (AVPacketList*) av_malloc(sizeof(AVPacketList));
		newP->pkt = *pkt;
		newP->next = NULL;
  
		if (!last_pkt)
			first_pkt = newP;
		else
			last_pkt->next = newP;

		last_pkt = newP;
		nb_packets++;
		size += newP->pkt.size;
		SDL_CondSignal(cond);
  
		SDL_UnlockMutex(mutex);
		return 0;
	}

	int Get(AVPacket *pkt)
	{
		AVPacketList *pkt1;
		int ret = 0;

		SDL_LockMutex(mutex);
  
		for(;;)
		{
			pkt1 = first_pkt;
			if (pkt1)
			{
				first_pkt = pkt1->next;
				if (!first_pkt)
					last_pkt = NULL;
				
				nb_packets--;
				size -= pkt1->pkt.size;
				*pkt = pkt1->pkt;
				av_free(pkt1);
				ret = 1;
				break;
			}
			else
			{
				SDL_CondWait(cond, mutex);
			}
		}
		SDL_UnlockMutex(mutex);

		return ret;
	}


	void flush()
	{
		AVPacketList *pkt, *pkt1;

		SDL_LockMutex(mutex);

		for (pkt = first_pkt; pkt != NULL; pkt = pkt1) 
		{
			pkt1 = pkt->next;
			//av_free_packet(&pkt->pkt);
			if (strcmp((char*)pkt->pkt.data, "LAST") == 0)
			{

			}
			else
			{
				av_packet_unref(&pkt->pkt);
				av_freep(&pkt);
			}
		}

		last_pkt = NULL;
		first_pkt = NULL;
		nb_packets = 0;
		size = 0;

		SDL_UnlockMutex(mutex);
	}

private:
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
	bool bLast;

};

