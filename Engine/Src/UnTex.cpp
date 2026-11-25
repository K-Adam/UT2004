/*=============================================================================
	UnTex.cpp: Unreal texture loading/saving/processing functions.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney

	DXT3/5 -> DXT1 conversion by PowerVR Technologies.

	Copyright 2001, PowerVR Technologies. 
	PowerVR Technologies is a division of Imagination Technologies Limited. 
	All Rights Reserved.
=============================================================================*/

#include "EnginePrivate.h"
#include <squish.h>

/*-----------------------------------------------------------------------------
	FColor functions.
-----------------------------------------------------------------------------*/

//
// Convert byte hue-saturation-brightness to floating point red-green-blue.
//
FPlane ENGINE_API FGetHSV( BYTE H, BYTE S, BYTE V )
{
    FLOAT Brightness = V * 1.4f / 255.f;
	Brightness *= 0.7f/(0.01f + appSqrt(Brightness));
	Brightness  = Clamp(Brightness,0.f,1.f);
	FVector Hue = (H<86) ? FVector((85-H)/85.f,(H-0)/85.f,0) : (H<171) ? FVector(0,(170-H)/85.f,(H-85)/85.f) : FVector((H-170)/85.f,0,(255-H)/84.f);
	return FPlane( (Hue + S/255.f * (FVector(1,1,1) - Hue)) * Brightness, 1 );
}

/*-----------------------------------------------------------------------------
	Texture locking and unlocking.
-----------------------------------------------------------------------------*/

//
// Update texture.
//
void UTexture::Update( FTime CurrentTime )
{
	guard(UTexture::Update);
	if( CurrentTime > GetLastUpdateTime() )
	{
		if( bRealtime )
			bRealtimeChanged = 1;
		Tick( CurrentTime - GetLastUpdateTime());
		SetLastUpdateTime(CurrentTime);
	}
	else
		SetLastUpdateTime(CurrentTime);
	unguard;
}

INT UTexture::DefaultLOD()
{
    // gam ---
    INT LOD;

    if( __Client == NULL )
	    LOD = NormalLOD;
    else
    {
		// Highest texture detail.
	    LOD = NormalLOD - 4;

		// Apply level specific offset.
		ALevelInfo* LevelInfo = GIsEditor ? NULL : __Client->Viewports.Num() && __Client->Viewports(0) && __Client->Viewports(0)->Actor ? __Client->Viewports(0)->Actor->Level : NULL;
		if( LevelInfo && (LODSet == LODSET_World) )
			LOD += Abs( LevelInfo->LevelTextureLODBias  );
	}

    LOD = Clamp( LOD, (INT)MinLOD, (INT)MaxLOD );

    return (LOD);
    // --- gam
}

/*---------------------------------------------------------------------------------------
	Texture animation.
---------------------------------------------------------------------------------------*/

//
// Continuous time update.
//
// To provide your own continuous-time update, override this and do not call this
// base class function in your child class.
//
void UTexture::Tick( FLOAT DeltaSeconds )
{
	guard(UTexture::Tick);

	Prime(); 

	// Update.
	if( MaxFrameRate == 0.f )
	{
		// Constant update.
		ConstantTimeTick();
	}
	else
	{
		// Catch up.
		FLOAT MinTime  = 1.f/Clamp(MaxFrameRate,0.1f,100.f);
		FLOAT MaxTime  = 1.f/Clamp(MinFrameRate,0.1f,100.f);
		Accumulator   += DeltaSeconds;
		if( Accumulator < MinTime )
		{
			// Skip update.
		}
		else if( Accumulator < MaxTime )
		{
			// Normal update.
			ConstantTimeTick();
			Accumulator = 0;
		}
		else
		{
			ConstantTimeTick();
			Accumulator -= MaxTime;
			if( Accumulator > MaxTime )
				Accumulator = MaxTime;
		}
	}
	unguardobj;
}

//
// Priming (warming-up) a texture.
//
void UTexture::Prime()
{
	// Regular priming.
	while( PrimeCurrent < PrimeCount )
	{
		PrimeCurrent++;
		ConstantTimeTick();
	}
}

//
// Discrete time update.
//
// To provide your own discrete-time update, override this and do not call this
// base class function in your child class.
//
void UTexture::ConstantTimeTick()
{
	guard(UTexture::ConstantTimeTick);

	// Simple cyclic animation.
	if( !AnimCur ) AnimCur = this;
	AnimCur = AnimCur->AnimNext;
	if( !AnimCur ) AnimCur = this;

	unguardobj;
}

/*---------------------------------------------------------------------------------------
	UTexture object implementation.
---------------------------------------------------------------------------------------*/

UTexture::UTexture()
{
	guard(UTexture::UTexture);

	RenderInterface = new FStaticTexture(this);
	// Material duplication sets bit 8 of GUglyHackFlags.
	if( !(GUglyHackFlags & 8) )
	{
		Format			= TEXF_P8;
		Palette			= NULL;
		UBits			= 0;
		VBits			= 0;
		USize			= 0;
		VSize			= 0;
		MipZero			= FColor(64,128,64,0);
		MaxColor		= FColor(255,255,255,255);
	}
	LastUpdateTime  = GCurrentTime;
	OLDSpecular		= 1.f;

	unguardobj;
}
UClient* UTexture::__Client=NULL;
	
static void SerializeMips( UTexture* Texture, FArchive& Ar, TArray<FMipmap>& Mips )
{
	guard(SerializeMips);
	Mips.CountBytes( Ar );
	if( Ar.IsLoading() )
	{
		// Load array.
		INT NewNum;
		Ar << AR_INDEX(NewNum);
		Mips.Empty( NewNum );
		INT LOD = Texture->DefaultLOD();
		for( INT i=0; i<NewNum; i++ )
		{
			UBOOL SavedLazyLoad = GLazyLoad;
			// Never load texture data until texture upload time.
			// This reduces peak memory usage a lot.
			GLazyLoad = 1;
			// Material duplication loads the data right away.
			if( GUglyHackFlags & 8 )
				GLazyLoad = 0;
			Ar << *new(Mips)FMipmap;
			GLazyLoad = SavedLazyLoad;
		}

	}
	else
	{
		// Save array.
		INT Num = Mips.Num();
		Ar << AR_INDEX(Num);
		for( INT i=0; i<Mips.Num(); i++ )
			Ar << Mips( i );
	}
	unguard;
}
void UTexture::Serialize( FArchive& Ar )
{
	guard(UTexture::Serialize);
	Super::Serialize( Ar );

	// Empty algorithmic textures.
	if( (Ar.IsSaving() || Ar.IsLoading()) && bParametric )
		for( INT i=0; i<Mips.Num(); i++ )
			Mips(i).DataArray.Empty();

	// Serialize the mipmaps.
	if( Ar.Ver() < 84 && OLDbHasComp )
	{
		//!!OLDVER
		TArray<FMipmap> JunkMips;
		SerializeMips( this, Ar, JunkMips );
		SerializeMips( this, Ar, Mips );
		Format = OLDCompFormat;
		OLDbHasComp = 0;
	}
	else
	{
		check(!OLDbHasComp);
		SerializeMips( this, Ar, Mips );
	}

	// Refill algorithmic textures.
	if( (Ar.IsSaving() || Ar.IsLoading()) && bParametric )
	{
		for( INT i=0; i<Mips.Num(); i++ )
		{
			INT Size = Mips(i).USize * Mips(i).VSize;
			Mips(i).DataArray.Empty    ( Size );
			Mips(i).DataArray.AddZeroed( Size );
		}
	}

	unguard;
}

// gam ---
#define MIN_TEXTURE_SIZE ((INT)4)
// --- gam

