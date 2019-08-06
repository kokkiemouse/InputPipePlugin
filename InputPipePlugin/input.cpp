//----------------------------------------------------------------------------------
//		�T���v��AVI(vfw�o�R)���̓v���O�C��  for AviUtl ver0.98�ȍ~
//----------------------------------------------------------------------------------
#include "pch.h"
#include <memory>
#include <unordered_map>
#include	<windows.h>
#include	<vfw.h>
#pragma comment(lib, "Vfw32.lib")
#include	"input.h"

#include "..\Share\Logger.h"
#include "..\Share\IPC.h"
#include "..\Share\Common.h"
#include "..\Share\CodeConvert.h"

extern HMODULE g_hModule;
HMODULE	g_hWinputDll = NULL;
INPUT_PLUGIN_TABLE* g_winputPluginTable = nullptr;

BindProcess	g_bindProcess;
LPCWSTR		kPipeName = LR"(\\.\pipe\InputPipePlugin)";
NamedPipe	g_namedPipe;

std::vector<BYTE> g_lastInfoGetData;

struct FrameAudioVideoBufferSize
{
	int OneFrameBufferSize;
	int PerAudioSampleBufferSize;
};

std::unordered_map<INPUT_HANDLE, FrameAudioVideoBufferSize> g_mapFrameBufferSize;

// for Logger
std::string	LogFileName()
{
	return (GetExeDirectory() / L"InputPipePlugin.log").string();
}


//#define NO_REMOTE

//---------------------------------------------------------------------
//		���̓v���O�C���\���̒�`
//---------------------------------------------------------------------
INPUT_PLUGIN_TABLE input_plugin_table = {
	INPUT_PLUGIN_FLAG_VIDEO|INPUT_PLUGIN_FLAG_AUDIO,	//	�t���O
														//	INPUT_PLUGIN_FLAG_VIDEO	: �摜���T�|�[�g����
														//	INPUT_PLUGIN_FLAG_AUDIO	: �������T�|�[�g����
	"InputPipePlugin",									//	�v���O�C���̖��O
	"any files (*.*)\0*.*\0",							//	���̓t�@�C���t�B���^
	"Plugin to bypass lwinput.aui By amate version " PLUGIN_VERSION,		//	�v���O�C���̏��
	func_init,											//	DLL�J�n���ɌĂ΂��֐��ւ̃|�C���^ (NULL�Ȃ�Ă΂�܂���)
	func_exit,											//	DLL�I�����ɌĂ΂��֐��ւ̃|�C���^ (NULL�Ȃ�Ă΂�܂���)
	func_open,											//	���̓t�@�C�����I�[�v������֐��ւ̃|�C���^
	func_close,											//	���̓t�@�C�����N���[�Y����֐��ւ̃|�C���^
	func_info_get,										//	���̓t�@�C���̏����擾����֐��ւ̃|�C���^
	func_read_video,									//	�摜�f�[�^��ǂݍ��ފ֐��ւ̃|�C���^
	func_read_audio,									//	�����f�[�^��ǂݍ��ފ֐��ւ̃|�C���^
	func_is_keyframe,									//	�L�[�t���[�������ׂ�֐��ւ̃|�C���^ (NULL�Ȃ�S�ăL�[�t���[��)
	func_config,										//	���͐ݒ�̃_�C�A���O��v�����ꂽ���ɌĂ΂��֐��ւ̃|�C���^ (NULL�Ȃ�Ă΂�܂���)
};


//---------------------------------------------------------------------
//		���̓v���O�C���\���̂̃|�C���^��n���֐�
//---------------------------------------------------------------------
EXTERN_C INPUT_PLUGIN_TABLE __declspec(dllexport) * __stdcall GetInputPluginTable( void )
{
	g_hWinputDll = ::LoadLibrary((GetExeDirectory() / L"lwinput.aui").c_str());
	assert(g_hWinputDll);
	if (g_hWinputDll == NULL) {
		WARN_LOG << L"lwinput.aui not found";
		return nullptr;
	}

	//using GetInputPluginTableFunc = INPUT_PLUGIN_TABLE * (__stdcall*)(void);
	//GetInputPluginTableFunc funcGetTable = (GetInputPluginTableFunc)::GetProcAddress(g_hWinputDll, "GetInputPluginTable");

	//g_winputPluginTable = funcGetTable();
	//input_plugin_table.filefilter = g_winputPluginTable->filefilter;

	::FreeLibrary(g_hWinputDll);
	g_hWinputDll = NULL;

	return &input_plugin_table;
}


