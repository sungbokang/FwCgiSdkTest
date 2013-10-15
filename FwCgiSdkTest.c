#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
// tweaked by SungboKang //////////
#include <pthread.h>
#include <unistd.h>
#define LENGTH_FFMPEG_COMMAND 60
#define MAX_QUEUE_N 256
///////////////////////////////////

#ifdef linux
#include <sys/types.h>
#include <sys/socket.h>		// basic socket definitions
#include <sys/time.h>		// timeval{} for select()
#include <netinet/in.h>		// struct sockaddr_in, htonl()
#include <arpa/inet.h>		// inet_aton(), inet_ntoa(), inet_pton(), inet_ntop()
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <net/if.h>

#include "syt_sys.h"
#include "sock_util.h"
#endif // linux

#ifdef WIN32
#ifndef _WINSOCKAPI_
	#include <winsock2.h>
#endif
	#include <io.h>
#endif


//=============================================================================
#define	__HEADER_STREAM_MODE_SUPPORT
//=============================================================================
#include "FwCgiLib.h"
#include "jpeguser.h"
#include "JesPacket.h"

// #define DECODER_ON

#ifdef DECODER_ON
#include "ffm4l.h"
#endif


//=============================================================================
//	Please set for your test environment
//=============================================================================
#define OPEN_TIMEOUT		8
#define MAX_PACK_SIZE		1024*1024
#define TARGET_IPADDR		"embedded.snut.ac.kr" //"192.168.2.20"
#define TARGET_PORT			8888
#define VSMID				0
#define PAUSE_TIME			0
#define RECV_IMAGE_CNT		150
#define	FW_USER_ID			"root"
#define	FW_PASS_WD			"root"
//=============================================================================

#ifdef WIN32
#define SOCK_STARTUP() { \
	WORD wVersionRequested; \
	WSADATA wsaData; \
	int err; \
	wVersionRequested = MAKEWORD( 2, 2 ); \
	err = WSAStartup( wVersionRequested, &wsaData ); \
	if ( err != 0 ) { \
        exit(0); \
	} \
	if (wsaData.wVersion != MAKEWORD(2,2)) {\
		WSACleanup();\
		exit(0);\
	}\
}
#define SOCK_CLEANUP()					WSACleanup()
#else
#define	SOCK_STARTUP()
#define	SOCK_CLEANUP()
#endif
//=============================================================================

static	long TestCamGetCgiV30Wp(int fwmodid, char *ip_addr, int port, int PortId, char *id, char *pwd, char *pRcvBuf, int BufLen, char *ip_addr_wp, int port_wp);
static	long TestStCtrCgiV30Wp(int fwmodid, int AppKey, int DaemonId, char *Action, char *CamList, unsigned long PauseTime,char *ip_addr, int port, char *id, char *pwd, char *pRcvBuf, int BufLen, char *ip_addr_wp, int port_wp);

#ifdef DECODER_ON
static int CodecChange(Cffm4l*, SytJesVideoCodecTypeEnum);
static int Decoding(Cffm4l* ffm4l, pjpeg_usr_t pImgBuff, char *filename);
#endif

static int SavePacket(char *pImgBuff, char *filename, int ImageSize);

// tweaked by SungboKang /////////
void* Control_thread_function(void *);
void* Ffmpeg_thread_function(void *);

typedef struct {
	int head;
	int tail;
	int queue[MAX_QUEUE_N];
} CircularQueue;
//////////////////////////////////

SytJesVideoCodecTypeEnum setuped_codec;
static	char	wp_domain[] ="";
static	short	wp_port=0;

const int MAX_FRAME_SIZE = (500*1024);
const int MAX_RGB_SIZE = (3*2048*1536);

