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
#include <cstdio>
#include "SubTitle.hpp"

class Video
{
	public:

		Video(AVStream *vStream) : S(0), subData(0)
		{
			quitEvent = false;

			packetQueue = new PacketQueue();

			TTF_Init();
			font = TTF_OpenFont("NanumGothicBold.ttf", 24);
			fontSubTitle = TTF_OpenFont("NanumGothicBold.ttf", 44);
			font_color = { 255, 255, 255 };

			videoStream = vStream;
			codecContext = videoStream->codec;
			codec = avcodec_find_decoder(codecContext->codec_id);
			avcodec_open2(codecContext, codec, NULL);

			screen = SDL_CreateWindow("Test Player",
								  SDL_WINDOWPOS_CENTERED_DISPLAY(0),
								  SDL_WINDOWPOS_CENTERED_DISPLAY(0),
								  codecContext->width, codecContext->height,
								  SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

			renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED);
			texture = SDL_CreateTexture(renderer,
							SDL_PIXELFORMAT_IYUV,							// YUV420P
							SDL_TEXTUREACCESS_STREAMING,
							codecContext->width, codecContext->height);

			PictureMutex = SDL_CreateMutex();
			PictureReadyCond = SDL_CreateCond();
			PictureReady = false;
			bFullScreen = false;
			screenRatio = float(codecContext->height) / codecContext->width;
		}

		~Video()
		{
			packetQueue->flush();
			delete packetQueue;

			Quit();

			SDL_DestroyTexture(texture);
			SDL_DestroyRenderer(renderer);
			SDL_DestroyMutex(PictureMutex);
			SDL_DestroyCond(PictureReadyCond);
			SDL_DestroyWindow(screen);
		
		}

		SDL_Thread * Start()
		{
			return SDL_CreateThread(DecodeVideoThread, "video", this);
		}

		void PutPacket(AVPacket *pkt)
		{
			packetQueue->Put(pkt);
		}
		
		double VideoClock()
		{
			return clock;
		}

		void RenderPicture()
		{
			while (!PictureReady)
			{
				SDL_CondWait(PictureReadyCond, PictureMutex);
				if (quitEvent)
					return;
			}
	
			drawPicture();
			drawTime();
			drawSubtitles();

			SDL_RenderPresent(renderer);

			SDL_LockMutex(PictureMutex);			
			PictureReady = false;
			SDL_CondSignal(PictureReadyCond);
			SDL_UnlockMutex(PictureMutex);

		}

		void drawPicture()
		{
			if (bFullScreen)
			{
				int x = 0;
				int y = 0;
				SDL_GL_GetDrawableSize(screen, &x, &y);

				SDL_Rect src;
				src.x = 0;
				src.y = 0;
				src.w = x;
				src.h = x*screenRatio;

				SDL_Rect desc;
				desc.x = 0;
				desc.y = (y - x*screenRatio) / 2;
				desc.w = x;
				desc.h = x*screenRatio;

				SDL_RenderCopy(renderer, texture, &src, &desc);

			}
			else
			{
				SDL_RenderCopy(renderer, texture, NULL, NULL);
			}
		}

		void drawTime()
		{
			double clock = VideoClock();
			char msg[100];
			sprintf(msg, "Time: %.2f s", clock);
			char* szMsg = Util::ANSIToUTF8(msg);

			//surfaceMessage = TTF_RenderText_Solid(font, msg, font_color);
			surTime = TTF_RenderUTF8_Blended(font, szMsg, font_color);
			texTime = SDL_CreateTextureFromSurface(renderer, surTime);
			SDL_Rect Message_rect;
			Message_rect.x = 10;
			TTF_SizeUTF8(font, szMsg, &Message_rect.w, &Message_rect.h);

			if (bFullScreen)
			{
				int x = 0;
				int y = 0;
				SDL_GL_GetDrawableSize(screen, &x, &y);
				Message_rect.y = (y - x*screenRatio) / 2;
				Message_rect.y += 10;
			}	
			else
			{
				Message_rect.y = 10;			
			}
						
			SDL_RenderCopy(renderer, texTime, NULL, &Message_rect);
			SDL_FreeSurface(surTime);
			SDL_DestroyTexture(texTime);
		}

		void drawSubtitles()
		{
			if (S == 0)
				return;

			if (subData == 0)
				subData = S->getSubTitle();

			if (subData)
			{
				double start = subData->start_display_time;
				double end = subData->end_display_time;
		
				if (clock > start ) // 보이기
				{
					surSubtitle = TTF_RenderUTF8_Blended(fontSubTitle, subData->text, font_color);
					texSubtitle = SDL_CreateTextureFromSurface(renderer, surSubtitle);

					SDL_Rect Message_rect;
					TTF_SizeUTF8(fontSubTitle, subData->text, &Message_rect.w, &Message_rect.h);

					int x = 0;
					int y = 0;
					SDL_GL_GetDrawableSize(screen, &x, &y);

					Message_rect.x = (x - Message_rect.w)/2;
					Message_rect.y = y - Message_rect.h * 2;

					if (bFullScreen)
						Message_rect.y -= (y - x*screenRatio) / 2;

					SDL_RenderCopy(renderer, texSubtitle, NULL, &Message_rect);
					SDL_FreeSurface(surSubtitle);
					SDL_DestroyTexture(texSubtitle);
				}

				if (clock > end) // 감추기
				{
					av_free(subData);
					subData = 0;
				}
			}
		}
			