//---------------------------------------------------------------------
//		�t�@�C���n���h���\����
//---------------------------------------------------------------------
typedef struct {
	int				flag;
	PAVIFILE		pfile;
	PAVISTREAM		pvideo,paudio;
	AVIFILEINFO		fileinfo;
	AVISTREAMINFO	videoinfo,audioinfo;
	void			*videoformat;
	LONG			videoformatsize;
	void			*audioformat;
	LONG			audioformatsize;
} FILE_HANDLE;
#define FILE_HANDLE_FLAG_VIDEO		1
#define FILE_HANDLE_FLAG_AUDIO		2


//---------------------------------------------------------------------
//		������
//---------------------------------------------------------------------
BOOL func_init( void )
{
	INFO_LOG << L"func_init";

	std::wstring pipeName = std::wstring(kPipeName) + std::to_wstring((uint64_t)g_hModule);
	bool ret = g_namedPipe.CreateNamedPipe(pipeName);
	// INFO_LOG << L"CreateNamedPipe: " << pipeName << L" ret: " << ret;

	auto InputPipeMainPath = GetExeDirectory() / L"InputPipeMain.exe";

	bool startRet = g_bindProcess.StartProcess(InputPipeMainPath.native(), pipeName);
	// INFO_LOG << L"StartProcess: " << L" ret: " << startRet;

	g_namedPipe.ConnectNamedPipe();

	//BOOL b = g_winputPluginTable->func_init();
	//return b;

	//AVIFileInit();
	return TRUE;
}


//---------------------------------------------------------------------
//		�I��
//---------------------------------------------------------------------
BOOL func_exit( void )
{
	INFO_LOG << L"func_exit";

	g_namedPipe.Disconnect();
	g_bindProcess.StopProcess();

	//BOOL b = g_winputPluginTable->func_exit();
	//return b;

	//AVIFileExit();
	return TRUE;
}


//---------------------------------------------------------------------
//		�t�@�C���I�[�v��
//---------------------------------------------------------------------
INPUT_HANDLE func_open( LPSTR file )
{
	INFO_LOG << L"func_open: " << CodeConvert::UTF16fromShiftJIS(file);

#ifndef NO_REMOTE
	size_t paramSize = strnlen_s(file, MAX_PATH) + 1;
	std::shared_ptr<ToWinputData> toData((ToWinputData*)new BYTE[kToWindDataHeaderSize + paramSize], 
		[](ToWinputData* p) {
			delete[] (BYTE*)p;
	});
	toData->header.callFunc = CallFunc::kOpen;
	toData->header.paramSize = paramSize;
	memcpy_s(toData->paramData, paramSize, file, paramSize);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	INPUT_HANDLE ih2 = *(INPUT_HANDLE*)readBody.data();
	INFO_LOG << ih2;
	return ih2;
#else
	INPUT_HANDLE ih = g_winputPluginTable->func_open(file);
	return ih;
#endif
#if 0
	FILE_HANDLE		*fp;
	int				i;
	PAVISTREAM		pas;
	AVISTREAMINFO	asi;

	fp = (FILE_HANDLE *)GlobalAlloc(GMEM_FIXED,sizeof(FILE_HANDLE));
	if( fp == NULL ) return NULL;
	ZeroMemory(fp,sizeof(FILE_HANDLE));

	if( AVIFileOpenA(&fp->pfile,file,OF_READ,NULL) != 0 ) {
		GlobalFree(fp);
		return NULL;
	}

	if( AVIFileInfo(fp->pfile,&fp->fileinfo,sizeof(fp->fileinfo)) == 0 ) {
		for(i=0;i<fp->fileinfo.dwStreams;i++) {
			if( AVIFileGetStream(fp->pfile,&pas,0,i) == 0 ) {
				AVIStreamInfo(pas,&asi,sizeof(asi));
				if( asi.fccType == streamtypeVIDEO ) {
					//	�r�f�I�X�g���[���̐ݒ�
					fp->pvideo = pas;
					fp->videoinfo = asi;
					AVIStreamFormatSize(fp->pvideo,0,&fp->videoformatsize);
					fp->videoformat = (FILE_HANDLE *)GlobalAlloc(GMEM_FIXED,fp->videoformatsize);
					if( fp->videoformat ) {
						AVIStreamReadFormat(fp->pvideo,0,fp->videoformat,&fp->videoformatsize);
						fp->flag |= FILE_HANDLE_FLAG_VIDEO;
					} else {
						AVIStreamRelease(pas);
					}
				} else if( asi.fccType == streamtypeAUDIO ) {
					//	�I�[�f�B�I�X�g���[���̐ݒ�
					fp->paudio = pas;
					fp->audioinfo = asi;
					AVIStreamFormatSize(fp->paudio,0,&fp->audioformatsize);
					fp->audioformat = (FILE_HANDLE *)GlobalAlloc(GMEM_FIXED,fp->audioformatsize);
					if( fp->videoformat ) {
						AVIStreamReadFormat(fp->paudio,0,fp->audioformat,&fp->audioformatsize);
						fp->flag |= FILE_HANDLE_FLAG_AUDIO;
					} else {
						AVIStreamRelease(pas);
					}
				} else {
					AVIStreamRelease(pas);
				}
			}
		}
	}

	return fp;
#endif
}