int main(int argc, char *argv[])
{
	// tweaked by SungboKang //
	int frameCnt = 0;
	int tempSeparateH264FileNumber = 0;
	// pthread_t controlThread;
	///////////////////////////

	char *			pImgBuff;
	SOCKET			StreamSock;
	char			strQuery[MAX_QUERY];

	int				AppKey;
	unsigned long	DaemonId = 0;
	int				ScanMode = SCAN_RAW_MODE;

	int				idx;
	long			nRead;
	int				ImageSize;
	int				nRetCode;
	
	char			tmp_img_file[_MAX_PATH];
	
	SytJesVideoCodecTypeEnum current_codec;
	
#ifdef DECODER_ON
	Cffm4l ffm4l;
#endif
	
	int First_IFrame_Flag = 0;

	pImgBuff = (char*)malloc(MAX_PACK_SIZE);
	if(pImgBuff == NULL)
		return -1;

	
#ifdef DECODER_ON
	if (ffm4l.CreateFfm4lDecoder(CODEC_ID_H264)!=FFM4L_COMMON_SUCCESS)
	{
		printf("ffmpeg Create failed\n");
		free(pImgBuff);
		
		return -1;
	}
#endif

	current_codec = JES_VIDEO_CODEC_H264;
	setuped_codec = JES_VIDEO_CODEC_H264;

	SOCK_STARTUP();

	//=============================================================================
	//Get FlexWATCH System Information
// 	printf("FwSysGetCgiWp\n");
// 	memset(pImgBuff, 0, MAX_PACK_SIZE);
// 	nRead = FwSysGetCgiWp(VSMID, TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);
// 	if(nRead > 0)
// 		printf("FwSysGetCgiWp=[%s]\n",pImgBuff);
	//=============================================================================

	//=============================================================================
	//Open Cgi Stream 
	AppKey = rand();

#ifdef __HEADER_STREAM_MODE_SUPPORT
	sprintf(	strQuery,
				STREAM_CGI_FMT_V30, 
				VSMID, 
				AppKey,
				"0",	// Port Id List (0,1,2,3)
				0x00, // 0x00 = Normal Mode, 0x0f = Header Only Mode 
				PAUSE_TIME, 
				FWCGI_VERSION);

#else	// Normal Mode Only 
	sprintf(	strQuery,
				STREAM_CGI_FMT_V30, 
				VSMID, 
				AppKey,
				"0,1",	// Port Id List (0,1,2,3)
				PAUSE_TIME, 
				FWCGI_VERSION);
#endif

	StreamSock = FwOpenGetCgiWp(TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, strQuery, OPEN_TIMEOUT, wp_domain, wp_port);
	if(StreamSock == INVALID_SOCKET)
	{
		SOCK_CLEANUP();
		free(pImgBuff);
		return 0;
	}
	//=============================================================================

	//=============================================================================
	
	// tweaked by SungboKang ////

	/////////////////////////////
	
	// Get Cgi Stream
	//for(idx=0 ; idx < RECV_IMAGE_CNT ; idx++)
	while(1)
	{
		// tweaked by SungboKang //////////
		// printf("Getting frame...\n");
		printf("Getting frame...%d\n", frameCnt);
		///////////////////////////////////
		nRetCode = FwRcvCgiStream(StreamSock, pImgBuff, MAX_PACK_SIZE, &ImageSize, &ScanMode, &DaemonId);
		
		/******************************** CODE ADDED BY SEYEON TECH START **********************************/
			p_jpeg_usr_t	pJesHeader;    // oject of the class that contains the header information
			unsigned short	JesHeaderSize; //
			char*			pH264Image;
			int				H264FrameSize;

			pJesHeader = (p_jpeg_usr_t)pImgBuff; // magic code

//			pJesHeader->start_of_jpg[0] : It mean JPEG or H.264
//			pJesHeader->start_of_jpg[1] : It mean Sequence Number 0,1,2,3... 

			JesHeaderSize = (unsigned short)(ntohs(pJesHeader->usr_length) + 2) + 1; // previously 2 // added 1 // IT WORKS!!!

			printf("################################ Header Size: %d\n", JesHeaderSize);

			// modify here remove the first 0x00 always
			// look at research log, june 20, 2012
			H264FrameSize = ImageSize - JesHeaderSize;
			pH264Image = pImgBuff + JesHeaderSize + 1;

		/******************************** CODE ADDED BY SEYEON TECH END ***********************************/

		if( nRetCode < 0 )
		{
			printf("GetCgiStream Error=%d\n", nRetCode);
		}
		else
#ifdef DECODER_ON
		{
			sprintf(tmp_img_file, "img%d_3_%02d.raw",__LINE__, idx);
			
			if(IsJesPacketVideo((pjpeg_usr_t)pImgBuff) )
			{
				current_codec = GetVideoCodecTypeOfJesPacket((pjpeg_usr_t)pImgBuff);
				
				if(current_codec == setuped_codec) 
				{
					if(current_codec == JES_VIDEO_CODEC_JPEG)	
						Decoding(&ffm4l, (pjpeg_usr_t)pImgBuff, tmp_img_file);				
					else if(current_codec == JES_VIDEO_CODEC_H264)
					{
						if(IsJesPacketVideoH264IFrame((pjpeg_usr_t)pImgBuff) ) 
						{
							if(First_IFrame_Flag == 0)
								First_IFrame_Flag = 1;
						}
						else if(First_IFrame_Flag == 0)
							continue;
					
						Decoding(&ffm4l, (pjpeg_usr_t)pImgBuff, tmp_img_file);
					}
						
				}
				else if(current_codec != setuped_codec) 
				{
					CodecChange(&ffm4l, current_codec);
									

				}
			}
		}	

#else
		{				
			if(IsJesPacketVideo((pjpeg_usr_t)pImgBuff) ) 
			{
		
				if (GetVideoCodecTypeOfJesPacket((pjpeg_usr_t)pImgBuff) == JES_VIDEO_CODEC_JPEG)
					sprintf(tmp_img_file, "img%d_3_%02d.jpg",__LINE__, idx);	
				else if(GetVideoCodecTypeOfJesPacket((pjpeg_usr_t)pImgBuff) == JES_VIDEO_CODEC_H264) 
				{
					if(IsJesPacketVideoH264IFrame((pjpeg_usr_t)pImgBuff) ) 
					{
						if(First_IFrame_Flag == 0)
							First_IFrame_Flag = 1;
					}
					else if(First_IFrame_Flag == 0)
						continue;
					
					// sprintf(tmp_img_file, "img%d_3_%02d.264",__LINE__, idx);	// original function call. saves one frame into one file
					// sprintf(tmp_img_file, "VIDEO.h264"); // new function call. saves many frames into one file	
					
					// tweaked by SungboKang //
					sprintf(tmp_img_file, "VIDEO%d.h264", tempSeparateH264FileNumber); // newer function call. saves many frames into numbered files	
					///////////////////////////
				}
				// SavePacket(pImgBuff, tmp_img_file, ImageSize); // original function call. saves one frame into one file WITH HEADER DATA
				SavePacket(pH264Image, tmp_img_file, H264FrameSize); // new function call. saves a frame without the JES headers
						
			}
		}
	
		// tweaked by SungboKang ////////////////////////////////////////////////////////////////
		// assume that a IP Camera sends 30 frames at any circumstances.
		// In here, it runs every 5 seconds(160 frames) to make a separate H264 file
		if((++frameCnt) == 160)
		{
			pthread_t threadFFmpeg; // declare thread object
			int thread_id;
			int thread_arg = tempSeparateH264FileNumber;

			// create and run a new thread
			thread_id = pthread_create(&threadFFmpeg, NULL, Ffmpeg_thread_function, (void *)&thread_arg);
			if(thread_id < 0) // when thread doesnt work properly
			{
				perror("thread create error\n");
				exit(-1);
			}

			tempSeparateH264FileNumber++;
			frameCnt = 0;
			First_IFrame_Flag = 0;

			if(tempSeparateH264FileNumber == 3) // makes 3 h264 files then quit looping
			{
				pthread_join(&threadFFmpeg, NULL);
				break;			
			}
			else
			{
				pthread_detach(threadFFmpeg); // detach a thread from the main thread in order to run and finish separately	
			}
		}
	
	//////////////////////////////////////////////////////////////////////////////////////////////
#endif
	}
	//=============================================================================

	//=============================================================================
	// Control Cgi Stream

	memset(pImgBuff, 0, MAX_PACK_SIZE);

	//=============================================================================
	// Set Stream as Normal mode ( Header + Image )
//	nRead = TestStCtrCgiV30Wp(VSMID, AppKey, DaemonId, "Normal", "0,1,2,3", PAUSE_TIME,TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);

	//=============================================================================
	// Set Stream as Header mode (Header Only)
//	nRead = FwStCtrCgiV30Wp(VSMID, AppKey, DaemonId, "Header", "0,1,2,3", PAUSE_TIME,TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);

	//=============================================================================
	// Add Camera 3,4 --> PortId (2,3)
//	nRead = TestStCtrCgiV30Wp(VSMID, AppKey, DaemonId, "Add", "2,3", PAUSE_TIME,TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);

	//=============================================================================
	// Remove Camera 2 --> PortId (1)
//	nRead = TestStCtrCgiV30Wp(VSMID, AppKey, DaemonId, "Remove", "1", PAUSE_TIME,TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);

	//=============================================================================
	// Set pause time 1000 msec
//	nRead = TestStCtrCgiV30Wp(VSMID, AppKey, DaemonId, "Set", NULL, 100,TARGET_IPADDR, TARGET_PORT, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);

	//=============================================================================
	// Get Cgi Stream
#if 0
	for(idx=0 ; idx < RECV_IMAGE_CNT ; idx++)
	{

		sprintf(tmp_img_file, "img%d_3_%02d.jpg",__LINE__, idx);
#ifdef WIN32
		tmp_img_filep=fopen(tmp_img_file, "wb");
#else
		tmp_img_filep=fopen(tmp_img_file, "wb");
#endif
		nRetCode = FwRcvCgiStream(StreamSock, pImgBuff, MAX_PACK_SIZE, &ImageSize, &ScanMode, &DaemonId);
		if( nRetCode < 0 )
		{
			printf("GetCgiStream Error=%d\n", nRetCode);
		}
		else
		{
//			printf("-------- Jpeg Header --------\n");
//			PrintJpegHeader(pImgBuff);
			printf("DaemondId  : 0x%08lx\n", DaemonId);
			printf("FwModId(0~): 0x%04x\n", GET_FW_STREAM_MOD_ID( nRetCode ));
			printf("PortId(0~3): 0x%04x\n", GET_FW_STREAM_PORT_ID( nRetCode ));
			fwrite(pImgBuff, sizeof(char), ImageSize, tmp_img_filep);
		}
		fclose(tmp_img_filep);
	}
	//=============================================================================
#endif

	//=============================================================================
	// Close Cgi Stream
	FwCloseCgiWp(StreamSock);
	//=============================================================================

//	nRead = TestCamGetCgiV30Wp(VSMID, TARGET_IPADDR, TARGET_PORT, 0, FW_USER_ID, FW_PASS_WD, pImgBuff, MAX_PACK_SIZE, wp_domain, wp_port);
//	if(nRead > 0)
//		printf("FwSysGetCgiWp=[%s]\n",pImgBuff);


	SOCK_CLEANUP();
	free(pImgBuff);
	
	return 0;
}
/*********************************************************************************/