void UTexture::PostLoad()
{
	guard(UTexture::PostLoad);
	Super::PostLoad();

	// Make sure the palette is valid if it's a P8 texture.
	if( Format == TEXF_P8 && !Palette )
	{
		Palette = new(GetOuter())UPalette;
		for( INT i=0; i<256; i++ )
			new(Palette->Colors)FColor(i,i,i,0);
	}
	// Make sure non-P8 textures do not have palettes.
	if( Format != TEXF_P8 && Palette )
		Palette = NULL;

	// Handle post editing.
	UClamp = Clamp(UClamp,0,USize);
	VClamp = Clamp(VClamp,0,VSize);

	// Better suited for low end machines.
	MaxLOD = Mips.Num() - 1;
	for( INT i=0; i<Mips.Num(); i++ )
	{
		if( (Mips(i).USize < 64) || (Mips(i).VSize < 64) && ((Mips(i).USize < 256) && (Mips(i).VSize < 256)) )
		{
			MaxLOD = i;
			break;
		}
	}

	// Init animation.
	Accumulator = 0;
	SetLastUpdateTime(GCurrentTime);

	unguardobj;
}
void UTexture::Destroy()
{
	guard(UTexture::Destroy);
	delete RenderInterface;
	RenderInterface = NULL;
	Super::Destroy();
	unguard;
}
// gam ---
void UTexture::PostEditChange()
{
	guard(UTexture::PostEditChange);
    Super::PostEditChange();
    bRealtimeChanged = 1;
	unguard;
}
// --- gam

IMPLEMENT_CLASS(UTexture);

/*---------------------------------------------------------------------------------------
	UTexture mipmap generation.
---------------------------------------------------------------------------------------*/

//
// Initialize the texture for a given resolution, single mipmap.
//
void UTexture::Init( INT InUSize, INT InVSize )
{
	guard(UTexture::Init);
	check(!(USize & (USize-1)));
	check(!(VSize & (VSize-1)));

	// Allocate space.
	USize = UClamp = InUSize;
	VSize = VClamp = InVSize;
    UBits = appCeilLogTwo(USize);
    VBits = appCeilLogTwo(VSize);

	// Allocate first mipmap.
	Mips.Empty();
	if( Format==TEXF_RGBA8 )
		new(Mips)FMipmap(UBits,VBits, USize * VSize * 4);
	else
	if( Format==TEXF_G16 )
		new(Mips)FMipmap(UBits,VBits, USize * VSize * 2);
	else
		new(Mips)FMipmap(UBits,VBits);
	Mips(0).Clear();

	unguardobj;
}

//
// Generate all mipmaps for a texture.  Call this after setting the texture's palette.
// Erik changed: converted to simpler 2x2 box filter with 24-bit color intermediates.
//
void UTexture::CreateMips( UBOOL FullMips, UBOOL Downsample, UBOOL SpecialBorderColor, FColor BorderColor )
{
	guard(UTexture::CreateMips);
	
	if( SpecialBorderColor )
	{
		check( Format == TEXF_RGBA8 );
		check( Downsample );
	}

	FColor* Colors = GetColors();
	switch( Format )
	{
	case TEXF_L8:
		{
			FPlane C(0,0,0,0);
			for( INT i=0; i<Mips(0).DataArray.Num(); i++ )
			{
				BYTE A = Mips(0).DataArray(i);
				C += FColor(A,A,A,A);
			}
			MipZero = FColor(C/Mips(0).DataArray.Num());
		}
		break;
	case TEXF_P8:
		{
			check(Palette!=NULL);
			FPlane C(0,0,0,0);
			for( INT i=0; i<Mips(0).DataArray.Num(); i++ )
				C += Colors[Mips(0).DataArray(i)].Plane();
			MipZero = FColor(C/Mips(0).DataArray.Num());
		}
		break;
	case TEXF_RGBA8:
		{
			if( SpecialBorderColor )
				MipZero = BorderColor;
			else
			{	
				FPlane C(0,0,0,0);
				for( INT i=0; i<Mips(0).DataArray.Num() >> 2 ; i++ )
					C += ((FColor*)(Mips(0).DataArray.GetTypedData()))[i].Plane();
				MipZero = FColor(C/Mips(0).DataArray.Num());
			}
		}
		break;
	case TEXF_DXT1:
	case TEXF_DXT3:
	case TEXF_DXT5:
	case TEXF_G16:
		return;
	default:
		debugf( TEXT("Can't create mips for texture format %d"), Format );
		return;
	}

	if ( Format != TEXF_P8 )
		bMasked = 0;

	// Empty any mipmaps.
	if( Mips.Num() > 1 )
		Mips.Remove( 1, Mips.Num()-1 );

	// Allocate mipmaps.
	if( FullMips )
	{
		while( UBits-Mips.Num()>=0 || VBits-Mips.Num()>=0 )
		{
			INT Num = Mips.Num();
			INT MipUBits = Max(UBits-Num,0);
			INT MipVBits = Max(VBits-Num,0);
			switch( Format )
			{
			case TEXF_L8:
			case TEXF_P8:
				new(Mips)FMipmap( MipUBits, MipVBits );
				break;
			case TEXF_RGBA8:
				new(Mips)FMipmap( MipUBits, MipVBits, (1<<MipUBits)*(1<<MipVBits)*4 );
				break;
			}
		}
		if( Downsample )
		{
			// Build each mip from the next-larger mip.
			FColor* TrueSource = NULL;
			FColor* TrueDest   = NULL;		
			for( INT MipLevel=1; MipLevel<Mips.Num(); MipLevel++ )
			{
				FMipmap&  Src        = Mips(MipLevel-1);
				FMipmap&  Dest       = Mips(MipLevel  );
				INT       ThisUTile  = Src.USize;
				INT       ThisVTile  = Src.VSize;

				// Cascade down the mip sequence with truecolor source and destination textures.			
				TrueSource = TrueDest; // Last destination is current source..
				TrueDest = new(TEXT("FColor"))FColor[Src.USize * Src.VSize];
				//!!MAT
				if( !(bMasked) )
				{
					// Source coordinate masking important for degenerate mipmap sizes.
					DWORD MaskU = (ThisUTile-1);
					DWORD MaskV = (ThisVTile-1);
					INT   UD    = (1 & MaskU);
					INT   VD    = (1 & MaskV)*ThisUTile;

					// Non-masked mipmap.
					for( INT V=0; V<Dest.VSize; V++ )
					{
						for( INT U=0; U<Dest.USize; U++)
						{
							if( SpecialBorderColor && ((U==0) || (V==0) || (U==Dest.USize-1) || (V==Dest.VSize-1)) )
							{
								((FColor*)(Dest.DataArray.GetTypedData()))[ V*Dest.USize+U ] = BorderColor;
								continue;
							}

							// Get 4 pixels from a one-higher-level mipmap.
							INT TexCoord = U*2 + V*2*ThisUTile;
							FPlane C(0,0,0,0);
							if( TrueSource )
							{	
								C += TrueSource[ TexCoord +  0 +  0 ].Plane();
								C += TrueSource[ TexCoord + UD +  0 ].Plane();
								C += TrueSource[ TexCoord +  0 + VD ].Plane();
								C += TrueSource[ TexCoord + UD + VD ].Plane();
							}
							else
							{
								switch( Format )
								{
								case TEXF_L8:
									{
										BYTE A;
										A = Src.DataArray( TexCoord +  0 +  0 );
										C += FColor( A,A,A,A );
										A = Src.DataArray( TexCoord + UD +  0 );
										C += FColor( A,A,A,A );
										A = Src.DataArray( TexCoord +  0 + VD );
										C += FColor( A,A,A,A ); 
										A = Src.DataArray( TexCoord + UD + VD );
										C += FColor( A,A,A,A );
									}
									break;
								case TEXF_P8:
									C += Colors[ Src.DataArray( TexCoord +  0 +  0 ) ].Plane();
									C += Colors[ Src.DataArray( TexCoord + UD +  0 ) ].Plane();
									C += Colors[ Src.DataArray( TexCoord +  0 + VD ) ].Plane(); 
									C += Colors[ Src.DataArray( TexCoord + UD + VD ) ].Plane();
									break;
								case TEXF_RGBA8:
									C += ((FColor*)(Src.DataArray.GetTypedData()))[ TexCoord +  0 +  0 ].Plane();
									C += ((FColor*)(Src.DataArray.GetTypedData()))[ TexCoord + UD +  0 ].Plane();
									C += ((FColor*)(Src.DataArray.GetTypedData()))[ TexCoord +  0 + VD ].Plane(); 
									C += ((FColor*)(Src.DataArray.GetTypedData()))[ TexCoord + UD + VD ].Plane();							
									break;
								}
							}
							FColor MipTexel;
							TrueDest[V*Dest.USize+U] = MipTexel = FColor( C/4.f );
							switch( Format )
							{
							case TEXF_L8:
								Dest.DataArray(V*Dest.USize+U) = MipTexel.R;
								break;
							case TEXF_P8:
								Dest.DataArray(V*Dest.USize+U) = Palette->BestMatch( MipTexel, 0 );
								break;
							case TEXF_RGBA8:
								((FColor*)(Dest.DataArray.GetTypedData()))[ V*Dest.USize+U ] = MipTexel;
								break;
							}
						}
					}
				}
				else
				{
					// Masked mipmap.
					DWORD MaskU = (ThisUTile-1);
					DWORD MaskV = (ThisVTile-1);
					for( INT V=0; V<Dest.VSize; V++ )
					{
						for( INT U=0; U<Dest.USize; U++) 
						{
							INT F = 0;
							BYTE B;
							FPlane C(0,0,0,0);
							INT TexCoord = V*2*ThisUTile + U*2;
							for( INT I=0; I<2; I++ )
							{
								for( INT J=0; J<2; J++ )
								{
									B = Src.DataArray(TexCoord + (I&MaskU) + (J&MaskV)*ThisUTile);
									if( B )
									{
										F++;
										if( TrueSource )
											C += TrueSource[TexCoord + (I&MaskU) + (J&MaskV)*ThisUTile].Plane();
										else
											C += Colors[B].Plane();
									}
								}
							}						

							// One masked texel or less becomes a non-masked texel.
							if( F >= 2 )
							{
								FColor MipTexel = TrueDest[V*Dest.USize+U] = FColor( C / F );
								Dest.DataArray(V*Dest.USize+U) = Palette->BestMatch( MipTexel, 1 );
							}
							else
							{
								TrueDest[V*Dest.USize+U] = FColor(0,0,0);
								Dest.DataArray(V*Dest.USize+U) = 0;
							}
						}
					}
				}
				if( TrueSource )
					delete TrueSource; 
			}
			if( TrueDest )
				delete TrueDest;
		}
	}
	unguardobj;
}