//---------------------------------------------------------------------
//		�t�@�C���N���[�Y
//---------------------------------------------------------------------
BOOL func_close( INPUT_HANDLE ih )
{
	INFO_LOG << L"func_close: " << ih;
#ifndef NO_REMOTE
	StandardParamPack spp = { ih };
	auto toData = GenerateToInputData(CallFunc::kClose, spp);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	auto retData = ParseFromInputData<BOOL>(readBody);

	g_mapFrameBufferSize.erase(ih);

	return retData.first;
#else
	BOOL b = g_winputPluginTable->func_close(ih);
	return b;
#endif
#if 0
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	if( fp ) {
		if( fp->flag&FILE_HANDLE_FLAG_AUDIO ) {
			AVIStreamRelease(fp->paudio);
			GlobalFree(fp->audioformat);
		}
		if( fp->flag&FILE_HANDLE_FLAG_VIDEO ) {
			AVIStreamRelease(fp->pvideo);
			GlobalFree(fp->videoformat);
		}
		AVIFileRelease(fp->pfile);
		GlobalFree(fp);
	}

	return TRUE;
#endif
}


//---------------------------------------------------------------------
//		�t�@�C���̏��
//---------------------------------------------------------------------
BOOL func_info_get( INPUT_HANDLE ih,INPUT_INFO *iip )
{
	INFO_LOG << L"func_info_get";
#ifndef NO_REMOTE
	StandardParamPack spp = { ih };
	auto toData = GenerateToInputData(CallFunc::kInfoGet, spp);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	auto retData = ParseFromInputData<BOOL>(readBody);
	*iip = *(INPUT_INFO*)retData.second;
	iip->format = (BITMAPINFOHEADER*)(retData.second + sizeof(INPUT_INFO));
	iip->audio_format = (WAVEFORMATEX*)(retData.second + sizeof(INPUT_INFO) + iip->format_size);
	g_lastInfoGetData = std::move(readBody);

	int OneFrameBufferSize = iip->format->biWidth * iip->format->biHeight * (iip->format->biBitCount / 8) + kVideoBufferSurplusBytes;
	int PerAudioSampleBufferSize = iip->audio_format->nChannels * (iip->audio_format->wBitsPerSample / 8);
	g_mapFrameBufferSize.emplace(ih, FrameAudioVideoBufferSize{ OneFrameBufferSize , PerAudioSampleBufferSize });

	INFO_LOG << L"OneFrameBufferSize: " << OneFrameBufferSize << L" PerAudioSampleBufferSize: " << PerAudioSampleBufferSize;
	return retData.first;
#else
	BOOL b = g_winputPluginTable->func_info_get(ih, iip);
	return b;
#endif
#if 0
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	iip->flag = 0;

	if( fp->flag&FILE_HANDLE_FLAG_VIDEO ) {
		iip->flag |= INPUT_INFO_FLAG_VIDEO;
		iip->rate = fp->videoinfo.dwRate;
		iip->scale = fp->videoinfo.dwScale;
		iip->n = fp->videoinfo.dwLength;
		iip->format = (BITMAPINFOHEADER *)fp->videoformat;
		iip->format_size = fp->videoformatsize;
		iip->handler = fp->videoinfo.fccHandler;
	}

	if( fp->flag&FILE_HANDLE_FLAG_AUDIO ) {
		iip->flag |= INPUT_INFO_FLAG_AUDIO;
		iip->audio_n = fp->audioinfo.dwLength;
		iip->audio_format = (WAVEFORMATEX *)fp->audioformat;
		iip->audio_format_size = fp->audioformatsize;
	}

	return TRUE;
#endif
}