static	long TestCamGetCgiV30Wp(int fwmodid, char *ip_addr, int port, int PortId, char *id, char *pwd, char *pRcvBuf, int BufLen, char *ip_addr_wp, int port_wp)
{
	char	query[MAX_QUERY];
	long	nRead;

	sprintf(query, "/asp-get/fwcamget.asp?FwModId=%d&PortId=%d&FwCgiVer=0x%04x", fwmodid, PortId, FWCGI_VERSION);
	nRead = FwAccessAspPageWp(query ,ip_addr, port, id, pwd, pRcvBuf, BufLen, ip_addr_wp, port_wp, CGI_TIMEOUT); 

	return nRead;
}


static	long TestStCtrCgiV30Wp(int fwmodid, int AppKey, int DaemonId, char *Action, char *pPortList, unsigned long PauseTime,char *ip_addr, int port, char *id, char *pwd, char *pRcvBuf, int BufLen, char *ip_addr_wp, int port_wp)
{
	char	query[MAX_QUERY];
	long	nRead;

	if(pPortList)
	{
		sprintf(query, "/cgi-bin/fwstctr.cgi?FwModId=%d&AppKey=0x%08x&DaemonId=%d&Action=%s&PortId=%s&FwCgiVer=0x%04x", 
						fwmodid, AppKey, DaemonId, Action, pPortList, FWCGI_VERSION);
	}
	else
	{
		sprintf(query, "/cgi-bin/fwstctr.cgi?FwModId=%d&AppKey=0x%08x&DaemonId=%d&Action=%s&PauseTime=%d&FwCgiVer=0x%04x", 
						fwmodid, AppKey, DaemonId, Action, (int)PauseTime, FWCGI_VERSION);
	}
	nRead = FwAccessGetCgiWp(query ,ip_addr, port, id, pwd, pRcvBuf, BufLen, ip_addr_wp, port_wp, CGI_TIMEOUT); 
	return nRead;
}

