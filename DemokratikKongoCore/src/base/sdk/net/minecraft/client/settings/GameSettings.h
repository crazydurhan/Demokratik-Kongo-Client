#pragma once
#include "../../../../java/IClass.h"

struct CGameSettings : IClass
{
	CGameSettings();

	jclass GetClass();
	jobject GetInstance();

	int GetThirdPersonView();
	float GetFOV();
	float GetGamma();
	float GetMouseSensitivity();
	void SetGamma(float value);
	void SetFOV(float value);
	void SetThirdPersonView(int view);
	void SetFullscreenKeyToNull();
	void RestoreFullscreenKey();
};