//
// Set the texture's MaxColor and MinColor so that the texture can be normalized
// when converting to lower color resolutions like RGB 5-5-5 for hardware
// rendering.
//
void UTexture::CreateColorRange()
{
	guard(UTexture::CreateColorRange);
	if( Palette )
	{
		MaxColor = FColor(0,0,0,0);
		FColor* Colors = GetColors();
		for( INT i=0; i<Mips.Num(); i++ )
		{
			for( INT j=0; j<Mips(i).DataArray.Num(); j++ )
			{
				FColor& Color = Colors[Mips(i).DataArray(j)];
				MaxColor.R    = ::Max(MaxColor.R, Color.R);
				MaxColor.G    = ::Max(MaxColor.G, Color.G);
				MaxColor.B    = ::Max(MaxColor.B, Color.B);
				MaxColor.A    = ::Max(MaxColor.A, Color.A);
			}
		}
	}
	else MaxColor = FColor(255,255,255,255);
	unguardobj;
}

/*---------------------------------------------------------------------------------------
	UTexture functions.
---------------------------------------------------------------------------------------*/

//
// Clear the texture.
//
void UTexture::Clear( DWORD ClearFlags )
{
	guard(UTexture::Clear);
	if( ClearFlags & TCLEAR_Bitmap )
		for( INT i=0; i<Mips.Num(); i++ )	
			Mips(i).Clear();
	unguardobj;
}

//
// Clear the texture to the specified color
//
void UTexture::Clear( FColor TexelColor )
{
	guard(UTexture::Clear);

	switch( Format )
	{
	case TEXF_RGBA8:
		for( INT i=0;i<USize*VSize;i++ )
			((FColor*)(Mips(0).DataArray.GetTypedData()))[i] = TexelColor;
		break;
	}

	unguard;
}


FColor UTexture::GetTexel( FLOAT u1, FLOAT v1, FLOAT u2, FLOAT v2 )
{
	switch(Format)
	{
	case TEXF_RGBA8:
		return ((FColor*)(Mips(0).DataArray.GetTypedData()))[INT(v1)*USize+INT(u1)];
		break;
	}
	return FColor(0,0,0,0);
}

//
// Perform an arithmetic operation on the texture using another texture
//
void UTexture::ArithOp( UTexture* Operand, ETextureArithOp Operation )
{
	guard(UTexture::ArithOp);

	Operand->Mips(0).DataArray.Load();

	FLOAT URatio = (FLOAT)Operand->USize/(FLOAT)USize;
	FLOAT VRatio = (FLOAT)Operand->VSize/(FLOAT)VSize;

	switch( Format )
	{
	case TEXF_RGBA8:
		for( INT v=0;v<VSize;v++ )
		{
			for( INT u=0;u<USize;u++ )
			{
				FColor OrigColor = ((FColor*)(Mips(0).DataArray.GetTypedData()))[v*USize+u];
				FColor OperColor = Operand->GetTexel( URatio*u, VRatio*v, URatio*(u+1), VRatio*(v+1) );
				FColor DestColor;

				switch( Operation )
				{
				case TAO_Assign:
					DestColor = OperColor;
					break;
				case TAO_Add:
					DestColor = Min<INT>( OrigColor + OperColor, 255 );
					break;
				case TAO_Subtract:
					DestColor = Max<INT>( OrigColor - OperColor, 0 );
					break;
				case TAO_Multiply:
					DestColor = OrigColor * OperColor / 255;
					break;
				case TAO_AssignAlpha:
					DestColor.R = OperColor.A;
					DestColor.G = OperColor.A;
					DestColor.B = OperColor.A;
					DestColor.A = 255;
					break;
				case TAO_MultiplyOneMinusAlpha:
					DestColor.R = OrigColor.R * (255-OperColor.A) / 255;
					DestColor.G = OrigColor.G * (255-OperColor.A) / 255;
					DestColor.B = OrigColor.B * (255-OperColor.A) / 255;
					DestColor.A = 255;
					break;
				case TAO_AssignLtoR:
					DestColor = OrigColor;
					DestColor.R = OperColor.R;
					break;
				case TAO_AssignLtoG:
					DestColor = OrigColor;
					DestColor.G = OperColor.R;
					break;
				case TAO_AssignLtoB:
					DestColor = OrigColor;
					DestColor.B = OperColor.R;
					break;
				case TAO_AssignLtoA:
					DestColor = OrigColor;
					DestColor.A = OperColor.R;
					break;
				default:
					DestColor = OrigColor;
					break;
				}
				((FColor*)(Mips(0).DataArray.GetTypedData()))[v*USize+u] = DestColor;
			}
		}
		break;
	}

	unguard;
}


/*---------------------------------------------------------------------------------------
	UPalette implementation.
---------------------------------------------------------------------------------------*/

UPalette::UPalette()
{
	guard(UPalette::UPalette);
	unguard;
}
void UPalette::Serialize( FArchive& Ar )
{
	guard(UPalette::Serialize);
	Super::Serialize( Ar );

	Ar << Colors;
	if( Ar.Ver()<66 )
		for( INT i=0; i<Colors.Num(); i++ )
			Colors(i).A = 255;

	unguard;
}
IMPLEMENT_CLASS(UPalette);

