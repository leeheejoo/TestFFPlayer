
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
#include <string>
#include <mmdeviceapi.h>
#include <endpointvolume.h>


#undef main

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_RESTART_EVENT (SDL_USEREVENT+1)

#include "Video.hpp"
#include "Audio.hpp"
#include "Syncer.hpp"
#include "SubTitle.hpp"

#define INT64_MIN        (-9223372036854775807i64 - 1)
#define INT64_MAX        9223372036854775807i64

class Multimedia
{	

public:

	Multimedia() : A(0), V(0), S(0)
	{
		quitEvent = false;
		formatContext = NULL;
		int ret = SDL_Init(SDL_INIT_VIDEO| SDL_INIT_AUDIO |SDL_INIT_TIMER);
		av_register_all();
		avformat_network_init();
		bStop = false;

		volumn = 0.3;
		ChangeVolume(volumn, true);

		SeekMutex = SDL_CreateMutex();
	}		

	~Multimedia()
	{
		avformat_flush(formatContext);
		avformat_close_input(&formatContext);
		avformat_network_deinit();

		SDL_DestroyMutex(SeekMutex);

		SDL_Quit();		
	}

	void Open(char * f, char* fSmi = 0)
	{
		quitEvent = false;
		filename = f;

		if (formatContext)
		{
			avformat_flush(formatContext);
			avformat_close_input(&formatContext);
		}

		int ret = avformat_open_input (&formatContext, filename, NULL, NULL);
		ret = avformat_find_stream_info(formatContext, NULL);
		av_dump_format(formatContext, 0, filename, 0);

		videoStream = getStreamID(AVMEDIA_TYPE_VIDEO);
		audioStream = getStreamID(AVMEDIA_TYPE_AUDIO);
		subtitleStream = getStreamID(AVMEDIA_TYPE_SUBTITLE);
		
		V = new Video(formatContext->streams[videoStream]);
		A = new Audio(formatContext->streams[audioStream]);

		if (subtitleStream > 0)
			S = new SubTitle(formatContext->streams[subtitleStream]);

		if (fSmi)
			S = new SubTitle(formatContext->streams[subtitleStream], true);

		Sync = new Syncer(V, A);
	}

	void Play()
	{
		bStop = false;

		SDL_AddTimer(10, PushRefreshEvent, V);

		demux = SDL_CreateThread(DemuxThread, "demux", this);
		video = V->Start();
		A->Start();

		if (S)
		{
			if (S->useSMI() == false)
				subtitle = S->Start();

			V->setSubTitle(S);
		}

		StartEventLoop();
		
		SDL_WaitThread(demux, NULL);
		SDL_WaitThread(video, NULL);		
		SDL_WaitThread(subtitle, NULL);
	}			

	void Stop()
	{
		bStop = true;

		A->Stop();

		if (S)
			S->Stop();
	}

	void Resume()
	{
		bStop = false;

		A->Resume();

		if (S)
			S->Resume();
	}

	void ReStart()
	{
		Reset();
		Open(filename);
		Play();
	}

	void Reset()
	{
		V->resetSubtitleInfo();

		quitEvent = true;
		SDL_WaitThread(demux, NULL);

		A->Quit();
		V->Quit();
		if (S)
		{
			S->Quit();
			delete S;
			S = 0;
		}

		//SDL_WaitThread(video, NULL);

		delete A;
		A = 0;

		delete V;
		V = 0;

		delete Sync;
		Sync = 0;

		if (formatContext)
		{
			avformat_flush(formatContext);
			avformat_close_input(&formatContext);
		}
	}

	
	void Quit()
	{
		Reset();

		SDL_Quit();

		exit(0);
	}
	
