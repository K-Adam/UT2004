/*=============================================================================
	ALAudioCapture.cpp: Shared thread safe audio capture device.
	Copyright 2003 Epic Games, Inc. All Rights Reserved.

	Revision history:
	* Created by Daniel Vogel.
=============================================================================*/

#include "ALAudioPrivate.h"

// Direct form II transposed IIR filter, den[0] is 1. May filter in place.
static void FilterMem( const FLOAT* X, const FLOAT* Num, const FLOAT* Den, FLOAT* Y, INT N, INT Ord, FLOAT* Mem )
{
	for( INT i=0; i<N; i++ )
	{
		FLOAT Xi = X[i];
		FLOAT Yi = Num[0]*Xi + Mem[0];
		for( INT j=0; j<Ord-1; j++ )
			Mem[j] = Mem[j+1] + Num[j+1]*Xi - Den[j+1]*Yi;
		Mem[Ord-1] = Num[Ord]*Xi - Den[Ord]*Yi;
		Y[i] = Yi;
	}
}

//
//	FALAudioCaptureDevice::FALAudioCaptureDevice.
//
FALAudioCaptureDevice::FALAudioCaptureDevice()
{
	ALCaptureDevice         = NULL;
	ALCaptureBuffer         = NULL;

	RecordCount			= 0;

	appMemzero(FilterMemory,sizeof(FLOAT)*(FILTER_8K_SIZE-1));
}

//
//	FALAudioCaptureDevice::~FALAudioCaptureDevice.
//
FALAudioCaptureDevice::~FALAudioCaptureDevice()
{
	if (ALCaptureDevice != NULL)
	{
		UALAudioSubsystem::alcCaptureStop(ALCaptureDevice);
		UALAudioSubsystem::alcCaptureCloseDevice(ALCaptureDevice);
		ALCaptureDevice = NULL;
	}

	if (ALCaptureBuffer)
	{
		delete[] ALCaptureBuffer;
		ALCaptureBuffer = NULL;
	}
}

//
//	FALAudioCaptureDevice::Init - will only be called from within ALAudio.
//
UBOOL FALAudioCaptureDevice::Init()
{
	// Favor ALC_EXT_capture...
	if( UALAudioSubsystem::SUPPORTS_ALC_EXT_capture )
	{
		const ALenum fmt = AL_FORMAT_MONO16;
		const ALsizei fmtsize = 2;
		const ALsizei freq = 16000;
		const ALsizei seconds = 5;  // seconds of audio to buffer.
		const ALsizei samples = freq * seconds;

		ALCaptureDevice = UALAudioSubsystem::alcCaptureOpenDevice(NULL, freq, fmt, samples);
		if (ALCaptureDevice != NULL)
		{
			alc_capture_samples = UALAudioSubsystem::alcGetEnumValue(ALCaptureDevice, (ALubyte *) "ALC_CAPTURE_SAMPLES");
			ALCaptureBuffer = new BYTE[samples * fmtsize];
			debugf(TEXT("ALAudio: Using ALC_EXT_capture to record audio."));
			return 1;
		}
	}

	debugf(TEXT("ALAudio: Couldn't find a means to record audio."));
	return 0;
}

//
//	FALAudioCaptureDevice::Record.
//
void FALAudioCaptureDevice::Record()
{
	FScopeCriticalSection Lock( &CriticalSection );

	if( RecordCount++ >= 0 )
	{
		if( ALCaptureDevice )
			UALAudioSubsystem::alcCaptureStart(ALCaptureDevice);
	}
}

//
//	FALAudioCaptureDevice::Stop.
//
void FALAudioCaptureDevice::Stop()
{
	FScopeCriticalSection Lock( &CriticalSection );

	if( --RecordCount <= 0 )
	{
		if( ALCaptureDevice )
			UALAudioSubsystem::alcCaptureStop(ALCaptureDevice);
	}
}

//
//	FALAudioCaptureDevice::GetCurrentOffset.
//
DWORD FALAudioCaptureDevice::GetCurrentOffset()
{
	return 0;  // ALC_EXT_capture doesn't expose a ring buffer...
}

//
//	FALAudioCaptureDevice::GetSamplesPerSecond.
//
DWORD FALAudioCaptureDevice::GetSamplesPerSecond()
{
	return 16000;
}

//
//	FALAudioCaptureDevice::GetAvailableSamples.
//
DWORD FALAudioCaptureDevice::GetAvailableSamples( DWORD Offset, EAudioCaptureType Type )
{
	FScopeCriticalSection Lock( &CriticalSection );

	if( ALCaptureDevice )
	{
		// A failed query, as on the web with a refused microphone, leaves the result unwritten.
		ALint samples = 0;
		UALAudioSubsystem::alcGetIntegerv(ALCaptureDevice, alc_capture_samples, sizeof (ALint), &samples);

		switch( Type )
		{
		case CAPTURE_8KHZ_FLOAT:
			return samples / 2; // frequency conversion.
			break;
		case CAPTURE_16KHZ_FLOAT:
		case CAPTURE_16KHZ_SHORT:
			return samples;
			break;
		default:
			return 0;
			break;
		}
	}

	return 0;
}