		void Quit()
		{
			quitEvent = true;
			SDL_CondSignal(PictureReadyCond);

			if (codecContext)
			{
				avcodec_close(codecContext);
			}
		}			
		
		int getPacketSize()
		{
			return packetQueue->getSize();
		}

		void fullScreen(bool bFull)
		{
			bFullScreen = bFull;

			if (bFullScreen)
			{
				SDL_SetWindowFullscreen(screen, SDL_WINDOW_FULLSCREEN_DESKTOP);	
			}
			else
			{		
				SDL_SetWindowFullscreen(screen,0);
			}
		}


		bool isFullScreen()
		{
			return bFullScreen;
		}

		void flush_packet()
		{
			packetQueue->flush();
		}

		void setSubTitle(SubTitle*   S)
		{
			this->S = S;
		}

		void resetSubtitleInfo()
		{
			av_free(subData);
			subData = 0;
		}

private:
		
		double UpdateClock(AVFrame* frame)
		{
			if (frame->pkt_dts != AV_NOPTS_VALUE)
				clock = av_q2d(videoStream->time_base) * frame->pkt_dts;
			else if (frame->pkt_pts != AV_NOPTS_VALUE)
				clock = av_q2d(videoStream->time_base) * frame->pkt_pts;
		
			double frame_delay = av_q2d(codecContext->time_base);
		
			/* if we are repeating a frame, adjust clock accordingly */
			frame_delay += frame->repeat_pict * (frame_delay * 0.5);
			clock += frame_delay;
		
			return clock;
		}
		
		static int DecodeVideoThread(void *arg)
		{
			Video *v = (Video*)arg;
			v->DecodeVideo();
			return 0;
		}
		
		int DecodeVideo()
		{
			AVPacket videoPacket;
			AVFrame*  frame = av_frame_alloc();
			int frameFinished;
		
			while (!quitEvent)
			{
				packetQueue->Get(&videoPacket);
		
				// 마지막 프레임 종료
				if (strcmp((char*)videoPacket.data, "LAST") == 0)
				{
					SDL_Event e;
					e.type = SDL_QUIT;
					SDL_PushEvent(&e);
					
					break;
				}
				else
				{
					int ret = avcodec_decode_video2(codecContext, frame, &frameFinished, &videoPacket);
					if (frameFinished)
					{
						UpdateClock(frame);
						PreparePicture(frame, codecContext);
					}
		
					//av_free_packet(&videoPacket);
				}

				av_packet_unref(&videoPacket);
			}
		
			av_frame_free(&frame);
		
			return 0;
		}
		
		
		uint8_t * ToYUV420(AVFrame* frame, AVCodecContext *codecContext)
		{
			static int numPixels = avpicture_get_size(AV_PIX_FMT_YUV420P, codecContext->width, codecContext->height);
			static uint8_t * buffer = (uint8_t *)malloc(numPixels);
		
			//Set context for conversion
			static struct SwsContext *swsContext = sws_getCachedContext(
				swsContext,
				codecContext->width,
				codecContext->height,
				codecContext->pix_fmt,
				codecContext->width,
				codecContext->height,
				AV_PIX_FMT_YUV420P,
				SWS_BILINEAR,
				NULL,
				NULL,
				NULL
				);
		
			AVFrame* frameYUV420P = av_frame_alloc();
			avpicture_fill((AVPicture *)frameYUV420P, buffer, AV_PIX_FMT_YUV420P, codecContext->width, codecContext->height);
		
			// Convert the image into YUV format that SDL uses
			sws_scale(swsContext, frame->data, frame->linesize, 0, codecContext->height, frameYUV420P->data, frameYUV420P->linesize);
		
			av_frame_free(&frameYUV420P);
		
			return buffer;
		}
		
		void PreparePicture(AVFrame *frame, AVCodecContext *codecContext)
		{
			while (PictureReady)
			{
				SDL_CondWait(PictureReadyCond, PictureMutex);
				if (quitEvent)
					return;
			}
		
			int pitch = codecContext->width * SDL_BYTESPERPIXEL(SDL_PIXELFORMAT_IYUV);
			uint8_t * buffer = ToYUV420(frame, codecContext);
			SDL_UpdateTexture(texture, NULL, buffer, pitch);
		
			SDL_LockMutex(PictureMutex);
			PictureReady = true;
			SDL_CondSignal(PictureReadyCond);
			SDL_UnlockMutex(PictureMutex);
		}
		
private:
	bool			quitEvent;

	SDL_Renderer	*renderer;
	SDL_Texture		*texture;
	SDL_Window		*screen;

	AVCodecContext  *codecContext;
	AVCodec			*codec;
	AVStream		*videoStream;
	PacketQueue		*packetQueue;

	SDL_mutex		*PictureMutex;
	SDL_cond		*PictureReadyCond;
	int				PictureReady;
	float			screenRatio;

	bool			bFullScreen;
	double			clock;
	TTF_Font*		font;
	TTF_Font*		fontSubTitle;
	SDL_Color		font_color;

	SDL_Surface*	surTime;
	SDL_Texture*	texTime;

	SDL_Surface*	surSubtitle;
	SDL_Texture*	texSubtitle;

	SubTitle		*S;
	SubTitleInfo*   subData;
};
