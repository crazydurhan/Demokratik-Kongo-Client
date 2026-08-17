#pragma once

#include "Entity.h"

struct CEntityLivingBase : CEntity
{
	CEntityLivingBase();
	CEntityLivingBase(jobject instance);

	jclass GetClass();
	jobject GetInstance();

	float GetHealth();
	float GetMaxHealth();
	float GetAbsorptionAmount();
	bool CanEntityBeSeen(jobject entity);
	void SwingItem();
	int GetJumpTicks();
	void SetJumpTicks(int value);
	bool IsPotionActive(int potionId);
	void RemovePotionEffect(int potionId);
};

