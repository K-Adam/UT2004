#include "EnginePrivate.h"
#include "xForceFeedback.h"

int GForceFeedbackAvailable = 0;
void (*GRumble)( FLOAT Low, FLOAT High, INT Milliseconds ) = NULL;

void InitForceFeedback(void* hInstance, void* hWnd) { GForceFeedbackAvailable = GRumble != NULL; }
void ExitForceFeedback() { GForceFeedbackAvailable = 0; }

// The effect names belong to Immersion's force feedback mice, so they map to a few rumbles by kind.
// Script checks the player's weapon, pickup and vehicle options before it plays one.
// A vehicle's centring spring is a steady pull, so it stays silent.
void PlayFeedbackEffect( const char* EffectName )
{
	if( !GRumble || !GForceFeedbackAvailable || !EffectName || !*EffectName )
		return;
	if( !strcmp( EffectName, "Damage" ) )
		GRumble( 0.5f, 0.8f, 150 );
	else if( strstr( EffectName, "StartUp" ) || strstr( EffectName, "ShutDown" ) )
		GRumble( 0.5f, 0.2f, 300 );
	else if( !strstr( EffectName, "Spring" ) )
		GRumble( 0.2f, 0.4f, 80 );
}

void StopFeedbackEffect( const char* EffectName ) {}
void ChangeBaseParamsForceFeedback( const char* EffectName, int DirectionX, int DirectionY, unsigned int Gain ) {}
void ChangeSpringFeedbackEffect( const char* EffectName, int CenterX, int CenterY ) {}

// Script has no explosion feedback, so a splash damage projectile going away near the view rumbles.
// Also fires for a projectile that expires without exploding, an explode hook would be exact.
void ProjectileFeedback( AProjectile* Projectile )
{
	if( !GForceFeedbackAvailable || Projectile->Damage <= 0.f || Projectile->DamageRadius <= 0.f )
		return;
	UClient* Client = Projectile->XLevel->Engine->Client;
	APlayerController* PC = Client && Client->Viewports.Num() ? Client->Viewports(0)->Actor : NULL;
	if( !PC || PC->XLevel != Projectile->XLevel || !PC->bEnableDamageForceFeedback )
		return;
	AActor* View = PC->ViewTarget ? PC->ViewTarget : PC;
	FLOAT Strength = 1.f - FDist( Projectile->Location, View->Location ) / (2.f * Projectile->DamageRadius);
	if( Strength > 0.f )
		GRumble( Strength, 0.6f * Strength, 250 );
}