	void seek(int sec)
	{
		Stop();

		AVRational avr;
		avr.num = 1;
		avr.den = formatContext->streams[videoStream]->time_base.den;

		
		// Video
		int seek_flags = sec < 0 ? AVSEEK_FLAG_BACKWARD : AVSEEK_FLAG_FRAME;
		int64_t seek_pos = (V->VideoClock() + sec)*formatContext->streams[videoStream]->time_base.den;
		seek_pos = av_rescale_q(seek_pos, avr, formatContext->streams[videoStream]->time_base);

		if (seek_pos > formatContext->duration/1000)
			seek_pos = formatContext->duration/1000 - 1000;

		SDL_LockMutex(SeekMutex);

		if (av_seek_frame(formatContext, videoStream, seek_pos, seek_flags) < 0)
		{
			SDL_UnlockMutex(SeekMutex);

			fprintf(stderr, "%s: error while seeking\n", formatContext->filename);

			SDL_Event e;
			e.type = SDL_QUIT;
			SDL_PushEvent(&e);

			return;
		}

		V->flush_packet();
		A->flush_packet();
		if (S)
		{
			V->resetSubtitleInfo();
			S->flush_packet();
		}

		SDL_UnlockMutex(SeekMutex);

		Resume();
	}


private:

	static Uint32 PushRefreshEvent(Uint32 interval, void *userdata)
	{
		SDL_Event e;
		e.type = FF_REFRESH_EVENT;
		SDL_PushEvent(&e);
		return 0;
	}

	int getStreamID(AVMediaType type)
	{
		unsigned int i;
		for (i = 0; i < formatContext->nb_streams; i++)
			if (formatContext->streams[i]->codec->codec_type == type)
				return i;

		return -1;
	}

	static int DemuxThread(void *arg)
	{
		Multimedia *m = (Multimedia*)arg;
		m->Demux();

		return 0;
	}

	int Demux()
	{
		AVPacket packet;

		while (!quitEvent)
		{
			if (V->getPacketSize() > 60 || A->getPacketSize() > 60 || bStop)
			{
				SDL_Delay(100);
			}
		
			SDL_LockMutex(SeekMutex);

			if (av_read_frame(formatContext, &packet) < 0)
			{
				AVPacket packetLastV;
				av_packet_from_data(&packetLastV, (uint8_t*)"LAST", 4);
				V->PutPacket(&packetLastV);

				SDL_UnlockMutex(SeekMutex);

				break;
			}

			if (packet.stream_index == videoStream)
				V->PutPacket(&packet);
			else if (packet.stream_index == audioStream)
				A->PutPacket(&packet);
			else if ( S->useSMI() == false && packet.stream_index == subtitleStream)
				S->PutPacket(&packet);

			SDL_UnlockMutex(SeekMutex);
		}

		return 0;
	}

	void StartEventLoop()
	{
		SDL_Event event;

		while (quitEvent != 1)
		{
			if (SDL_WaitEvent(&event)) {

				if (event.type == SDL_QUIT)
				{
					Quit();
				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_SPACE)
				{
					if (bStop)
						Resume();
					else
						Stop();
				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_LEFT)
				{
					seek(-10);
				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_RIGHT)
				{
					seek(10);
				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_UP)
				{
					volumn += 0.05;
					if (volumn > 1)
						volumn = 1;

					ChangeVolume(volumn, true);

				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_DOWN)
				{
					volumn -= 0.05;
					if (volumn < 0)
						volumn = 0;

					ChangeVolume(volumn, true);

				}
				else if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_RETURN)
				{
					if (V->isFullScreen())
						V->fullScreen(false);
					else
						V->fullScreen(true);

				}
				else if (event.type == SDL_MOUSEBUTTONDOWN)
				{
					if (bStop)
						Resume();
					else
						Stop();
				}
				else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_MOVED)
				{
					Resume();
				}
				else if (event.type == FF_RESTART_EVENT)
				{
					ReStart();
				}

				if (event.type == FF_REFRESH_EVENT)
				{

					if (bStop)
					{
						SDL_AddTimer(100, PushRefreshEvent, NULL);
					}
					else
					{
						Uint32 delay = (int)(Sync->computeFrameDelay() * 1000);
						SDL_AddTimer(delay, PushRefreshEvent, NULL);
						V->RenderPicture();
					}
				}
			}
		}
	}


	bool ChangeVolume(double nVolume, bool bScalar)
	{

		HRESULT hr = NULL;
		bool decibels = false;
		bool scalar = false;
		double newVolume = nVolume;

		IMMDeviceEnumerator *deviceEnumerator = NULL;
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID *)&deviceEnumerator);
		IMMDevice *defaultDevice = NULL;

		hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
		deviceEnumerator->Release();
		deviceEnumerator = NULL;

		IAudioEndpointVolume *endpointVolume = NULL;
		hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID *)&endpointVolume);
		defaultDevice->Release();
		defaultDevice = NULL;

		float currentVolume = 0;
		endpointVolume->GetMasterVolumeLevel(&currentVolume);

		hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);

		if (bScalar == false)
		{
			hr = endpointVolume->SetMasterVolumeLevel((float)newVolume, NULL);
		}
		else if (bScalar == true)
		{
			hr = endpointVolume->SetMasterVolumeLevelScalar((float)newVolume, NULL);
		}
		endpointVolume->Release();

		return FALSE;
	}