/*-----------------------------------------------------------------------------
	UPalette general functions.
-----------------------------------------------------------------------------*/

//
// Adjust a regular (imported) palette.
//
void UPalette::FixPalette()
{
	guard(UPalette::FixPalette);

	FColor TempColors[256];
	for( int i=0; i<256; i++ )
		TempColors[i] = Colors(0);

	for( int iColor=0; iColor<8; iColor++ )
	{
		int iStart = (iColor==0) ? 1 : 32*iColor;
		for( int iShade=0; iShade<28; iShade++ )
			TempColors[16 + iColor + (iShade<<3)] = Colors(iStart + iShade);

	}
	for( int i=0; i<256; i++ )
	{
		Colors(i) = TempColors[i];
		Colors(i).A = i+0x10;
	}
	Colors(0).A=0;

	unguardobj;
}

//
// Find closest palette color matching a given RGB value.
//
BYTE UPalette::BestMatch( FColor Color, INT First )
{
	guard(UPalette::BestMatch);
	INT BestDelta         = MAXINT;
	INT BestUnscaledDelta = MAXINT;
	INT BestColor         = First;
	INT TexRed            = Color.R;
	INT TexBlue           = Color.B;
	INT TexGreen          = Color.G;
	for( INT TestColor=First; TestColor<NUM_PAL_COLORS; TestColor++ )
	{
		// By comparing unscaled green, saves about 4 out of every 5 full comparisons.
		FColor* ColorPtr   = &Colors(TestColor);
		INT     GreenDelta = Square(ColorPtr->G - TexGreen);

		// Same as comparing 8*GreenDelta with BestDelta.
		if( GreenDelta < BestUnscaledDelta )
		{
			INT Delta = 
			(
				8 * GreenDelta                     +
				4 * Square(ColorPtr->R - TexRed  ) +
				1 * Square(ColorPtr->B - TexBlue )
			);
			if( Delta < BestDelta ) 
			{
				BestColor         = TestColor;
				BestDelta         = Delta;
				BestUnscaledDelta = (Delta + 7) >> 3; 
			}
		}
	}
	return BestColor;
	unguardobj;
}

//
// Sees if this palette is a duplicate of an existing palette.
// If it is, deletes this palette and returns the existing one.
// If not, returns this palette.
//
UPalette* UPalette::ReplaceWithExisting()
{
	guard(UPalette::ReplaceWithExisting);
	for( TObjectIterator<UPalette>It; It; ++It )
	{
		if( *It!=this && It->GetOuter()==GetOuter() )
		{
			FColor* C1 = Colors.GetTypedData();
			FColor* C2 = It->Colors.GetTypedData();
			int i;
			for( i=0; i<NUM_PAL_COLORS; i++ )
				if( *C1++ != *C2++ ) break;
			if( i == NUM_PAL_COLORS )
			{
				debugf( NAME_Log, TEXT("Replaced palette %s with %s"), GetName(), It->GetName() );
				delete this;
				return *It;
			}
		}
	}
	return this;
	unguardobj;
}

/*-----------------------------------------------------------------------------
	DXT functions.
-----------------------------------------------------------------------------*/

// Src may point into Out.
void CompressDXT( const FColor* Src, INT Width, INT Height, ETextureFormat Format, TArray<BYTE>& Out )
{
	guard(CompressDXT);
	INT Flags = Format == TEXF_DXT1 ? squish::kDxt1 : Format == TEXF_DXT3 ? squish::kDxt3 : squish::kDxt5;
	TArray<BYTE> RGBA(Width * Height * 4);
	for( INT i=0; i<Width*Height; i++ )
	{
		RGBA(i*4+0) = Src[i].R;
		RGBA(i*4+1) = Src[i].G;
		RGBA(i*4+2) = Src[i].B;
		// Opaque, otherwise squish picks DXT1's punch-through mode for texels below half alpha.
		RGBA(i*4+3) = Format == TEXF_DXT1 ? 255 : Src[i].A;
	}
	Out.Empty();
	Out.Add( squish::GetStorageRequirements(Width, Height, Flags) );
	squish::CompressImage( RGBA.GetTypedData(), Width, Height, Out.GetTypedData(), Flags );
	unguard;
}

UBOOL UTexture::Compress( ETextureFormat InFormat, UBOOL Mipmaps, FDXTCompressionOptions* Options )
{
	guard(UTexture::Compress);
	if( InFormat != TEXF_DXT1 && InFormat != TEXF_DXT3 && InFormat != TEXF_DXT5 )
		return 0;
	if( Format != TEXF_RGBA8 && Format != TEXF_P8 )
		return 0;

	// Textures are always lazy loaded.
	Mips(0).DataArray.Load();
	TArray<BYTE> Data(USize * VSize * 4);
	if( Format == TEXF_RGBA8 )
		appMemcpy( Data.GetTypedData(), Mips(0).DataArray.GetTypedData(), Data.Num() );
	else
		RenderInterface->GetTextureData(0,Data.GetTypedData(),RenderInterface->GetWidth()*sizeof(FColor),TEXF_RGBA8);

	// A new mip 0 drops the lazy loader, which would otherwise reload the uncompressed source.
	Mips.Empty();
	new(Mips)FMipmap( UBits, VBits, Data.Num() );
	appMemcpy( Mips(0).DataArray.GetTypedData(), Data.GetTypedData(), Data.Num() );
	Format = TEXF_RGBA8;

	// CreateMips clears bMasked for anything but P8.
	UBOOL Masked = bMasked;
	UBOOL Border = Options && Options->Border;
	CreateMips( Mipmaps, 1, Border, Border ? Options->BorderColor : FColor(0,0,0,0) );
	bMasked = Masked;

	for( INT MipIndex=0; MipIndex<Mips.Num(); MipIndex++ )
	{
		FMipmap& Mip    = Mips(MipIndex);
		FColor*  Colors = (FColor*)Mip.DataArray.GetTypedData();
		if( Options && Options->Fade )
		{
			// FadeAmount is the percentage faded per mip level, as nvDXT defined it.
			FLOAT  Alpha = Min( MipIndex * Options->FadeAmount / 100.f, 1.f );
			FColor To    = Options->FadeToColor;
			for( INT i=0; i<Mip.USize*Mip.VSize; i++ )
			{
				Colors[i].R = appRound( Colors[i].R + (To.R - Colors[i].R) * Alpha );
				Colors[i].G = appRound( Colors[i].G + (To.G - Colors[i].G) * Alpha );
				Colors[i].B = appRound( Colors[i].B + (To.B - Colors[i].B) * Alpha );
			}
		}
		CompressDXT( Colors, Mip.USize, Mip.VSize, InFormat, Mip.DataArray );
	}
	Format = InFormat;

	// Make sure it gets reuploaded.
	bRealtimeChanged = 1;

	return 1;
	unguard;
}

struct FDXTExplicitAlphaBlock
{
	_WORD	Rows[4];
};

struct FColor565
{
	unsigned B : 5;
	unsigned G : 6;
	unsigned R : 5;
};

struct FDXTColorBlock
{
	_WORD	Colors[2];
	BYTE	Rows[4];
};

struct FDXT3Block
{
	FDXTExplicitAlphaBlock	Alpha;
	FDXTColorBlock			Color;
};

