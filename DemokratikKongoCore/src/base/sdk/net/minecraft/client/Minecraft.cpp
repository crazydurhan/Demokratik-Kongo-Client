#include "Minecraft.h"
#include "../../../../java/java.h"
#include "../../../../util/logger.h"
#include "../../../../sdk/strayCache.h"
#include "../../../jniResolve.h"

#include "../entity/EntityLivingBase.h"

CMinecraft::CMinecraft()
{
	mappingsValid = false;

	if (!Java::AssignClass("net.minecraft.client.Minecraft", this->Class))
	{
		Logger::Err("[CMinecraft] Failed to resolve Minecraft class.");
		return;
	}

	this->MethodIDs["getMinecraft"] = StrayCache::minecraft_getMinecraft
		? StrayCache::minecraft_getMinecraft
		: JniResolve::StaticMethod(Java::GetEnv(), this->GetClass(), "()Lnet/minecraft/client/Minecraft;", "getMinecraft");

	jobject localInstance = nullptr;
	if (this->MethodIDs["getMinecraft"])
	{
		localInstance = Java::GetEnv()->CallStaticObjectMethod(this->GetClass(), this->MethodIDs["getMinecraft"]);
		JniResolve::ClearException(Java::GetEnv());
	}
	if (localInstance)
	{
		this->Instance = localInstance;
		Java::GetEnv()->DeleteLocalRef(localInstance);
	}

	this->FieldIDs["thePlayer"] = StrayCache::minecraft_thePlayer;
	this->FieldIDs["theWorld"] = StrayCache::minecraft_theWorld;
	this->FieldIDs["renderManager"] = StrayCache::minecraft_renderManager
		? StrayCache::minecraft_renderManager
		: JniResolve::Field(Java::GetEnv(), this->GetClass(), "Lnet/minecraft/client/renderer/entity/RenderManager;", "renderManager");
	this->FieldIDs["timer"] = StrayCache::minecraft_timer;
	this->FieldIDs["gameSettings"] = StrayCache::minecraft_gameSettings;
	this->FieldIDs["objectMouseOver"] = StrayCache::minecraft_objectMouseOver;
	this->FieldIDs["currentScreen"] = StrayCache::minecraft_currentScreen;
	this->FieldIDs["leftClickCounter"] = StrayCache::minecraft_leftClickCounter;
	this->FieldIDs["rightClickDelayTimer"] = StrayCache::minecraft_rightClickDelayTimer;
	this->FieldIDs["inGameHasFocus"] = StrayCache::minecraft_inGameHasFocus;
	this->MethodIDs["clickMouse"] = StrayCache::minecraft_clickMouse;
	this->MethodIDs["rightClickMouse"] = StrayCache::minecraft_rightClickMouse;
	this->MethodIDs["getRenderViewEntity"] = JniResolve::Method(Java::GetEnv(), this->GetClass(),
		"()Lnet/minecraft/entity/Entity;", "getRenderViewEntity");

	this->thePlayer = new CEntityPlayerSP();
	this->theWorld = new CWorldClient();
	this->activeRenderInfo = new CActiveRenderInfo();
	this->renderManager = new CRenderManager();
	this->timer = new CTimer();
	this->gameSettings = new CGameSettings();

	mappingsValid = IsReady();
	if (!mappingsValid)
		Logger::Err("[CMinecraft] Missing critical Minecraft field/method mappings.");
}

CMinecraft::~CMinecraft()
{
	delete thePlayer;
	delete theWorld;
	delete activeRenderInfo;
	delete renderManager;
	delete timer;
	delete gameSettings;
}

bool CMinecraft::IsReady() const
{
	return this->Class
		&& this->MethodIDs.count("getMinecraft") && this->MethodIDs.at("getMinecraft")
		&& this->FieldIDs.count("thePlayer") && this->FieldIDs.at("thePlayer")
		&& this->FieldIDs.count("theWorld") && this->FieldIDs.at("theWorld")
		&& this->FieldIDs.count("objectMouseOver") && this->FieldIDs.at("objectMouseOver")
		&& this->FieldIDs.count("leftClickCounter") && this->FieldIDs.at("leftClickCounter")
		&& this->MethodIDs.count("clickMouse") && this->MethodIDs.at("clickMouse");
}

jclass CMinecraft::GetClass()
{
	return this->Class;
}

jobject CMinecraft::GetInstance()
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !this->Class || !this->MethodIDs["getMinecraft"])
		return nullptr;

	if (!this->Instance.IsValid())
	{
		jobject localInstance = env->CallStaticObjectMethod(this->GetClass(), this->MethodIDs["getMinecraft"]);
		JniResolve::ClearException(env);
		if (localInstance)
		{
			this->Instance = localInstance;
			env->DeleteLocalRef(localInstance);
		}
	}
	return this->Instance;
}