public:
	bool			bStop;

private:
	bool			quitEvent;
	char			*filename;
	AVFormatContext *formatContext;
	Video			*V;
	Audio			*A;
	SubTitle		*S;
	Syncer			*Sync;
	int				videoStream;
	int				audioStream;
	int				subtitleStream;
	SDL_Thread		*demux;
	SDL_Thread		*video;
	SDL_Thread		*subtitle;
	SDL_mutex		*SeekMutex;
	double			volumn;

};

int main(int argc, char *argv[])
{
	char * filename = "";

#ifdef _DEBUG

	//if (argc < 2) {
	//	fprintf(stderr, "Usage: player.exe <file>\n");
	//	exit(1);
	//} else {
		//filename = argv[1];

		//filename = "rtsp://mpv.cdn3.bigCDN.com:554/bigCDN/_definst_/mp4:bigbuckbunnyiphone_400.mp4";

		//filename = "http://tomtom2k5.free.fr/mkv/720p/You.Dont.Mess.With.The.Zohan.2008.720p.mkv";  //자막 포함
		//filename = "http://tomtom2k5.free.fr/mkv/720p/pixar.short.film.collection.720p.bluray.x264.sample-sinners.mkv";
		//filename = "http://tomtom2k5.free.fr/mkv/720p/star.wars.episode.i.the.phantom.menace.1999.720p.hdtv.x264.internal.sample-hv.mkv";  //자막 포함
		//filename = "http://tomtom2k5.free.fr/mkv/720p/transformers.2007.720p.hddvd.x264.sample-hv.mkv";
		//filename = "http://tomtom2k5.free.fr/mkv/720p/vadrouille_x264hd-sample.mkv";
		//filename = "http://tomtom2k5.free.fr/mkv/1080p/Batman.Begins.1080p.HDDVD.x264-ESiR-Sample.mkv";

		//filename = "D:/test/sample/300_x264hd_sample.mkv";
		//filename = "D:/test/sample/batman.mkv";
		filename = "D:/test/sample/star.mkv";
		//filename = "D:/test/sample/video.mp4";
		//filename = "D:/test/sample/KakaoTalk.mp4";

		//filename = "http://tomtom2k5.free.fr/mkv/720p/NEXT%20720p%20HD%20sample-029.mkv";
	//}
#else

	// test 분기 만듬 ㅋㅋㅋ
	// ㅋㅋㅋ
	// 111

	if (argc < 2) {
		fprintf(stderr, "Usage: player.exe <file>\n");
		exit(1);
	} else {
		filename = argv[1];
	}

#endif

	if ( strcmp(filename,"") == 0)
		return 0;
	
	CoInitialize(NULL);

	Multimedia m;
	m.Open(filename);
	m.Play();

	CoUninitialize();

	return 0;
}