UBOOL UTexture::Decompress( ETextureFormat InFormat )
{
	if(Format == TEXF_DXT3)
	{
		if(InFormat == TEXF_RGBA8)
		{
			for(INT MipIndex = 0;MipIndex < Mips.Num();MipIndex++)
			{
				INT				MipWidth = USize >> MipIndex,
								MipHeight = VSize >> MipIndex;
				TArray<FColor>	MipColors(MipWidth * MipHeight);

				if(!Mips(MipIndex).DataArray.Num())
					Mips(MipIndex).DataArray.Load();

				for(INT Y = 0;Y < MipHeight;Y += 4)
				{
					for(INT X = 0;X < MipWidth;X += 4)
					{
						FDXT3Block*	SourceBlock = (FDXT3Block*) ((BYTE*) Mips(MipIndex).DataArray.GetTypedData() + (Y / 4) * (MipWidth / 4 * 16) + (X / 4) * 16);
						FColor		Colors[4];
						FColor565*	SourceColor;

						SourceColor = (FColor565*) &SourceBlock->Color.Colors[0];
						Colors[0].A = 0xff;
						Colors[0].R = SourceColor->R << 3;
						Colors[0].G = SourceColor->G << 2;
						Colors[0].B = SourceColor->B << 3;

						SourceColor = (FColor565*) &SourceBlock->Color.Colors[1];
						Colors[1].A = 0xff;
						Colors[1].R = SourceColor->R << 3;
						Colors[1].G = SourceColor->G << 2;
						Colors[1].B = SourceColor->B << 3;

						if(*((_WORD*)&SourceBlock->Color.Colors[0]) > *((_WORD*)&SourceBlock->Color.Colors[1]))
						{
							Colors[2].A = 0xff;
							Colors[2].R = (BYTE) ((((_WORD) Colors[0].R) * 2 + ((_WORD) Colors[1].R)) / 3);
							Colors[2].G = (BYTE) ((((_WORD) Colors[0].G) * 2 + ((_WORD) Colors[1].G)) / 3);
							Colors[2].B = (BYTE) ((((_WORD) Colors[0].B) * 2 + ((_WORD) Colors[1].B)) / 3);

							Colors[3].A = 0xff;
							Colors[3].R = (BYTE) ((((_WORD) Colors[0].R) + ((_WORD) Colors[1].R) * 2) / 3);
							Colors[3].G = (BYTE) ((((_WORD) Colors[0].G) + ((_WORD) Colors[1].G) * 2) / 3);
							Colors[3].B = (BYTE) ((((_WORD) Colors[0].B) + ((_WORD) Colors[1].B) * 2) / 3);
						}
						else
						{
							Colors[2].A = 0xff;
							Colors[2].R = (BYTE) ((((_WORD) Colors[0].R) + ((_WORD) Colors[1].R)) / 2);
							Colors[2].G = (BYTE) ((((_WORD) Colors[0].G) + ((_WORD) Colors[1].G)) / 2);
							Colors[2].B = (BYTE) ((((_WORD) Colors[0].B) + ((_WORD) Colors[1].B)) / 2);

							Colors[3].A = 0x00;
							Colors[3].R = 0x00;
							Colors[3].G = 0xff;
							Colors[3].B = 0xff;
						}

						for(INT Row = 0;Row < 4;Row++)
						{
							FColor*		DestColor = MipColors.GetTypedData() + (Y + Row) * MipWidth + X;

							DestColor[0] = Colors[(SourceBlock->Color.Rows[Row] >> 0) & 0x03];
							DestColor[1] = Colors[(SourceBlock->Color.Rows[Row] >> 2) & 0x03];
							DestColor[2] = Colors[(SourceBlock->Color.Rows[Row] >> 4) & 0x03];
							DestColor[3] = Colors[(SourceBlock->Color.Rows[Row] >> 6) & 0x03];
						}
					}
				}

				Mips(MipIndex).DataArray.Empty(MipWidth * MipHeight * sizeof(FColor));
				Mips(MipIndex).DataArray.Add(MipWidth * MipHeight * sizeof(FColor));
	
				appMemcpy(Mips(MipIndex).DataArray.GetTypedData(),MipColors.GetTypedData(),MipWidth * MipHeight * sizeof(FColor));
			}

			Format = TEXF_RGBA8;
			bRealtimeChanged = 1;

			return 1;
		}
	}
	return 0;
}

static UBOOL ConvertDXT1(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA );
static UBOOL ConvertDXT3(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA );
static UBOOL ConvertDXT5(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA );

ETextureFormat UTexture::ConvertDXT( INT MipLevel, UBOOL ForceRGBA, UBOOL Use4444, BYTE** Data )
{
	guard(UTexture::ConvertDXT);

	// Texture are always lazy loaded.
	if( !Mips(MipLevel).DataArray.Num() )
		Mips(MipLevel).DataArray.Load();
	
	UBOOL IsDXT1	= 0;
	INT Width		= Mips(MipLevel).USize;
	INT Height		= Mips(MipLevel).VSize;

	// Handle corrupted textures.
	if( Mips(MipLevel).DataArray.GetTypedData() == NULL )
	{
		*Data = new BYTE[Width * Height *4];
		appMemzero( *Data, Width * Height * 4 );
		return TEXF_RGBA8;
	}

	switch( Format )
	{
	case TEXF_DXT1:
		IsDXT1 = ConvertDXT1( Mips(MipLevel).DataArray.GetTypedData(), Width, Height, Use4444, Data, ForceRGBA );
		break;
	case TEXF_DXT3:
		IsDXT1 = ConvertDXT3( Mips(MipLevel).DataArray.GetTypedData(), Width, Height, Use4444, Data, ForceRGBA );
		break;
	case TEXF_DXT5:
		IsDXT1 = ConvertDXT5( Mips(MipLevel).DataArray.GetTypedData(), Width, Height, Use4444, Data, ForceRGBA );
		break;
	default:
		appErrorf(TEXT("Invalid texture format passed to ConvertDXT"));
	}

	Mips(MipLevel).DataArray.Unload();

	return IsDXT1 ? TEXF_DXT1 : TEXF_RGBA8;
	unguard;
}


ETextureFormat UTexture::ConvertDXT()
{
	guard(UTexture::ConvertDXT);
	if( Format == TEXF_DXT3 || Format == TEXF_DXT5 )
	{
		UBOOL ForceRGBA		= 0;
		UBOOL ConvertTo4444	= 0;			
		
		for( INT m=0; m< Mips.Num(); m++ )
		{
			BYTE *Data	= NULL;
			INT Width	= Mips(m).USize;
			INT Height	= Mips(m).VSize;

			// Texture are always lazy loaded.
			if( !Mips(m).DataArray.Num() )
				Mips(m).DataArray.Load();
			check( Mips(m).DataArray.GetTypedData() );

			UBOOL IsDXT1;
			if( Format == TEXF_DXT3 )
				IsDXT1 = ConvertDXT3( Mips(m).DataArray.GetTypedData(), Width, Height, ConvertTo4444, &Data, ForceRGBA );
			else 
				IsDXT1 = ConvertDXT5( Mips(m).DataArray.GetTypedData(), Width, Height, ConvertTo4444, &Data, ForceRGBA );
								
			// Make sure all miplevels use RGBA if needed.
			if( m==0 && !IsDXT1 )
				ForceRGBA = 1;
							
			Mips(m).DataArray.Empty();
			INT Size = IsDXT1 ? Max(Width/4,1) * Max(Height/4,1) * 8 : ConvertTo4444 ? Width * Height * 2 : Width * Height * 4;
			Mips(m).DataArray.Add( Size );
			appMemcpy( Mips(m).DataArray.GetTypedData(), Data, Size );
			delete [] Data;
		}
		Format = ForceRGBA ? TEXF_RGBA8 : TEXF_DXT1;
	}

	return (ETextureFormat) Format;
	unguard
}


// squish writes RGBA, the engine stores BGRA.
static BYTE* DecompressDXT( void* Src, INT X, INT Y, INT Flags )
{
	BYTE* Dest = new BYTE[4*X*Y];
	squish::DecompressImage( Dest, X, Y, Src, Flags );
	for( INT i=0; i<X*Y; i++ )
		Exchange( Dest[i*4], Dest[i*4+2] );
	return Dest;
}

static UBOOL ConvertDXT1(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA )
{
	if (b4444)
	{
		debugf(TEXT("DXT CONVERT: DXT1 texture - converting to 4444"));
		check(0);
	}
	else
	{
		*ppDestData = DecompressDXT( pSrcData, x, y, squish::kDxt1 );
	}
	return false;
}

