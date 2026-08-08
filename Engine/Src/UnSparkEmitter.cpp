/*=============================================================================
	UnSparkEmitter.cpp: Unreal Spark Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	USparkEmitter.
-----------------------------------------------------------------------------*/

//!! TODO: TimeBeforeVisible?!

IMPLEMENT_CLASS(USparkEmitter);

void USparkEmitter::PostEditChange()
{
	Super::PostEditChange();
	CleanUp();
	Initialize( MaxParticles );
}

// Initializes the Emitter.
void USparkEmitter::Initialize( INT InMaxParticles )
{
	guard(USparkEmitter::Initialize);
	Super::Initialize( InMaxParticles );
	NumSegments = (INT)Max( 2.f, LineSegmentsRange.GetMax() );
	VerticesPerParticle		= 2 * NumSegments;
	IndicesPerParticle		= 2 * NumSegments;
	PrimitivesPerParticle	= NumSegments;
	SparkData.Add( InMaxParticles );
	
	unguard;
}

// Update particles.
INT USparkEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(USparkEmitter::UpdateParticles);
	INT Value	= Super::UpdateParticles( DeltaTime );
	BoundingBox = BoundingBox.ExpandBy( MaxSizeScale );
	return Value;
	unguard;
}

// CleanUp.
void USparkEmitter::CleanUp()
{
	guard(USparkEmitter::CleanUp);
	Super::CleanUp();
	SparkData.Empty();
	unguard;
}

void USparkEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(USparkEmitter::SpawnParticle);
	Super::SpawnParticle( Index, SpawnTime, Flags, SpawnFlags, LocalLocationOffset );
	SparkData(Index).TimeBeforeVisible	 = TimeBeforeVisibleRange.GetRand();
	SparkData(Index).TimeBetweenSegments = TimeBetweenSegmentsRange.GetRand(); 
	SparkData(Index).StartLocation		 = Particles(Index).Location;
	SparkData(Index).StartVelocity		 = Particles(Index).Velocity;
	unguard;
}


#pragma pack(push,1)

struct FSparkVertex
{
	FVector	Position GCC_PACK(1);
	FColor	Diffuse GCC_PACK(1);
	FLOAT	U GCC_PACK(1);
	FLOAT	V GCC_PACK(1);

	static INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Color;
		OutComponents[1].Function = FVF_Diffuse;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;

		return 3;
	}
};

#pragma pack(pop)


class FSparkVertexStream : public FParticleVertexStream<FSparkVertex>
{
public:

	USparkEmitter*				Emitter;
	INT							NumParticles;

	// Constructor.
	FSparkVertexStream(USparkEmitter* InEmitter,INT InNumParticles)
	{
		Emitter = InEmitter;
		NumParticles = InNumParticles;
	}

	virtual INT GetSize()
	{
		return NumParticles * Emitter->VerticesPerParticle * sizeof(FSparkVertex);
	}

	// Generates the spark vertices procedurally.
	virtual void GetStreamData(void* Dest)
	{
		FSparkVertex*	Vertices = (FSparkVertex*) Dest;
		INT				VertexIndex = 0;
		FLOAT			RNumSegments = 1.0f / Emitter->NumSegments;

		for(INT ParticleIndex = 0;ParticleIndex < Emitter->ActiveParticles;ParticleIndex++)
		{
			FParticle*	Particle = &Emitter->Particles(ParticleIndex);

			if(!(Particle->Flags & PTF_Active))
				continue;

			for(INT SegmentIndex = 0;SegmentIndex < Emitter->NumSegments;SegmentIndex++)
			{
				FLOAT	t		 = Max( 0.f, Particle->Time - SegmentIndex * Emitter->SparkData(ParticleIndex).TimeBetweenSegments );
				FVector	Location = Emitter->SparkData(ParticleIndex).StartLocation + Emitter->SparkData(ParticleIndex).StartVelocity*t + Emitter->Acceleration*t*t;

				t				 = Max( 0.f, Particle->Time - (SegmentIndex+1) * Emitter->SparkData(ParticleIndex).TimeBetweenSegments );
				FVector	Location2= Emitter->SparkData(ParticleIndex).StartLocation + Emitter->SparkData(ParticleIndex).StartVelocity*t + Emitter->Acceleration*t*t;
				FColor  Color    = Particle->Color.RenderColor();
				
				Vertices[VertexIndex].Position	= Location;
				Vertices[VertexIndex].Diffuse	= Color;
				Vertices[VertexIndex].U			= 0;
				Vertices[VertexIndex].V			= RNumSegments * SegmentIndex;
				VertexIndex++;

				Vertices[VertexIndex].Position	= Location2;
				Vertices[VertexIndex].Diffuse	= Color;
				Vertices[VertexIndex].U			= 1.f;
				Vertices[VertexIndex].V			= RNumSegments * SegmentIndex;
				VertexIndex++;
			}
		}
		
	}
};

// Renders all active particles
INT USparkEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(USparkEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	if ( Texture == NULL )
		return 0;

	INT RenderedParticles = 0;

	// Render the particles.
	if ( ActiveParticles > 0 )
	{
		// Calculate the number of active particles.
		for(INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++)
		{
			FParticle* Particle = &Particles(ParticleIndex);

			if(Particle->Flags & PTF_Active)
				RenderedParticles++;
		}

		// Bail out if nothing to render.
		if (!RenderedParticles)
			return 0;

		// Create the dynamic vertex stream and index buffer.
		FSparkVertexStream SparkVertices(this,RenderedParticles);

		// Rotate & translate if needed.
		if ( CoordinateSystem == PTCS_Relative )
		{
			FMatrix	LocalToWorld = FRotationMatrix(Owner->Rotation) * FTranslationMatrix(Owner->Location);

			RI->SetTransform(TT_LocalToWorld,LocalToWorld);
		} 
		else
			RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

		// Set texture and blending.
		Owner->ParticleMaterial->ParticleBlending			= DrawStyle;
		if( SceneNode->Viewport->Actor && SceneNode->Viewport->Actor->RendMap == REN_LightingOnly )
			Owner->ParticleMaterial->BitmapMaterial			= NULL;
		else
			Owner->ParticleMaterial->BitmapMaterial			= Texture;
		Owner->ParticleMaterial->BlendBetweenSubdivisions	= 0;
		Owner->ParticleMaterial->RenderTwoSided				= 1;
		Owner->ParticleMaterial->UseTFactor					= 0;
		Owner->ParticleMaterial->AlphaTest					= AlphaTest;
		Owner->ParticleMaterial->AlphaRef					= AlphaRef;
		Owner->ParticleMaterial->AcceptsProjectors			= AcceptsProjectors;
		Owner->ParticleMaterial->ZTest						= ZTest;
		Owner->ParticleMaterial->ZWrite						= ZWrite;
		Owner->ParticleMaterial->Wireframe					= SceneNode->Viewport->IsWire();
		
		RI->EnableLighting(0,1);
		RI->SetMaterial(Owner->ParticleMaterial);

		// Set the particle vertex stream and index buffer.
		// The particle vertices aren't actually generated until now.
		INT	BaseVertexIndex = RI->SetDynamicStream(VS_FixedFunction,&SparkVertices);

		RI->SetIndexBuffer(NULL,0);

		// Draw the particles.
		RI->DrawPrimitive(
			PT_LineList,
			BaseVertexIndex,
			RenderedParticles * PrimitivesPerParticle
			);
	}

	return RenderedParticles;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

