#include "xForceFeedback.h"

// Immersion's IFC was a DirectInput-only library, so force feedback is unavailable.
int GForceFeedbackAvailable = 0;

void InitForceFeedback(void* hInstance, void* hWnd) {}
void ExitForceFeedback() {}
void PlayFeedbackEffect( const char* EffectName ) {}
void StopFeedbackEffect( const char* EffectName ) {}
void ChangeBaseParamsForceFeedback( const char* EffectName, int DirectionX, int DirectionY, unsigned int Gain ) {}
void ChangeSpringFeedbackEffect( const char* EffectName, int CenterX, int CenterY ) {}
