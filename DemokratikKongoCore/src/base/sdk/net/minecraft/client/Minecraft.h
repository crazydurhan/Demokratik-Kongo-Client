#pragma once

#include "../../../java/IClass.h"
#include "../client/entity/EntityPlayerSP.h"
#include "multiplayer/WorldClient.h"
#include "renderer/ActiveRenderInfo.h"
#include "renderer/entity/RenderManager.h"
#include "../util/Timer.h"
#include "settings/GameSettings.h"
#include "../util/MovingObjectPosition.h"

struct CMinecraft : IClass
{
	CMinecraft();
	~CMinecraft();

	jclass GetClass();
	jobject GetInstance();
	bool IsReady() const;

	jobject GetThePlayerObject();
	jobject GetTheWorldObject();
	jobject GetPlayerControllerObject();

	CEntity GetRenderViewEntity();
	bool IsInGuiState();
	bool HasInGameFocus() const;
	void ClickMouse();
	void RightClickMouse();
	void SyncCurrentPlayItem();
	CMovingObjectPosition GetMouseOver();
	void SetMouseOver(jobject value);
	int GetLeftClickCounter();
	void SetLeftClickCounter(int value);
	int GetRightClickDelayTimer();
	void SetRightClickDelayTimer(int value);

	CEntityPlayerSP* thePlayer;
	CWorldClient* theWorld;
	CActiveRenderInfo* activeRenderInfo;
	CRenderManager* renderManager;
	CTimer* timer;
	CGameSettings* gameSettings;

	bool mappingsValid = false;
};