//---------------------------------------------------------------------
//		�摜�ǂݍ���
//---------------------------------------------------------------------
int func_read_video( INPUT_HANDLE ih,int frame,void *buf )
{
	//INFO_LOG << L"func_read_video" << L" frame: " << frame;
#ifndef NO_REMOTE
	const int OneFrameBufferSize = g_mapFrameBufferSize[ih].OneFrameBufferSize;

	StandardParamPack spp = { ih, frame };
	spp.perBufferSize = OneFrameBufferSize;
	auto toData = GenerateToInputData(CallFunc::kReadVideo, spp);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	int readBytes = 0;
	int nRet = g_namedPipe.Read((BYTE*)&readBytes, sizeof(readBytes));
	assert(nRet == sizeof(readBytes));
	assert((readBytes + sizeof(int)) == fromData->returnSize);
	nRet = g_namedPipe.Read((BYTE*)buf, readBytes);
	assert(nRet == readBytes);
	return readBytes;
#if 0
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	auto retData = ParseFromInputData<int>(readBody);

	::memcpy_s(buf, OneFrameBufferSize, retData.second, OneFrameBufferSize);
	return retData.first;
#endif

#else
	int n = g_winputPluginTable->func_read_video(ih, frame, buf);
	return n;
#endif

#if 0
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;
	LONG		videosize;
	LONG		size;

	AVIStreamRead(fp->pvideo,frame,1,NULL,NULL,&videosize,NULL);
	if( AVIStreamRead(fp->pvideo,frame,1,buf,videosize,&size,NULL) ) return 0;
	return size;
#endif
}


//---------------------------------------------------------------------
//		�����ǂݍ���
//---------------------------------------------------------------------
int func_read_audio( INPUT_HANDLE ih,int start,int length,void *buf )
{
	//INFO_LOG << L"func_read_audio: " << ih << L" start: " << start << L" length: " << length;

#ifndef NO_REMOTE
	const int PerAudioSampleBufferSize = g_mapFrameBufferSize[ih].PerAudioSampleBufferSize;

	StandardParamPack spp = { ih, start, length, PerAudioSampleBufferSize };
	auto toData = GenerateToInputData(CallFunc::kReadAudio, spp);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	int readSample = 0;
	int nRet = g_namedPipe.Read((BYTE*)&readSample, sizeof(readSample));
	assert(nRet == sizeof(readSample));
	const int audioBufferSize = fromData->returnSize - sizeof(int);
	nRet = g_namedPipe.Read((BYTE*)buf, audioBufferSize);
	assert(nRet == audioBufferSize);
	return readSample;
#if 0
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	auto retData = ParseFromInputData<int>(readBody);
	const int requestReadBytes = PerAudioSampleBufferSize * length;
	::memcpy_s(buf, requestReadBytes, retData.second, requestReadBytes);

	return retData.first;
#endif
#else

	int n = g_winputPluginTable->func_read_audio(ih, start, length, buf);
	return n;
#endif
#if 0
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;
	LONG		size;
	int			samplesize;

	samplesize = ((WAVEFORMATEX *)fp->audioformat)->nBlockAlign;
	if( AVIStreamRead(fp->paudio,start,length,buf,samplesize*length,NULL,&size) ) return 0;
	return size;
#endif
}


//---------------------------------------------------------------------
//		�L�[�t���[�����
//---------------------------------------------------------------------
BOOL func_is_keyframe( INPUT_HANDLE ih,int frame )
{
	// INFO_LOG << L"func_is_keyframe" << L" frame:" << frame;

#ifndef NO_REMOTE
	StandardParamPack spp = { ih, frame };
	auto toData = GenerateToInputData(CallFunc::kIsKeyframe, spp);
	g_namedPipe.Write((const BYTE*)toData.get(), ToWinputDataTotalSize(*toData));

	std::vector<BYTE> headerData = g_namedPipe.Read(kFromWinputDataHeaderSize);
	FromWinputData* fromData = (FromWinputData*)headerData.data();
	std::vector<BYTE> readBody = g_namedPipe.Read(fromData->returnSize);
	auto retData = ParseFromInputData<BOOL>(readBody);	
	return retData.first;

#else
	BOOL b = g_winputPluginTable->func_is_keyframe(ih, frame);
	return b;
#endif
#if 0
	FILE_HANDLE	*fp = (FILE_HANDLE *)ih;

	return AVIStreamIsKeyFrame(fp->pvideo,frame);
#endif
}


//---------------------------------------------------------------------
//		�ݒ�_�C�A���O
//---------------------------------------------------------------------
BOOL func_config( HWND hwnd, HINSTANCE dll_hinst )
{
	// INFO_LOG << L"func_config";
#ifndef NO_REMOTE
	MessageBox(hwnd, L"�������ł�...", L"", MB_OK);
	return TRUE;

#else
	BOOL b = g_winputPluginTable->func_config(hwnd, dll_hinst);
	return b;
#endif
#if 0
	MessageBoxA(hwnd,"�T���v���_�C�A���O","���͐ݒ�",MB_OK);

	//	DLL���J������Ă��ݒ肪�c��悤�ɕۑ����Ă����Ă��������B

	return TRUE;
#endif
}

