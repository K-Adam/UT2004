/*=============================================================================
	ALAudioVoice.h: Voice encoding/ decoding module.
	Copyright 2003 Epic Games, Inc. All Rights Reserved.

	Revision history:
	* Created by Daniel Vogel.
=============================================================================*/

#ifndef _INC_ALAUDIOVOICE
#define _INC_ALAUDIOVOICE

/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

// Speex is built with default packing, the engine with 4-byte packing on MSVC.
#pragma pack(push,8)
#include <speex/speex.h>
#include <speex/speex_preprocess.h>
#pragma pack(pop)

/*------------------------------------------------------------------------------------
	FALVoiceModule.
------------------------------------------------------------------------------------*/

class FALVoiceModule
{
public:
	FALVoiceModule( class UALAudioSubsystem* InAudioSubsystem );
	~FALVoiceModule() noexcept(false);

	void SetEncoder( EVoiceCodec InVoiceCodec );

	INT Encode();
	void ResetCapture();
	UBOOL Decode( FVoiceInfo* VoiceInfo );

	void NoteDestroy( AActor* Actor );

protected:
	UALAudioSubsystem*			AudioSubsystem;

	DWORD						CaptureOffset;
	UBOOL						CapturingVoice,
								VADActive;

	TMap<INT, APawn*>			PlayerIDToPawn;

	void*						SpeexEncoder; 
	SpeexPreprocessState*		SpeexPreprocessor;
	TMap<USound*,void*>			SpeexDecoders;
	SpeexBits*					SpeexData;
	EVoiceCodec					VoiceCodec;
};

#endif