/*******************************************************************************
 * Function Name  : ConvertDXT3
 * Returns		  : TRUE or FALSE
 * Inputs		  : *pSrcData, x, y, b4444
 * Outputs		  : **ppDestData
 * Global Used    : None
 * Description    : Convert DXT3 textures to DXT1 or RGBA (4444 or 8888). 
 *					Convert to DXT1 is source data if fully opaque, RGBA otherwise.
 *					- pSrcData points to the source DXT3 data
 *					- x and y are the dimensions of the source data, in pixels.
 *					- b4444 indicates if the texture data should be converted to
 *					4444 (default is 8888) if non-opaque.
 *					- pDestData is a pointer that will contain the output data.
 *					This function returns TRUE if resulting data is DXT1, and 
 *					FALSE if the resulting data is in RGBA format.
 * Remarks		  : This function allocates memory for the pointer *ppDestData.
 *					It is up to application to free this buffer after use.
 *					
 *******************************************************************************/
static UBOOL ConvertDXT3(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA )
{
	DWORD			*pdwBlockDXT1, *pdwBlock;
	DWORD			*pdwAlpha, dwAlphaInfo=0xFFFFFFFF;
	int				nBlockX, nBlockY, nNumberOfBlocks;
	int				i;

	// Compute number of blocks.
	nBlockX = Max(x/4, 1);
	nBlockY = Max(y/4, 1);
	nNumberOfBlocks = nBlockX*nBlockY;
	i = nNumberOfBlocks;

	// Scan source data to determine if DXT3 data is opaque or not.
	pdwAlpha = (DWORD *)pSrcData;
	while (i-- && dwAlphaInfo==0xFFFFFFFF)
	{	
		dwAlphaInfo &= *pdwAlpha++;	// First two rows of alpha data
		dwAlphaInfo &= *pdwAlpha;	// Last two rows of alpha data
		pdwAlpha += 3;				// Go to next translucent block (3*4 = 12 bytes away)
	}

	// Is texture opaque?
	if (dwAlphaInfo!=0xFFFFFFFF || ForceRGBA)
	{
		// Conversion to RGBA.
		if (b4444)
		{
			check(0);
		}
		else
		{
			*ppDestData = DecompressDXT( pSrcData, x, y, squish::kDxt3 );
		}
		// Texture was converted to RGBA.
		return false;
	}
	
	// Conversion to DXT1.
	
	// Allocate memory for destination data (8 bytes per block).
	*ppDestData = new BYTE[nNumberOfBlocks*8];

	// Initiate pointers.
	pdwBlockDXT1 = (DWORD *)(*ppDestData);
	pdwBlock =     (DWORD *)pSrcData + 2;

	for (i=0; i<nNumberOfBlocks; i++)
	{
		if(((_WORD*)pdwBlock)[1] > ((_WORD*)pdwBlock)[0])
		{
			// DXT1 decompression will consider this an alpha block, that's not good...
			*pdwBlockDXT1++ = _rotr(*pdwBlock++, 16);		// Switch colours
			*pdwBlockDXT1++ = *pdwBlock ^ 0x55555555;		// Invert bit 0 of each bit-pair
		}
		else
		{
		*pdwBlockDXT1++ = *pdwBlock++;
		*pdwBlockDXT1++ = *pdwBlock;
		}

		pdwBlock+=3;
	}

	// Texture was converted to DXT1.
	return true;
}


/*******************************************************************************
 * Function Name  : ConvertDXT5
 * Returns		  : TRUE or FALSE
 * Inputs		  : *pSrcData, x, y, b4444
 * Outputs		  : **ppDestData
 * Global Used    : None
 * Description    : Convert DXT5 textures to DXT1 or RGBA (4444 or 8888). 
 *					Convert to DXT1 is source data if fully opaque, RGBA otherwise.
 *					- pSrcData points to the source DXT5 data
 *					- x and y are the dimensions of the source data, in pixels.
 *					- b4444 indicates if the texture data should be converted to
 *					4444 (default is 8888) if non-opaque.
 *					- pDestData is a pointer that will contain the output data.
 *					This function returns TRUE if resulting data is DXT1, and 
 *					FALSE if the resulting data is in RGBA format.
 * Remarks		  : This function allocates memory for the pointer *ppDestData.
 *					It is up to application to free this buffer after use.
 *					DXT5 detection relies on the two 8 bits alpha values being 0xFF.
 *					
 *******************************************************************************/
static UBOOL ConvertDXT5(void *pSrcData, int x, int y, UBOOL b4444, BYTE **ppDestData, UBOOL ForceRGBA )
{
	DWORD			*pdwBlockDXT1, *pdwBlock;
	_WORD			*pwAlpha, wAlphaInfo=0xFFFF;
	int				nBlockX, nBlockY, nNumberOfBlocks;
	int				i;

	// Compute number of blocks.
	nBlockX = Max(x/4, 1);
	nBlockY = Max(y/4, 1);
	nNumberOfBlocks = nBlockX*nBlockY;
	i = nNumberOfBlocks;

	// Scan source data to determine if DXT5 data is opaque or not.
	pwAlpha = (_WORD *)pSrcData;
	while (i-- && wAlphaInfo==0xFFFF)
	{	
		wAlphaInfo &= *pwAlpha;		// Check two alpha values
		pwAlpha += 8;				// Go to next translucent block (8*2 = 16 bytes away)
	}

	// Is texture opaque?
	if (wAlphaInfo!=0xFFFF || ForceRGBA)
	{
		// Conversion to RGBA
		if (b4444)
		{
			check(0);
		}
		else
		{
			*ppDestData = DecompressDXT( pSrcData, x, y, squish::kDxt5 );
		}
		// Texture was converted to RGBA.
		return false;
	}
	
	// Conversion to DXT1.

	// Allocate memory for destination data (8 bytes per block).
	*ppDestData = new BYTE[nNumberOfBlocks*8];

	// Initiate pointers.
	pdwBlockDXT1 = (DWORD *)(*ppDestData);
	pdwBlock =     (DWORD *)pSrcData + 2;

	for (i=0; i<nNumberOfBlocks; i++)
	{
		if(((_WORD*)pdwBlock)[1] > ((_WORD*)pdwBlock)[0])
		{
			// DXT1 decompression will consider this an alpha block, that's not good...
			*pdwBlockDXT1++ = _rotr(*pdwBlock++, 16);		// Switch colours
			*pdwBlockDXT1++ = *pdwBlock ^ 0x55555555;		// Invert bit 0 of each bit-pair
		}
		else
		{
		*pdwBlockDXT1++ = *pdwBlock++;
		*pdwBlockDXT1++ = *pdwBlock;
		}

		pdwBlock+=3;
	}

	// Texture was converted to DXT1.
	return true;
}


/*-----------------------------------------------------------------------------
	FStaticTexture.
-----------------------------------------------------------------------------*/

//
//	FStaticTexture::FStaticTexture
//
FStaticTexture::FStaticTexture(UTexture* InTexture)
{
	Texture = InTexture;
	CacheId = MakeCacheID(CID_RenderTexture);
	LastRevision = 1;
}

//
//	FStaticTexture::GetCacheId
//
QWORD FStaticTexture::GetCacheId()
{
	return CacheId;
}

//
//	FStaticTexture::GetRevision
//
INT FStaticTexture::GetRevision()
{
	if(Texture->bRealtimeChanged)
	{
		LastRevision++;
		Texture->bRealtimeChanged = 0;
	}

	return LastRevision;
}

//
//	FStaticTexture::GetWidth
//
INT FStaticTexture::GetWidth()
{
	return Texture->USize;
}

//
//	FStaticTexture::GetHeight
//
INT FStaticTexture::GetHeight()
{
	return Texture->VSize;
}