//
//	FALAudioCaptureDevice::GetSampleData.
//
FLOAT FALAudioCaptureDevice::GetSampleData( DWORD& Offset, void* Pointer, DWORD& Samples, EAudioCaptureType Type )
{
	FScopeCriticalSection Lock( &CriticalSection );
	SHORT*	CaptureData[2]		= {NULL, NULL};
	DWORD	CaptureLength[2]	= {0,0};
	FLOAT	MaximumGain			= 0.f;
	DWORD	SamplesRead			= 0;

	const FLOAT INV_MAX_SHORT	= 1.f / 32768.f;

	switch( Type )
	{
	case CAPTURE_8KHZ_FLOAT:
		Samples *= 2; // frequency conversion.
		break;
	case CAPTURE_16KHZ_SHORT:
	case CAPTURE_16KHZ_FLOAT:
	default:
		break;
	}

	if (ALCaptureDevice)
	{
		Offset  = 0;  // ALC_EXT_capture handles this. No ring buffer.
		UALAudioSubsystem::alcGetError(ALCaptureDevice);
		UALAudioSubsystem::alcCaptureSamples(ALCaptureDevice, ALCaptureBuffer, Samples);
		if( UALAudioSubsystem::alcGetError(ALCaptureDevice) )
		{
			Samples = 0;
			return 0.f;
		}
		CaptureData[0] = (SHORT *) ALCaptureBuffer;
		CaptureLength[0] = Samples;
	}

	// This conversion could be dropped by opening the capture device with the requested Type,
	// since AL converts anyway. The max gain would still need to be determined.
 
	switch( Type )
	{
	case CAPTURE_8KHZ_FLOAT:
		{
			INT		RawSamples	= CaptureLength[0] + CaptureLength[1];
			FLOAT*	Dest		= (FLOAT*) Pointer;
			FLOAT*	FloatSrc	= new FLOAT[RawSamples];
			FLOAT*	Dummy		= FloatSrc;

			// Convert to float and calculate maximum gain.
			for( INT n=0; n<2; n++ ) 
			{
				SHORT* Src = CaptureData[n];
				for( DWORD i=0; i<CaptureLength[n]; i++ )
				{
					*Dummy			= *(Src++);
					MaximumGain		= Max( MaximumGain, *Dummy * INV_MAX_SHORT );
					Dummy++;
				}
			}

			// Filter 16kHz input.
			FilterMem( FloatSrc,FilterNumerator, FilterDenominator, FloatSrc, RawSamples, FILTER_8K_SIZE-1, FilterMemory );

			// Frequency conversion.
			SamplesRead = RawSamples / 2;
			for( DWORD i=0; i<SamplesRead; i++ )
				Dest[i] = FloatSrc[i*2];

			// Delete temporary 16kHz float buffer.
			delete [] FloatSrc;
		}
		break;
	case CAPTURE_16KHZ_FLOAT:
		{
			FLOAT* Dest = (FLOAT*) Pointer;

			for( INT n=0; n<2; n++ ) 
			{
				SHORT*	Src			= CaptureData[n];
				SamplesRead		   += CaptureLength[n];

				for( DWORD i=0; i<CaptureLength[n]; i++ )
				{
					*(Dest++)		= *Src;
					MaximumGain		= Max( MaximumGain, *Src * INV_MAX_SHORT );
					Src++;
				}
			}
		}
		break;
	case CAPTURE_16KHZ_SHORT:
		{
			SHORT* Dest = (SHORT*) Pointer;
			for( INT n=0; n<2; n++ ) 
			{
				SHORT*	Src			= CaptureData[n];
				SamplesRead		   += CaptureLength[n];

				for( DWORD i=0; i<CaptureLength[n]; i++ )
				{
					*(Dest++)		= *Src;
					MaximumGain		= Max( MaximumGain, *Src * INV_MAX_SHORT );
					Src++;
				}
			}
		}
		break;
	default:
		break;
	}

	Samples = SamplesRead;

	// Return maximum gain in range 0..1
	return MaximumGain;
}

FLOAT FALAudioCaptureDevice::FilterNumerator[FILTER_8K_SIZE] = 
{
	0.0413379526609213f,0.0436872819013653f,0.0676740625504942f,-0.0172296616128663f,-0.0711813239119114f,-0.128576623176006f,
	-0.0711813239119116f,-0.0172296616128662f,0.0676740625504941f,0.0436872819013654f,0.0413379526609213f
};

FLOAT FALAudioCaptureDevice::FilterDenominator[FILTER_8K_SIZE] = 
{
	1.f,-2.9271450525137f,5.66048987056755f,-8.47759931415551f,9.43116686411878f,-8.59478639304787f,6.23976253450014f,
	-3.48380046532984f,1.53076294451108f,-0.435963555011292f,0.070863358848392f
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