#ifdef DECODER_ON

static int CodecChange(Cffm4l* ffm4l, SytJesVideoCodecTypeEnum codec)
{
	ffm4l->DeleteFfm4lDecoder();
					
	if(codec == JES_VIDEO_CODEC_JPEG)
	{
		if (ffm4l->CreateFfm4lDecoder(CODEC_ID_MJPEG)!=FFM4L_COMMON_SUCCESS)
		{	
			printf("ffmpeg Create failed\n");
			return -1;
		}
		setuped_codec = JES_VIDEO_CODEC_JPEG;
		printf("codec change jpeg\n");
	}
	else if(codec == JES_VIDEO_CODEC_H264)
	{
		if (ffm4l->CreateFfm4lDecoder(CODEC_ID_H264)!=FFM4L_COMMON_SUCCESS)
		{	
			printf("ffmpeg Create failed\n");
			return -1;
		}
		setuped_codec = JES_VIDEO_CODEC_H264;
		printf("codec change h264\n");
	}
	return 0;
}

static int Decoding(Cffm4l* ffm4l, pjpeg_usr_t pImgBuff, char *filename)
{
	unsigned long width = 0;
	unsigned long height = 0;
	int				nRetCode;
	FILE*			tmp_img_filep;
	
	char* bitmapBuffer = NULL;
	bitmapBuffer = (char*)malloc(MAX_RGB_SIZE);
	if (!bitmapBuffer)
	{
		printf("memory allocation failed\n");
		free(pImgBuff);
		return -1;
	}
	tmp_img_filep=fopen(filename, "wb");

	nRetCode = ffm4l->DecodeTargetBufToGeneralBuf((BYTE*)::GetPayloadOfJesPacket(pImgBuff)	,
					::GetPayloadSizeOfJesPacket(pImgBuff),
					&width,
					&height,
					0,
					(BYTE*)bitmapBuffer);
	if (nRetCode!=FFM4L_COMMON_SUCCESS)
		printf("ffmpeg.Decode failed\n");
	else
		printf("ffmpeg.Decode success[W:%d H:%d]\n", width, height);	

	fwrite(bitmapBuffer, sizeof(char), width * height * 3, tmp_img_filep); // Saved BGR 24bit Pattern
	fclose(tmp_img_filep);	

	free(bitmapBuffer);
	return 0;

}
#endif //DECODER_ON

static int SavePacket(char* pImgBuff, char *filename, int ImageSize)
{
	FILE*			tmp_img_filep;
	
	tmp_img_filep=fopen(filename, "ab");
	fwrite(pImgBuff, sizeof(char), ImageSize, tmp_img_filep);
	fclose(tmp_img_filep);

	return 0;
}

// tweaked by SungboKang /////////
void* Ffmpeg_thread_function(void* arg)
{
	int tempSeparateH264FileNumber = *((int *)arg);
	char commandFFmpeg[LENGTH_FFMPEG_COMMAND];
	
	sprintf(commandFFmpeg, "ffmpeg -r 30 -i VIDEO%d.h264 -vcodec copy VIDEO%d.mp4 &", tempSeparateH264FileNumber, tempSeparateH264FileNumber);
	system(commandFFmpeg); // execute ffmpeg command
}
//////////////////////////////////