//
//  FStaticTexture::GetFirstMip
//
INT FStaticTexture::GetFirstMip()
{
	INT FirstMip = Texture->DefaultLOD();
	while( ((GetWidth() >> FirstMip) < 64) && ((GetHeight() >> FirstMip) < 64) && (FirstMip > 0) )
		FirstMip--;

	return Min( Max( FirstMip, Texture->MinLOD ), Max( Texture->Mips.Num()-1, 0 ) );
}

//
//	FStaticTexture::GetNumMips
//
INT FStaticTexture::GetNumMips()
{
	return Texture->Mips.Num();
}

//
//	FStaticTexture::GetFormat
//
ETextureFormat FStaticTexture::GetFormat()
{
	return (ETextureFormat) Texture->Format;
}

//
//	FStaticTexture::GetUClamp()
//
ETexClampMode FStaticTexture::GetUClamp()
{
	return (ETexClampMode) Texture->UClampMode;
}

//
//	FStaticTexture::GetVClamp()
//
ETexClampMode FStaticTexture::GetVClamp()
{
	return (ETexClampMode) Texture->VClampMode;
}

//
//  FStaticTexture::GetUTexture()
//
UTexture* FStaticTexture::GetUTexture()
{
	return Texture;
}

// Mip sizes and data come from the package, while renderers size their buffers from the texture size.
static UBOOL MipFits( UTexture* Texture, INT MipIndex )
{
	FMipmap&		Mip		= Texture->Mips(MipIndex);
	ETextureFormat	Format	= (ETextureFormat) Texture->Format;
	UBOOL			DXT		= Format == TEXF_DXT1 || Format == TEXF_DXT3 || Format == TEXF_DXT5;
	INT				USize	= DXT ? Max(Mip.USize,4) : Mip.USize,
					VSize	= DXT ? Max(Mip.VSize,4) : Mip.VSize;
	return	Mip.USize == Max(Texture->USize >> MipIndex,1) && Mip.VSize == Max(Texture->VSize >> MipIndex,1)
		&&	(QWORD)GetBytesPerPixel(Format,USize) * VSize <= (QWORD)Mip.DataArray.Num();
}

//
//	FStaticTexture::GetRawTextureData()
//
void* FStaticTexture::GetRawTextureData(INT MipIndex)
{
	guard(FStaticTexture::GetRawTextureData);

	if( MipIndex >= Texture->Mips.Num() )
		return NULL;

	if(!Texture->bParametric)
		Texture->Mips(MipIndex).DataArray.Load();

	if( !MipFits(Texture,MipIndex) )
	{
		debugf(TEXT("WARNING: Texture [%s] is corrupted."),Texture->GetPathName());
		return NULL;
	}
	return Texture->Mips(MipIndex).DataArray.GetTypedData();

	unguard;
}


//
//	FStaticTexture::UnloadRawTextureData()
//
void FStaticTexture::UnloadRawTextureData(INT MipIndex)
{
	guard(FStaticTexture::UnloadRawTextureData);

	if(!GIsEditor && !Texture->bParametric && MipIndex < Texture->Mips.Num())
		Texture->Mips(MipIndex).DataArray.Unload();

	unguard;
}


//
//	FStaticTexture::GetTextureData()
//
void FStaticTexture::GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
{
	guard(FStaticTexture::GetTextureData);

	// Servers often strip texture data from their packages, and a client downloads such a copy when it lacks the full one.
	if( MipIndex >= Texture->Mips.Num() )
	{
		debugf(TEXT("WARNING: Texture [%s] has no data."),Texture->GetPathName());
		return;
	}

	FMipmap&		Mipmap = Texture->Mips(MipIndex);
	ETextureFormat	SourceFormat = (ETextureFormat) Texture->Format;

	if(!Texture->bParametric)
		Mipmap.DataArray.Load();

	if( Mipmap.DataArray.GetTypedData() == NULL || !MipFits(Texture,MipIndex) )
	{
		debugf(TEXT("WARNING: Texture [%s] is corrupted."),Texture->GetPathName());
		return;
	}

	if( Texture->Format == TEXF_P8 && Texture->Palette == NULL )
	{
		debugf(TEXT("WARNING: Texture [%s] is P8 though has no palette."),Texture->GetPathName());
		return;
	}

	// Coloring different LOD sets.
	UGameEngine*	GameEngine		= UTexture::__Client ? Cast<UGameEngine>(UTexture::__Client->Engine) : NULL;
	ALevelInfo*		LevelInfo		= UTexture::__Client && UTexture::__Client->Viewports.Num() && UTexture::__Client->Viewports(0) && UTexture::__Client->Viewports(0)->Actor ? UTexture::__Client->Viewports(0)->Actor->Level : NULL;
	UBOOL			ColorLODSets;

	if(	
		GameEngine 
	&&	LevelInfo
	&&	LevelInfo->NetMode == NM_Standalone
	&&	( 
			(GameEngine->ColorWorldTextures		&& Texture->LODSet == LODSET_World) 
		||	(GameEngine->ColorInterfaceTextures && Texture->LODSet == LODSET_Interface) 
		||	(
				(GameEngine->ColorPlayerAndWeaponTextures && Texture->LODSet == LODSET_PlayerSkin) 
			||	(GameEngine->ColorPlayerAndWeaponTextures && Texture->LODSet == LODSET_WeaponSkin)
			)
		)
	)
		ColorLODSets = 1;
	else
		ColorLODSets = 0;

	if( ColoredMips || ColorLODSets )
	{
		FColor MipColors[12] =
		{
			FColor( 255, 0, 0, 128 ),
			FColor( 0, 0, 255, 128 ),
			FColor( 0, 255, 0, 128 ),
			FColor( 255, 0, 255, 128 ),
			FColor( 255, 255, 0, 128 ),
			FColor( 0, 255, 255, 128 ),
			FColor( 0, 0, 0, 128 ),
			FColor( 255, 255, 255, 128 ),
			FColor( 64, 128, 192, 128 ),
			FColor( 128, 192, 64, 128 ),
			FColor( 192, 64, 128, 128 ),
			FColor( 128, 128, 128, 128)
		};

		if( !ColoredMips )
		{
			FColor MipColor;
			switch( Texture->LODSet )
			{
				case LODSET_World:
				case LODSET_Terrain:
					MipColor = FColor(255,0,0,255);
					break;
				case LODSET_PlayerSkin:
				case LODSET_WeaponSkin:
					MipColor = FColor(0,0,255,255);
					break;
				case LODSET_Interface:
					MipColor = FColor(0,255,0,255);
					break;
				case LODSET_RenderMap:
				case LODSET_Lightmap:
				default:
					MipColor = FColor(255,255,255,255);
					break;
			}

			for( INT i=0; i<12; i++ )
				MipColors[i] = MipColor;
		}

		FColor& MipColor = MipColors[Clamp(MipIndex, 0, 11)];

		if( DestFormat == TEXF_DXT1 )
		{
			_WORD Color = MipColor.HiColor565();

			QWORD	ColorPattern= (QWORD)Color;
	
			INT		USize		= Max(Mipmap.USize,4),
					VSize		= Max(Mipmap.VSize,4),
					NumRows		= VSize / 4,
					NumColums	= USize / 4;
		
			BYTE*	Dummy		= (BYTE*) Dest;

			for(INT Y = 0;Y < NumRows;Y++)
			{
				QWORD* DestData = (QWORD*) Dummy;
				for( INT X = 0; X < NumColums; X++ )
					*(DestData++) = ColorPattern;
				Dummy += DestStride;
			}
		}
		// Alpha is set to 0.5
		else if( (DestFormat == TEXF_DXT3) || (DestFormat == TEXF_DXT5) )
		{
			_WORD Color = MipColor.HiColor565();

			QWORD	ColorPattern= (QWORD)Color,
					AlphaPattern= (DestFormat == TEXF_DXT3) ? 0x7777777777777777ULL : 0x8081ffffffffffffULL;

			INT		USize		= Max(Mipmap.USize,4),
					VSize		= Max(Mipmap.VSize,4),
					NumRows		= VSize / 4,
					NumColums	= USize / 4;
		
			BYTE*	Dummy		= (BYTE*) Dest;

			for(INT Y = 0;Y < NumRows;Y++)
			{
				QWORD* DestData = (QWORD*) Dummy;
				for( INT X = 0; X < NumColums; X++ )
				{
					*(DestData++) = AlphaPattern;
					*(DestData++) = ColorPattern;
				}
				Dummy += DestStride;
			}
		}
		else if( DestFormat == TEXF_RGBA8 )
		{
			DWORD Color	= MipColor.DWColor();

			DWORD*	DestData	= (DWORD*) Dest;
			for( INT i=DestStride * Mipmap.VSize / 4; i>0; --i )
				*(DestData++) = Color;
		}
	} 
	else if(DestFormat == TEXF_DXT1 || DestFormat == TEXF_DXT3 || DestFormat == TEXF_DXT5)
	{
		check(SourceFormat == DestFormat);

		BYTE*	SourceData = Mipmap.DataArray.GetTypedData();
		BYTE*	DestData = (BYTE*) Dest;
		INT		USize = Max(Mipmap.USize,4),
				VSize = Max(Mipmap.VSize,4),
				NumRows = VSize / 4,
				SourceStride = GetBytesPerPixel(SourceFormat,USize);

		SourceStride *= 4;

		if(SourceStride == DestStride)
			appMemcpy(DestData,SourceData,SourceStride*NumRows);
		else
		{
			for(INT Y = 0;Y < NumRows;Y++)
			{
				appMemcpy(DestData,SourceData,SourceStride);
		
				SourceData += SourceStride;
				DestData += DestStride;
			}
		}
	}
	else if(SourceFormat == DestFormat)
	{
		// No format conversion.
		BYTE*	SourceData = Mipmap.DataArray.GetTypedData();
		BYTE*	DestData = (BYTE*) Dest;
		INT		SourceStride = GetBytesPerPixel(SourceFormat,Mipmap.USize);

		if(SourceStride == DestStride)
			appMemcpy(DestData,SourceData,SourceStride * Mipmap.VSize);
		else
		{
			for(INT Y = 0;Y < Mipmap.VSize;Y++)
			{
				appMemcpy(DestData,SourceData,SourceStride);

				SourceData += SourceStride;
				DestData += DestStride;
			}
		}
	}
	else if(SourceFormat == TEXF_P8 && DestFormat == TEXF_RGBA8)
	{
		// Expand palettized texture to 32-bit RGBA.
		static DWORD	Palette[256];
		static DWORD*	PalettePtr = Palette;
		BYTE*			SourceData = Mipmap.DataArray.GetTypedData();

		appMemcpy(Palette,Texture->Palette->Colors.GetTypedData(),Min(Texture->Palette->Colors.Num(),256) * sizeof(FColor));

		//!!MAT
		if(Texture->bMasked)
			Palette[0] = 0;

		DWORD*	DestData = (DWORD*) Dest;
		INT		Width = Mipmap.USize,
				DestSkip = DestStride / sizeof(DWORD),
				Height = Mipmap.VSize;

		for(INT Y = 0;Y < Height;Y++)
		{
			for(INT X = 0;X < Width;X++)
				DestData[X] = Palette[SourceData[X]];

			DestData += DestSkip;
			SourceData += Width;
		}
	}
	else if(SourceFormat == TEXF_G16 && DestFormat == TEXF_RGBA8)
	{
		// Expand 16-bit greyscale to 32-bit RGBA.
		_WORD*	SourceData = (_WORD*) Mipmap.DataArray.GetTypedData();
		FColor*	DestData = (FColor*) Dest;

		for(INT Y = 0;Y < Mipmap.VSize;Y++)
		{
			for(INT X = 0;X < Mipmap.USize;X++)
			{
				BYTE	Intensity = (*SourceData++) >> 8;

				*DestData++ = FColor(Intensity,Intensity,Intensity,255);
			}

			DestData += (DestStride - Mipmap.USize * sizeof(FColor)) / sizeof(FColor);
		}
	}

	if(!GIsEditor && !Texture->bParametric)
		Mipmap.DataArray.Unload();

	unguardf((TEXT("%s"),Texture->GetPathName()));
}