jobject CMinecraft::GetThePlayerObject()
{
	JNIEnv* env = Java::GetEnv();
	jobject mc = GetInstance();
	jfieldID fid = FieldIDs["thePlayer"];
	if (!env || !mc || !fid) return nullptr;
	jobject player = env->GetObjectField(mc, fid);
	JniResolve::ClearException(env);
	return player;
}

jobject CMinecraft::GetTheWorldObject()
{
	JNIEnv* env = Java::GetEnv();
	jobject mc = GetInstance();
	jfieldID fid = FieldIDs["theWorld"];
	if (!env || !mc || !fid) return nullptr;
	jobject world = env->GetObjectField(mc, fid);
	JniResolve::ClearException(env);
	return world;
}

jobject CMinecraft::GetPlayerControllerObject()
{
	JNIEnv* env = Java::GetEnv();
	jobject mc = GetInstance();
	if (!env || !mc || !StrayCache::minecraft_playerController) return nullptr;
	jobject controller = env->GetObjectField(mc, StrayCache::minecraft_playerController);
	JniResolve::ClearException(env);
	return controller;
}

CEntity CMinecraft::GetRenderViewEntity()
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !MethodIDs["getRenderViewEntity"]) return CEntity((jobject)nullptr);
	return CEntity(env->CallObjectMethod(GetInstance(), MethodIDs["getRenderViewEntity"]));
}

bool CMinecraft::IsInGuiState()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["currentScreen"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return false;
	jobject screen = env->GetObjectField(mc, fid);
	JniResolve::ClearException(env);
	const bool open = screen != nullptr;
	if (screen) env->DeleteLocalRef(screen);
	return open;
}

bool CMinecraft::HasInGameFocus() const
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs.count("inGameHasFocus") ? FieldIDs.at("inGameHasFocus") : nullptr;
	jobject mc = const_cast<CMinecraft*>(this)->GetInstance();
	if (!env || !mc || !fid) return false;
	const jboolean focused = env->GetBooleanField(mc, fid);
	JniResolve::ClearException(env);
	return focused == JNI_TRUE;
}

void CMinecraft::ClickMouse()
{
	JNIEnv* env = Java::GetEnv();
	jmethodID mid = MethodIDs["clickMouse"];
	jobject mc = GetInstance();
	if (!env || !mc || !mid) return;
	env->CallVoidMethod(mc, mid);
	JniResolve::ClearException(env);
}

void CMinecraft::RightClickMouse()
{
	JNIEnv* env = Java::GetEnv();
	jmethodID mid = MethodIDs.count("rightClickMouse") ? MethodIDs.at("rightClickMouse") : nullptr;
	jobject mc = GetInstance();
	if (!env || !mc || !mid) return;
	env->CallVoidMethod(mc, mid);
	JniResolve::ClearException(env);
}

void CMinecraft::SyncCurrentPlayItem()
{
	JNIEnv* env = Java::GetEnv();
	jobject controller = GetPlayerControllerObject();
	if (!env || !controller || !StrayCache::playerControllerMP_syncCurrentPlayItem) return;
	env->CallVoidMethod(controller, StrayCache::playerControllerMP_syncCurrentPlayItem);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(controller);
}

CMovingObjectPosition CMinecraft::GetMouseOver()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["objectMouseOver"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return CMovingObjectPosition((jobject)nullptr);
	return CMovingObjectPosition(env->GetObjectField(mc, fid));
}

void CMinecraft::SetMouseOver(jobject value)
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["objectMouseOver"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return;
	env->SetObjectField(mc, fid, value);
	JniResolve::ClearException(env);
}

int CMinecraft::GetLeftClickCounter()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["leftClickCounter"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return 0;
	return env->GetIntField(mc, fid);
}

void CMinecraft::SetLeftClickCounter(int value)
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["leftClickCounter"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return;
	env->SetIntField(mc, fid, value);
	JniResolve::ClearException(env);
}

int CMinecraft::GetRightClickDelayTimer()
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["rightClickDelayTimer"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return 4;
	const int value = env->GetIntField(mc, fid);
	JniResolve::ClearException(env);
	return value;
}

void CMinecraft::SetRightClickDelayTimer(int value)
{
	JNIEnv* env = Java::GetEnv();
	jfieldID fid = FieldIDs["rightClickDelayTimer"];
	jobject mc = GetInstance();
	if (!env || !mc || !fid) return;
	env->SetIntField(mc, fid, value);
	JniResolve::ClearException(env);
}
