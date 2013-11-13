// This file is part of Wintermute Engine
// For conditions of distribution and use, see copyright notice in license.txt
// http://dead-code.org/redir.php?target=wme


#include "dcgf.h"
#include ".\vidtheoraplayer.h"


#pragma comment(lib, "libtheora.lib")

IMPLEMENT_PERSISTENT(CVidTheoraPlayer, false);

//////////////////////////////////////////////////////////////////////////
CVidTheoraPlayer::CVidTheoraPlayer(CBGame* inGame):CBBase(inGame)
{
	SetDefaults();
}

//////////////////////////////////////////////////////////////////////////
void CVidTheoraPlayer::SetDefaults()
{
	m_File = NULL;
	m_Filename = NULL;
	m_StartTime = 0;
	m_Looping = false;
	m_Sound = NULL;
	m_AudiobufGranulepos = 0;
	m_FreezeGame = false;
	m_CurrentTime = 0;

	m_State = THEORA_STATE_NONE;

	m_VideoFrameReady = false;
	m_AudioFrameReady = false;
	m_VideobufTime = 0;

	m_AudioBuf = NULL;
	m_AudioBufFill = 0;
	m_AudioBufSize = 0;
	m_PlaybackStarted = false;
	m_DontDropFrames = false;

	m_Texture = NULL;
	m_AlphaImage = NULL;
	m_AlphaFilename = NULL;

	m_FrameRendered = false;


	memset(&m_OggSyncState, 0, sizeof(ogg_sync_state));
	memset(&m_OggPage, 0, sizeof(ogg_page));
	memset(&m_VorbisStreamState, 0 , sizeof(ogg_stream_state));
	memset(&m_TheoraStreamState, 0 , sizeof(ogg_stream_state));

	memset(&m_TheoraInfo, 0, sizeof(theora_info));
	memset(&m_TheoraComment, 0, sizeof(theora_comment));
	memset(&m_TheoraState, 0, sizeof(theora_state));

	memset(&m_VorbisInfo, 0, sizeof(vorbis_info));
	memset(&m_VorbisDSPState, 0, sizeof(vorbis_dsp_state));
	memset(&m_VorbisBlock, 0, sizeof(vorbis_block));
	memset(&m_VorbisComment, 0, sizeof(vorbis_comment));

	m_VorbisStreams = m_TheoraStreams = 0;

	GenLookupTables();

	m_SeekingKeyframe = false; 
	m_TimeOffset = 0.0f;

	m_PosX = m_PosY = 0;
	m_PlaybackType = VID_PLAY_CENTER;
	m_PlayZoom = 0.0f;

	m_Subtitler = NULL;

	m_SavedState = THEORA_STATE_NONE;
	m_SavedPos = 0;
	m_Volume = 100;
}

//////////////////////////////////////////////////////////////////////////
CVidTheoraPlayer::~CVidTheoraPlayer(void)
{
	Cleanup();

	SAFE_DELETE_ARRAY(m_Filename);
	SAFE_DELETE_ARRAY(m_AlphaFilename);
	SAFE_DELETE(m_Texture);
	SAFE_DELETE(m_AlphaImage);
	SAFE_DELETE(m_Subtitler);
}

//////////////////////////////////////////////////////////////////////////
void CVidTheoraPlayer::Cleanup()
{
	if(m_VorbisStreams)
	{
		ogg_stream_clear(&m_VorbisStreamState);
		vorbis_block_clear(&m_VorbisBlock);
		vorbis_dsp_clear(&m_VorbisDSPState);
		vorbis_comment_clear(&m_VorbisComment);
		vorbis_info_clear(&m_VorbisInfo);

		m_VorbisStreams = 0;
	}
	if(m_TheoraStreams)
	{
		ogg_stream_clear(&m_TheoraStreamState);
		theora_clear(&m_TheoraState);
		theora_comment_clear(&m_TheoraComment);
		theora_info_clear(&m_TheoraInfo);

		m_TheoraStreams = 0;
	}
	ogg_sync_clear(&m_OggSyncState);


	if(m_File) Game->m_FileManager->CloseFile(m_File);
	m_File = NULL;

	if(m_Sound)
	{
		Game->m_SoundMgr->RemoveSound(m_Sound);
		m_Sound = NULL;
	}

	
	SAFE_DELETE_ARRAY(m_AudioBuf);
	m_AudioBufFill = 0;
	m_AudioBufSize = 0;
}