/*-----------------------------------------------------------------------------
	FStaticCubemap.
-----------------------------------------------------------------------------*/

FStaticCubemap::FStaticCubemap(UCubemap* InCubemap)
{
	Cubemap = InCubemap;
	CacheId = MakeCacheID(CID_RenderTexture);
	LastRevision = 1;
}

// Each face in one of these formats is converted to RGBA8 from its own format.
static UBOOL ExpandsToRGBA8( ETextureFormat Format )
{
	return Format == TEXF_P8 || Format == TEXF_RGBA8 || Format == TEXF_G16;
}

FTexture* FStaticCubemap::GetFace( INT Face )
{
	// A face may itself be a cubemap, whose interface is no FTexture.
	UBitmapMaterial* Current = Cubemap->Faces[Face] ? Cubemap->Faces[Face]->Get(Cubemap->GetLastUpdateTime(),NULL) : NULL;
	FBaseTexture* Base = Current ? Current->GetRenderInterface() : NULL;
	FTexture* Texture = Base ? Base->GetTextureInterface() : NULL;
	if( !Texture )
		return NULL;
	// Renderers size every face from face 0.
	ETextureFormat Format = Texture->GetFormat(), Expected = GetFormat();
	if( Texture->GetWidth() != GetWidth() || Texture->GetHeight() != GetHeight() || (Format != Expected && !(ExpandsToRGBA8(Format) && ExpandsToRGBA8(Expected))) )
		return NULL;
	return Texture;
}

QWORD FStaticCubemap::GetCacheId()
{
	return CacheId;
}

INT FStaticCubemap::GetRevision()
{
	if(Cubemap->bRealtimeChanged)
	{
		LastRevision++;
		Cubemap->bRealtimeChanged = 0;
	}

	for(INT FaceIndex = 0;FaceIndex < 6;FaceIndex++)
	{
		if(Cubemap->Faces[FaceIndex] && Cubemap->Faces[FaceIndex]->bRealtimeChanged)
		{
			LastRevision++;
			Cubemap->Faces[FaceIndex]->bRealtimeChanged = 0;
		}
	}
	return LastRevision;
}

INT FStaticCubemap::GetWidth()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetWidth();
	else
		return 4;
}

INT FStaticCubemap::GetHeight()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetHeight();
	else
		return 4;
}

INT FStaticCubemap::GetNumMips()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetNumMips();
	else
		return 1;
}

INT FStaticCubemap::GetFirstMip()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetFirstMip();
	else
		return 0;
}

ETextureFormat FStaticCubemap::GetFormat()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetFormat();
	else
		return TEXF_RGBA8;
}

ETexClampMode FStaticCubemap::GetUClamp()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetUClamp();
	else
		return TC_Wrap;
}

ETexClampMode FStaticCubemap::GetVClamp()
{
	if(Cubemap->Faces[0])
		return FStaticTexture(Cubemap->Faces[0]).GetVClamp();
	else
		return TC_Wrap;
}

/*-----------------------------------------------------------------------------
	UCubemap.
-----------------------------------------------------------------------------*/

UCubemap::UCubemap()
: UTexture()
{
	guard(UCubemap::UCubemap);
	CubemapRenderInterface = new FStaticCubemap(this);
	unguard;
}

FBaseTexture* UCubemap::GetRenderInterface() 
{ 
	return CubemapRenderInterface; 
}

void UCubemap::Destroy()
{
	guard(UCubemap::Destroy);
	delete CubemapRenderInterface;
	CubemapRenderInterface = NULL;
	Super::Destroy();
	unguard;
}

IMPLEMENT_CLASS(UCubemap);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

