#ifndef X_FORCE_FEEDBACK_H
#define X_FORCE_FEEDBACK_H

extern void InitForceFeedback(void* hInstance, void* hWnd);
extern void ExitForceFeedback();
extern void PlayFeedbackEffect( const char* EffectName );
extern void StopFeedbackEffect( const char* EffectName ); // Pass NULL to stop all
extern void ChangeSpringFeedbackEffect( const char* EffectName, int CenterX, int CenterY );
extern void ChangeBaseParamsForceFeedback( const char* EffectName, int DirectionX, int DirectionY, unsigned int Gain );
extern void ProjectileFeedback( class AProjectile* Projectile );

extern int GForceFeedbackAvailable;

// Set by the viewport driver, strengths are 0 to 1.
ENGINE_API extern void (*GRumble)( FLOAT Low, FLOAT High, INT Milliseconds );

#endif  //X_FORCE_FEEDBACK_H