//////////////////////////////////////////////////////////////////////////
int CVidTheoraPlayer::BufferData(ogg_sync_state* OggSyncState)
{
	if(!m_File) return 0;

	DWORD Size = 4096;
	if(m_File->GetSize() - m_File->GetPos() < Size) Size = m_File->GetSize() - m_File->GetPos();

	char* Buffer = ogg_sync_buffer(OggSyncState, Size);
	m_File->Read(Buffer, Size);
	ogg_sync_wrote(OggSyncState, Size);

	return Size;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Initialize(char* Filename, char* SubtitleFile)
{	
	Cleanup();

	m_File = Game->m_FileManager->OpenFile(Filename);
	if(!m_File) return E_FAIL;

	if(Filename!=m_Filename) CBUtils::SetString(&m_Filename, Filename);

	// start up Ogg stream synchronization layer
	ogg_sync_init(&m_OggSyncState);

	// init supporting Vorbis structures needed in header parsing
	vorbis_comment_init(&m_VorbisComment);
	vorbis_info_init(&m_VorbisInfo);

	// init supporting Theora structures needed in header parsing
	theora_comment_init(&m_TheoraComment);
	theora_info_init(&m_TheoraInfo);



	// Ogg file open; parse the headers
	// Only interested in Vorbis/Theora streams
	ogg_packet TempOggPacket;
	bool IsDone = false;
	while(!IsDone)
	{
		int BytesRead = BufferData(&m_OggSyncState);
		if(BytesRead==0) break;

		while(ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
		{
			ogg_stream_state OggStateTest;

			// is this a mandated initial header? If not, stop parsing
			if(!ogg_page_bos(&m_OggPage))
			{
				// don't leak the page; get it into the appropriate stream
				if(m_TheoraStreams) 
					ogg_stream_pagein(&m_TheoraStreamState, &m_OggPage);
				if(m_VorbisStreams) 
					ogg_stream_pagein(&m_VorbisStreamState, &m_OggPage);

				IsDone = true;
				break;
			}

			ogg_stream_init(&OggStateTest, ogg_page_serialno(&m_OggPage));
			ogg_stream_pagein(&OggStateTest, &m_OggPage);
			ogg_stream_packetout(&OggStateTest, &TempOggPacket);

			// identify the codec: try theora
			if(!m_TheoraStreams && theora_decode_header(&m_TheoraInfo, &m_TheoraComment, &TempOggPacket)>=0)
			{
				// it is theora
				memcpy(&m_TheoraStreamState, &OggStateTest, sizeof(OggStateTest));
				m_TheoraStreams = 1;
			}
			else if(!m_VorbisStreams && vorbis_synthesis_headerin(&m_VorbisInfo, &m_VorbisComment, &TempOggPacket)>=0)
			{
				// it is vorbis
				memcpy(&m_VorbisStreamState, &OggStateTest, sizeof(OggStateTest));
				m_VorbisStreams = 1;
			}
			else
			{
				// whatever it is, we don't care about it
				ogg_stream_clear(&OggStateTest);
			}
		}
	}

	// we're expecting more header packets
	while((m_TheoraStreams && m_TheoraStreams < 3) || (m_VorbisStreams && m_VorbisStreams < 3))
	{
		int Ret;

		// look for further theora headers
		while(m_TheoraStreams && (m_TheoraStreams < 3) && (Ret = ogg_stream_packetout(&m_TheoraStreamState, &TempOggPacket)))
		{
			if(Ret < 0)
			{
				Game->LOG(0, "Error parsing Theora stream headers; corrupt stream?");
				return E_FAIL;
			}
			if(theora_decode_header(&m_TheoraInfo, &m_TheoraComment, &TempOggPacket))
			{
				Game->LOG(0, "Error parsing Theora stream headers; corrupt stream?");
				return E_FAIL;
			}
			m_TheoraStreams++;
			if(m_TheoraStreams==3) break;
		}

		/* look for more vorbis header packets */
		while(m_VorbisStreams && (m_VorbisStreams < 3) && (Ret = ogg_stream_packetout(&m_VorbisStreamState, &TempOggPacket)))
		{
			if(Ret < 0)
			{
				Game->LOG(0, "Error parsing Vorbis stream headers; corrupt stream?");
				return E_FAIL;
			}
			if(vorbis_synthesis_headerin(&m_VorbisInfo, &m_VorbisComment, &TempOggPacket))
			{
				Game->LOG(0, "Error parsing Vorbis stream headers; corrupt stream?");
				return E_FAIL;
			}
			m_VorbisStreams++;
			if(m_VorbisStreams==3) break;
		}

		// The header pages/packets will arrive before anything else we
		// care about, or the stream is not obeying spec
		if(ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
		{
			if(m_TheoraStreams) 
				ogg_stream_pagein(&m_TheoraStreamState, &m_OggPage);
			if(m_VorbisStreams) 
				ogg_stream_pagein(&m_VorbisStreamState, &m_OggPage);
		}
		else
		{
			int Ret = BufferData(&m_OggSyncState); // someone needs more data
			if(Ret==0)
			{
				Game->LOG(0, "End of file while searching for codec headers");
				return E_FAIL;
			}
		}
	}



	// and now we have it all.  initialize decoders
	if(m_TheoraStreams)
	{
		theora_decode_init(&m_TheoraState, &m_TheoraInfo);
	}
	else{
		// tear down the partial theora setup
		theora_info_clear(&m_TheoraInfo);
		theora_comment_clear(&m_TheoraComment);
	}
	
	if(m_VorbisStreams)
	{
		vorbis_synthesis_init(&m_VorbisDSPState, &m_VorbisInfo);
		vorbis_block_init(&m_VorbisDSPState, &m_VorbisBlock);
		
	}
	else
	{
		// tear down the partial vorbis setup
		vorbis_info_clear(&m_VorbisInfo);
		vorbis_comment_clear(&m_VorbisComment);
	}

	HRESULT Res = S_OK;

	// create sound buffer
	if(m_VorbisStreams && Game->m_SoundMgr->m_SoundAvailable)
	{
		m_Sound = new CBSoundTheora(Game);
		Game->m_SoundMgr->AddSound(m_Sound);
		if(FAILED(Res = m_Sound->InitializeBuffer(this)))
		{
			Game->m_SoundMgr->RemoveSound(m_Sound);
			m_Sound = NULL;
			Game->LOG(Res, "Error initializing sound buffer for Theora file '%s'", Filename);
		}
		else
		{
			SAFE_DELETE_ARRAY(m_AudioBuf);
			m_AudioBufSize = m_Sound->m_StreamBlockSize;
			m_AudioBuf = new ogg_int16_t[m_AudioBufSize];
		}
	}

	// create texture
	if(m_TheoraStreams && !m_Texture)
	{
		if(Game->m_UseD3D)
			m_Texture = new CBSurfaceD3D(Game);
		else
			m_Texture = new CBSurfaceDD(Game);

		if(!m_Texture || FAILED(Res = m_Texture->Create(m_TheoraInfo.width, m_TheoraInfo.height)))
		{
			SAFE_DELETE(m_Texture);
		}
	}

	
	if(!m_Subtitler) m_Subtitler = new CVidSubtitler(Game);
	if(m_Subtitler && Game->m_VideoSubtitles) m_Subtitler->LoadSubtitles(Filename, SubtitleFile);

	return Res;
}


//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::ResetStream()
{
	if(m_Sound) m_Sound->Stop();

	m_TimeOffset = 0.0f;
	Initialize(m_Filename);
	Play(m_PlaybackType, m_PosX, m_PosY, false, false, m_Looping, 0, m_PlayZoom);

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Play(TVideoPlayback Type, int X, int Y, bool FreezeGame, bool FreezeMusic, bool Looping, DWORD StartTime, float ForceZoom, int Volume)
{	
	if(ForceZoom<0.0f) ForceZoom = 100.0f;
	if(Volume<0) m_Volume = Game->m_SoundMgr->GetVolumePercent(SOUND_SFX);
	else m_Volume = Volume;

	m_FreezeGame = FreezeGame;
	if(!m_PlaybackStarted && m_FreezeGame) Game->Freeze(FreezeMusic);

	m_PlaybackStarted = false;
	m_State = THEORA_STATE_PLAYING;

	m_Looping = Looping;
	m_PlaybackType = Type;

	float Width, Height;
	if(m_TheoraStreams)
	{
		Width = (float)m_TheoraInfo.width;
		Height = (float)m_TheoraInfo.height;
	}
	else
	{
		Width = (float)Game->m_Renderer->m_Width;
		Height = (float)Game->m_Renderer->m_Height;
	}

	switch(Type)
	{
		case VID_PLAY_POS:
			m_PlayZoom = ForceZoom;
			m_PosX = X;
			m_PosY = Y;
		break;

		case VID_PLAY_STRETCH:
		{
			float ZoomX = (float)((float)Game->m_Renderer->m_Width / Width * 100);
			float ZoomY = (float)((float)Game->m_Renderer->m_Height / Height * 100);
			m_PlayZoom = min(ZoomX, ZoomY);
			m_PosX = (Game->m_Renderer->m_Width - Width * (m_PlayZoom / 100)) / 2;
			m_PosY = (Game->m_Renderer->m_Height - Height * (m_PlayZoom / 100)) / 2;
		}
		break;

		case VID_PLAY_CENTER:
			m_PlayZoom = 100.0f;
			m_PosX = (Game->m_Renderer->m_Width - Width) / 2;
			m_PosY = (Game->m_Renderer->m_Height - Height) / 2;
		break;
	}


	if(StartTime) SeekToTime(StartTime);

	Update();

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Stop()
{
	if(m_Sound) m_Sound->Stop();
	m_State = THEORA_STATE_FINISHED;
	if(m_FreezeGame) Game->Unfreeze();

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Update()
{
	m_CurrentTime = m_FreezeGame?Game->m_LiveTimer:Game->m_Timer;

	if(!IsPlaying()) return S_OK;

	if(m_PlaybackStarted && m_Sound && !m_Sound->IsPlaying()) return S_OK;

	if(m_PlaybackStarted && !m_FreezeGame && Game->m_State==GAME_FROZEN) return S_OK;

	int Counter = 0;
	while(true)
	{
		if(m_Sound) DecodeVorbis();
		else m_AudioFrameReady = true;

		if(m_Texture) DecodeTheora();
		else m_VideoFrameReady = true;

		if((!m_Sound || !m_AudioFrameReady) && (!m_Texture || !m_VideoFrameReady) && m_File->IsEOF())
		{
			// end playback
			if(!m_Looping)
			{
				m_State = THEORA_STATE_FINISHED;
				if(m_Sound) m_Sound->Stop();
				if(m_FreezeGame) Game->Unfreeze();
				break;
			}
			else
			{
				ResetStream();
				return S_OK;
			}
		}


		if(!m_VideoFrameReady || !m_AudioFrameReady)
		{
			Counter++;
			if(StreamInData()==0) break;
		}
		else break;
	}


	// If playback has begun, top audio buffer off immediately.
	//if(m_Sound) WriteAudio();

	// are we at or past time for this video frame?	
	if(m_PlaybackStarted && m_VideoFrameReady && (!m_FrameRendered || m_VideobufTime <= GetMovieTime()))
	{
		//Game->LOG(0, "%f %f", m_VideobufTime, GetMovieTime());
		if(m_Texture) WriteVideo();
		m_VideoFrameReady = false;

		if(m_SavedState==THEORA_STATE_PAUSED)
		{
			Pause();
			m_SavedState = THEORA_STATE_NONE;
		}
	}

	// if our buffers either don't exist or are ready to go,
	// we can begin playback
	bool StartNow = false;
	if((!m_TheoraStreams || m_VideoFrameReady) &&
	   (!m_VorbisStreams || m_AudioFrameReady)) StartNow = true;
	// same if we've run out of input
	if(m_File->IsEOF()) StartNow = true;


	if(m_Sound) WriteAudio();


	if(!m_PlaybackStarted && StartNow && !m_SeekingKeyframe)
	{
		//m_StartTime = timeGetTime();
		m_StartTime = m_CurrentTime;
		if(m_Sound)
		{
			m_Sound->SetPrivateVolume(m_Volume);
			m_Sound->Play();
		}
		m_PlaybackStarted = true;
	}

	if(m_Subtitler && Game->m_VideoSubtitles) m_Subtitler->Update(GetMovieFrame());

	return S_OK;
}


//////////////////////////////////////////////////////////////////////////
int CVidTheoraPlayer::StreamInData()
{
	// no data yet for somebody.  Grab another page
	int BytesRead = BufferData(&m_OggSyncState);
	while (ogg_sync_pageout(&m_OggSyncState, &m_OggPage) > 0)
	{
		if(m_TheoraStreams) 
			ogg_stream_pagein(&m_TheoraStreamState, &m_OggPage);
		if(m_VorbisStreams) 
			ogg_stream_pagein(&m_VorbisStreamState, &m_OggPage);
	}
	return BytesRead;
}

//////////////////////////////////////////////////////////////////////////
void CVidTheoraPlayer::DecodeVorbis()
{
	if(!m_Sound) return;

	while(m_VorbisStreams && !m_AudioFrameReady)
	{
		int ret;
		float** pcm;

		// if there's pending, decoded audio, grab it
		if((ret=vorbis_synthesis_pcmout(&m_VorbisDSPState, &pcm)) > 0 && !m_SeekingKeyframe)
		{
			int count = m_AudioBufFill / 2;
			int maxsamples = (m_AudioBufSize - m_AudioBufFill)/2/m_VorbisInfo.channels;
			
			int i;
			for(i=0; i<ret && i<maxsamples; i++)
				for(int j=0; j<m_VorbisInfo.channels; j++)
				{
					int val=(int)(pcm[j][i]*32767.f);
					if(val>32767)	val=32767;
					if(val<-32768)	val=-32768;

					m_AudioBuf[count++] = val;
				}
				vorbis_synthesis_read(&m_VorbisDSPState, i);
				m_AudioBufFill += i * m_VorbisInfo.channels*2;
				if(m_AudioBufFill == m_AudioBufSize) m_AudioFrameReady = true;
				if(m_VorbisDSPState.granulepos >= 0)
					m_AudiobufGranulepos = m_VorbisDSPState.granulepos-ret+i;
				else
					m_AudiobufGranulepos += i;
		}
		else
		{
			ogg_packet opVorbis;

			//no pending audio; is there a pending packet to decode?
			if( ogg_stream_packetout( &m_VorbisStreamState, &opVorbis) >0 )
			{
				//test for success!
				if(vorbis_synthesis( &m_VorbisBlock, &opVorbis) == 0 )
					vorbis_synthesis_blockin( &m_VorbisDSPState, &m_VorbisBlock );
			}
			else	//we need more data; break out to suck in another page
			{
				break;
			}
		}
	} // while
	
}


//////////////////////////////////////////////////////////////////////////
void CVidTheoraPlayer::DecodeTheora()
{
	ogg_packet opTheora;
	
	while(m_TheoraStreams && !m_VideoFrameReady)
	{
		// theora is one in, one out...
		if(ogg_stream_packetout(&m_TheoraStreamState, &opTheora) > 0)
		{
			theora_decode_packetin(&m_TheoraState, &opTheora);
			m_VideobufTime = theora_granule_time(&m_TheoraState, m_TheoraState.granulepos);

			if(m_SeekingKeyframe)
			{
				if(!theora_packet_iskeyframe(&opTheora)) continue;
				else
				{
					m_SeekingKeyframe = false;
					m_TimeOffset = m_VideobufTime;
				}
			}
			
			if(m_VideobufTime >= GetMovieTime() || m_DontDropFrames) m_VideoFrameReady = true;
		}
		else
		{
			break;
		}
	}

}

//////////////////////////////////////////////////////////////////////////
float CVidTheoraPlayer::GetMovieTime()
{
	if(!m_PlaybackStarted) return 0.0f;
	else if(m_Sound) return (float)(m_Sound->GetPosition()) / 1000.0f + m_TimeOffset;
	else return (float)(m_CurrentTime - m_StartTime) / 1000.0f  + m_TimeOffset;
}


//////////////////////////////////////////////////////////////////////////
int CVidTheoraPlayer::GetMovieFrame()
{
	if(!m_TheoraStreams) return 0;
	float Time = GetMovieTime();

	return Time / ((double)m_TheoraInfo.fps_denominator / m_TheoraInfo.fps_numerator);
}


//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::WriteAudio()
{
	if(m_AudioFrameReady)
	{
		if(m_Sound->WriteBlock((BYTE*)m_AudioBuf, m_AudioBufSize))
		{			
			m_AudioBufFill = 0;
			m_AudioFrameReady = false;
		}
	}
	else if(m_File->IsEOF())
	{
		memset(m_AudioBuf, 0, m_AudioBufSize);
		m_Sound->WriteBlock((BYTE*)m_AudioBuf, m_AudioBufSize);
	}

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::WriteVideo()
{
	if(!m_Texture) return E_FAIL;

	yuv_buffer yuv;
	theora_decode_YUVout(&m_TheoraState, &yuv);
	
	m_Texture->StartPixelOp();
	RenderFrame(m_Texture, &yuv);
	m_Texture->EndPixelOp();
	
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Display(DWORD Alpha)
{
	RECT rc;
	HRESULT Res;

	if(m_Texture)
	{
		SetRect(&rc, 0, 0, m_Texture->GetWidth(), m_Texture->GetHeight());
		if(m_PlayZoom==100.0f) Res = m_Texture->DisplayTrans(m_PosX, m_PosY, rc, Alpha);
		else Res = m_Texture->DisplayTransZoom(m_PosX, m_PosY, rc, m_PlayZoom, m_PlayZoom, Alpha);
	}
	else Res = E_FAIL;

	if(m_Subtitler && Game->m_VideoSubtitles) m_Subtitler->Display();

	return Res;
}

//////////////////////////////////////////////////////////////////////////
void CVidTheoraPlayer::GenLookupTables()
{
	//used to bring the table into the high side (scale up) so we
	//can maintain high precision and not use floats (FIXED POINT)
	int scale = 1L << 13;
	int temp;

	for(unsigned int i = 0; i < 256; i++)
	{
		temp = i - 128;

		m_YTable[i]  = (unsigned int)((1.164 * scale + 0.5) * (i - 16));	//Calc Y component

		m_RVTable[i] = (unsigned int)((1.596 * scale + 0.5) * temp);		//Calc R component

		m_GUTable[i] = (unsigned int)((0.391 * scale + 0.5) * temp);		//Calc G u & v components
		m_GVTable[i] = (unsigned int)((0.813 * scale + 0.5) * temp);

		m_BUTable[i] = (unsigned int)((2.018 * scale + 0.5) * temp);		//Calc B component
	}
}

#define CLIP_RGB_COLOR( rgb_color_test ) max( min(rgb_color_test, 255), 0 )
//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::RenderFrame(CBSurface* Texture, yuv_buffer* yuv)
{
	//Convert 4:2:0 YUV YCrCb to an RGB24 Bitmap
	//convenient pointers
	int TargetX1 = 0;
	int TargetX2 = 1;
	int TargetY1 = 0;
	int TargetY2 = 1;

	unsigned char* ySrc = (unsigned char*)yuv->y;
	unsigned char* uSrc = (unsigned char*)yuv->u;
	unsigned char* vSrc = (unsigned char*)yuv->v;
	unsigned char* ySrc2 = ySrc + yuv->y_stride;

	//Calculate buffer offset
	int yOff = (yuv->y_stride * 2) - yuv->y_width;


	//Check if upside down, if so, reverse buffers and offsets
	if ( yuv->y_height < 0 )
	{
		yuv->y_height = -yuv->y_height;
		ySrc		 += (yuv->y_height - 1) * yuv->y_stride;

		uSrc += ((yuv->y_height / 2) - 1) * yuv->uv_stride;
		vSrc += ((yuv->y_height / 2) - 1) * yuv->uv_stride;

		ySrc2 = ySrc - yuv->y_stride;
		yOff  = -yuv->y_width - ( yuv->y_stride * 2 );

		yuv->uv_stride = -yuv->uv_stride;
	}

	//Cut width and height in half (uv field is only half y field)
	yuv->y_height = yuv->y_height >> 1;
	yuv->y_width = yuv->y_width >> 1;

	//Convientient temp vars
	signed int r, g, b, u, v, bU, gUV, rV, rgbY;
	int x;

	//Loop does four blocks per iteration (2 rows, 2 pixels at a time)
	for (int y = yuv->y_height; y > 0; --y)
	{
		for (x = 0; x < yuv->y_width; ++x) 
		{
			//Get uv pointers for row
			u = uSrc[x]; 
			v = vSrc[x];

			//get corresponding lookup values
			rgbY= m_YTable[*ySrc];				
			rV  = m_RVTable[v];
			gUV = m_GUTable[u] + m_GVTable[v];
			bU  = m_BUTable[u];
			++ySrc;

			//scale down - brings are values back into the 8 bits of a byte
			r = CLIP_RGB_COLOR((rgbY + rV ) >> 13);
			g = CLIP_RGB_COLOR((rgbY - gUV) >> 13);
			b = CLIP_RGB_COLOR((rgbY + bU ) >> 13);
			Texture->PutPixel(TargetX1, TargetY1, r, g, b, GetAlphaAt(TargetX1, TargetY1));

			//And repeat for other pixels (note, y is unique for each
			//pixel, while uv are not)
			rgbY = m_YTable[*ySrc];
			r = CLIP_RGB_COLOR((rgbY + rV)  >> 13);
			g = CLIP_RGB_COLOR((rgbY - gUV) >> 13);
			b = CLIP_RGB_COLOR((rgbY + bU)  >> 13);
			Texture->PutPixel(TargetX2, TargetY1, r, g, b, GetAlphaAt(TargetX2, TargetY1));
			++ySrc;

			rgbY = m_YTable[*ySrc2];
			r = CLIP_RGB_COLOR((rgbY + rV)  >> 13);
			g = CLIP_RGB_COLOR((rgbY - gUV) >> 13);
			b = CLIP_RGB_COLOR((rgbY + bU)  >> 13);
			Texture->PutPixel(TargetX1, TargetY2, r, g, b, GetAlphaAt(TargetX1, TargetY2));
			++ySrc2;

			rgbY = m_YTable[*ySrc2];
			r = CLIP_RGB_COLOR((rgbY + rV)  >> 13);
			g = CLIP_RGB_COLOR((rgbY - gUV) >> 13);
			b = CLIP_RGB_COLOR((rgbY + bU)  >> 13);
			Texture->PutPixel(TargetX2, TargetY2, r, g, b, GetAlphaAt(TargetX2, TargetY2));
			++ySrc2;

			/*
			Texture->PutPixel(TargetX1, TargetY1, 255, 0, 0, GetAlphaAt(TargetX1, TargetY1));
			Texture->PutPixel(TargetX2, TargetY1, 255, 0, 0, GetAlphaAt(TargetX2, TargetY1));
			Texture->PutPixel(TargetX1, TargetY2, 255, 0, 0, GetAlphaAt(TargetX1, TargetY2));
			Texture->PutPixel(TargetX2, TargetY2, 255, 0, 0, GetAlphaAt(TargetX2, TargetY2));
			*/


			//Advance inner loop offsets
			TargetX1+=2;
			TargetX2+=2;
		} // end for x

		//Advance destination pointers by offsets
		TargetX1 = 0;
		TargetX2 = 1;
		TargetY1+=2;
		TargetY2+=2;

		ySrc			+= yOff;
		ySrc2			+= yOff;
		uSrc			+= yuv->uv_stride;
		vSrc			+= yuv->uv_stride;
	} //end for y
	
	m_FrameRendered = true;

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::SetAlphaImage(char* Filename)
{	
	SAFE_DELETE(m_AlphaImage);
	m_AlphaImage = new CBImage(Game);
	if(!m_AlphaImage || FAILED(m_AlphaImage->LoadFile(Filename)))
	{
		SAFE_DELETE(m_AlphaImage);
		SAFE_DELETE_ARRAY(m_AlphaFilename);
		return E_FAIL;
	}
	if(m_AlphaFilename != Filename) CBUtils::SetString(&m_AlphaFilename, Filename);
	m_AlphaImage->Convert(IMG_TRUECOLOR);
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
BYTE CVidTheoraPlayer::GetAlphaAt(int X, int Y)
{
	if(m_AlphaImage) return m_AlphaImage->GetAlphaAt(X, Y);
	else return 0xFF;
}


//////////////////////////////////////////////////////////////////////////
inline int intlog(int num) 
{
	int r = 0;
	while( num > 0 )
	{
		num = num / 2;
		r = r + 1;
	}

	return r;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::SeekToTime(DWORD Time)
{
	if(!m_TheoraStreams) return E_FAIL;


	float TargetTime = Time / 1000.0f;


	ogg_page page;
	int read = 1;
	ogg_int64_t gran;
	float movieLength = 0;
	DWORD LastPos = 0;

	int keyframe_granule_shift=intlog(m_TheoraInfo.keyframe_frequency_force - 1);

	while( !m_File->IsEOF() && read != 0)
	{
		read = BufferData(&m_OggSyncState);

		while(ogg_sync_pageout(&m_OggSyncState, &page) > 0)
		{
			int serno = ogg_page_serialno(&page);
			//This is theora stream we were searching for
			if(m_TheoraStreamState.serialno == serno)
			{
				//Calculate a rough time estimate
				gran = ogg_page_granulepos(&page);
				if(gran >= 0)
				{
					ogg_int64_t iframe = gran >> keyframe_granule_shift;
					ogg_int64_t pframe = gran	-(iframe << keyframe_granule_shift );
					movieLength = (iframe+pframe)*
						((double)m_TheoraInfo.fps_denominator / m_TheoraInfo.fps_numerator);
					
					if(movieLength >= TargetTime)
					{
						m_TimeOffset = movieLength;
						//m_TimeOffset = TargetTime;
						//m_File->Seek(LastPos);

						goto finish;
					}
					LastPos = m_File->GetPos();
				}
			}
		}
	}

finish:	
	ogg_sync_reset(&m_OggSyncState);

	ogg_stream_reset(&m_TheoraStreamState);
	ogg_stream_reset(&m_VorbisStreamState);

	theora_clear(&m_TheoraState);
	theora_decode_init(&m_TheoraState, &m_TheoraInfo);
	vorbis_synthesis_restart(&m_VorbisDSPState);

	m_SeekingKeyframe = true;

	//theora_packet_iskeyframe 


	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Pause()
{
	if(m_State == THEORA_STATE_PLAYING)
	{
		m_State = THEORA_STATE_PAUSED;
		if(m_Sound) m_Sound->Pause();
		return S_OK;
	}
	else return E_FAIL;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Resume()
{
	if(m_State == THEORA_STATE_PAUSED)
	{
		m_State = THEORA_STATE_PLAYING;
		if(m_Sound) m_Sound->Resume();
		return S_OK;
	}
	else return E_FAIL;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::Persist(CBPersistMgr* PersistMgr)
{
	//CBBase::Persist(PersistMgr);

	if(PersistMgr->m_Saving)
	{
		m_SavedPos = GetMovieTime() * 1000;
		m_SavedState = m_State;
	}
	else
	{
		SetDefaults();
	}

	PersistMgr->Transfer(TMEMBER(Game));
	PersistMgr->Transfer(TMEMBER(m_SavedPos));
	PersistMgr->Transfer(TMEMBER(m_SavedState));
	PersistMgr->Transfer(TMEMBER(m_Filename));
	PersistMgr->Transfer(TMEMBER(m_AlphaFilename));
	PersistMgr->Transfer(TMEMBER(m_PosX));
	PersistMgr->Transfer(TMEMBER(m_PosY));
	PersistMgr->Transfer(TMEMBER(m_PlayZoom));
	PersistMgr->Transfer(TMEMBER_INT(m_PlaybackType));
	PersistMgr->Transfer(TMEMBER(m_Looping));

	if(PersistMgr->CheckVersion(1, 7, 3))
	{
		PersistMgr->Transfer(TMEMBER(m_Volume));
	}
	else
	{
		m_Volume = 100;
	}

	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
HRESULT CVidTheoraPlayer::InitializeSimple()
{
	if(SUCCEEDED(Initialize(m_Filename)))
	{
		if(m_AlphaFilename) SetAlphaImage(m_AlphaFilename);
		Play(m_PlaybackType, m_PosX, m_PosY, false, false, m_Looping, m_SavedPos, m_PlayZoom);
	}
	else m_State = THEORA_STATE_FINISHED;
	
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////
CBSurface* CVidTheoraPlayer::GetTexture()
{
	return m_Texture;
}