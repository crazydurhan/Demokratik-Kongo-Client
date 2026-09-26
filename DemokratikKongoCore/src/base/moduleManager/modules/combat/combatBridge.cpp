#include "combatBridge.h"

#include "../../commonData.h"
#include "friends.h"
#include "piercing.h"
#include "../../../menu/menu.h"
#include "../../../sdk/jniResolve.h"
#include "../../../patcher/patcher.h"
#include "../../../sdk/net/minecraft/client/entity/EntityPlayerSP.h"
#include "../../../sdk/strayCache.h"
#include "../../../java/java.h"
#include "../../../util/math/math.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include <Windows.h>

namespace
{
	inline long long nowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	int s_prevCrosshairHurtTime = 0;
	long long s_lastPlayerHitMs = 0;
	long long s_lastAttackInputMs = 0;

	constexpr long long kTargetingWindowMs = 2000;
	constexpr long long kAttackInputWindowMs = 700;

	std::atomic<int> s_pendingAttackClicks{ 0 };
	std::atomic<int> s_pendingUseClicks{ 0 };

	// Client actuation queue
	std::atomic<int> s_pendingBlock{ -1 }; // -1 none, 0 release, 1 press
	std::atomic<bool> s_moveKeysActive{ false };
	std::atomic<bool> s_moveKeysApplied{ false };
	std::atomic<bool> s_mkForward{ false };
	std::atomic<bool> s_mkBack{ false };
	std::atomic<bool> s_mkLeft{ false };
	std::atomic<bool> s_mkRight{ false };
	std::atomic<bool> s_mkJump{ false };
	std::atomic<bool> s_zeroMotionXZ{ false };
	std::atomic<int> s_pendingSneak{ -1 }; // -1 none, 0 release, 1 press
	std::atomic<int> s_pendingHotbar{ -1 };
	std::atomic<bool> s_zeroLeftClick{ false };
	std::atomic<int> s_pendingRightClickDelay{ -1 };
	std::atomic<bool> s_zeroJumpTicks{ false };
}

namespace
{
	struct KeyBindingCache
	{
		jclass  cls = nullptr;
		jmethodID setKeyBindState = nullptr;
		jmethodID onTick = nullptr;
		jmethodID getKeyCode = nullptr;
		jfieldID  keyBindAttack = nullptr;
		jfieldID  keyBindUseItem = nullptr;
		jfieldID  keyBindForward = nullptr;
		jfieldID  keyBindBack = nullptr;
		jfieldID  keyBindLeft = nullptr;
		jfieldID  keyBindRight = nullptr;
		jfieldID  keyBindJump = nullptr;
		jfieldID  keyBindSneak = nullptr;
	};

	KeyBindingCache& keys()
	{
		static KeyBindingCache k;
		return k;
	}

	bool ensureKeyBinding(JNIEnv* env)
	{
		KeyBindingCache& k = keys();
		if (k.cls && k.onTick && k.getKeyCode && k.keyBindAttack && k.keyBindUseItem)
			return true;
		if (!env) return false;

		if (!k.cls)
		{
			jclass local = nullptr;
			if (!Java::AssignClass("net.minecraft.client.settings.KeyBinding", local))
				return false;
			k.cls = local;
		}

		if (!k.setKeyBindState)
			k.setKeyBindState = JniResolve::StaticMethod(env, k.cls, "(IZ)V", "setKeyBindState");
		if (!k.onTick)
			k.onTick = JniResolve::StaticMethod(env, k.cls, "(I)V", "onTick");
		if (!k.getKeyCode)
			k.getKeyCode = JniResolve::Method(env, k.cls, "()I", "getKeyCode");

		if (SDK::Minecraft && SDK::Minecraft->gameSettings)
		{
			jclass gsClass = SDK::Minecraft->gameSettings->GetClass();
			if (gsClass)
			{
				if (!k.keyBindAttack)
					k.keyBindAttack = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindAttack");
				if (!k.keyBindUseItem)
					k.keyBindUseItem = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindUseItem");
				if (!k.keyBindForward)
					k.keyBindForward = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindForward");
				if (!k.keyBindBack)
					k.keyBindBack = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindBack");
				if (!k.keyBindLeft)
					k.keyBindLeft = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindLeft");
				if (!k.keyBindRight)
					k.keyBindRight = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindRight");
				if (!k.keyBindJump)
					k.keyBindJump = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindJump");
				if (!k.keyBindSneak)
					k.keyBindSneak = JniResolve::Field(env, gsClass,
						"Lnet/minecraft/client/settings/KeyBinding;", "keyBindSneak");
			}
		}

		return k.cls && k.onTick && k.getKeyCode && k.keyBindAttack && k.keyBindUseItem;
	}

	void applyKeyBindingPressed(JNIEnv* env, jobject keyBind, int keyCode, bool pressed)
	{
		if (!env || !keyBind) return;

		if (keys().setKeyBindState) {
			env->CallStaticVoidMethod(keys().cls, keys().setKeyBindState,
				keyCode, pressed ? JNI_TRUE : JNI_FALSE);
			JniResolve::ClearException(env);
		}

		jclass kbClass = env->GetObjectClass(keyBind);
		jfieldID fPressed = JniResolve::Field(env, kbClass, "Z", "pressed");
		if (!fPressed)
			fPressed = JniResolve::Field(env, kbClass, "Z", "field_74513_e");
		jfieldID fPressTime = JniResolve::Field(env, kbClass, "I", "pressTime");
		if (!fPressTime)
			fPressTime = JniResolve::Field(env, kbClass, "I", "field_151474_i");

		if (fPressed) {
			env->SetBooleanField(keyBind, fPressed, pressed ? JNI_TRUE : JNI_FALSE);
			JniResolve::ClearException(env);
		}
		if (pressed && fPressTime) {
			env->SetIntField(keyBind, fPressTime, 1);
			JniResolve::ClearException(env);
		}

		env->DeleteLocalRef(kbClass);
	}

	// Drives a movement-style keybind (forward/back/left/right/jump/sneak) by
	// GameSettings field. Only the pressed state is touched — pressTime stays
	// untouched so click-type handlers never see synthetic "clicks".
	void applyKeyBindStateByField(JNIEnv* env, jobject gameSettings, jfieldID keyBindField, bool pressed)
	{
		if (!env || !gameSettings || !keyBindField) return;

		jobject keyBind = env->GetObjectField(gameSettings, keyBindField);
		JniResolve::ClearException(env);
		if (!keyBind) return;

		if (keys().setKeyBindState && keys().getKeyCode) {
			const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
			JniResolve::ClearException(env);
			env->CallStaticVoidMethod(keys().cls, keys().setKeyBindState,
				keyCode, pressed ? JNI_TRUE : JNI_FALSE);
			JniResolve::ClearException(env);
		}

		// jfieldIDs are stable for the class lifetime — resolve once.
		static jfieldID fPressed = nullptr;
		if (!fPressed) {
			jclass kbClass = env->GetObjectClass(keyBind);
			fPressed = JniResolve::Field(env, kbClass, "Z", "pressed");
			if (!fPressed)
				fPressed = JniResolve::Field(env, kbClass, "Z", "field_74513_e");
			env->DeleteLocalRef(kbClass);
		}
		if (fPressed) {
			env->SetBooleanField(keyBind, fPressed, pressed ? JNI_TRUE : JNI_FALSE);
			JniResolve::ClearException(env);
		}

		env->DeleteLocalRef(keyBind);
	}

	bool fireAttackKeyBinding()
	{
		JNIEnv* env = Java::GetEnv();
		if (!env || !ensureKeyBinding(env) || !keys().keyBindAttack) return false;

		jobject gs = SDK::Minecraft->gameSettings->GetInstance();
		if (!gs) return false;

		jobject keyBind = env->GetObjectField(gs, keys().keyBindAttack);
		JniResolve::ClearException(env);
		if (!keyBind) return false;

		const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
		JniResolve::ClearException(env);

		applyKeyBindingPressed(env, keyBind, keyCode, true);
		SDK::Minecraft->SetLeftClickCounter(0);
		applyKeyBindingPressed(env, keyBind, keyCode, false);

		env->DeleteLocalRef(keyBind);
		return true;
	}

	// Vanilla use-item path: keyBindUseItem held, then Minecraft.rightClickMouse().
	bool fireUseItemKeyBinding()
	{
		JNIEnv* env = Java::GetEnv();
		if (!env || !ensureKeyBinding(env) || !keys().keyBindUseItem) return false;

		jobject gs = SDK::Minecraft->gameSettings->GetInstance();
		if (!gs) return false;

		jobject keyBind = env->GetObjectField(gs, keys().keyBindUseItem);
		JniResolve::ClearException(env);
		if (!keyBind) return false;

		const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
		JniResolve::ClearException(env);

		if (StrayCache::minecraft_rightClickDelayTimer)
			SDK::Minecraft->SetRightClickDelayTimer(0);

		applyKeyBindingPressed(env, keyBind, keyCode, true);
		SDK::Minecraft->RightClickMouse();
		applyKeyBindingPressed(env, keyBind, keyCode, false);

		env->DeleteLocalRef(keyBind);
		return true;
	}

	bool raySlab(float origin, float dir, double minB, double maxB, float& tMin, float& tMax)
	{
		if (std::fabs(dir) < 1e-8f)
			return origin >= (float)minB && origin <= (float)maxB;

		float invD = 1.0f / dir;
		float t0 = (float)(minB - origin) * invD;
		float t1 = (float)(maxB - origin) * invD;
		if (t0 > t1) std::swap(t0, t1);
		tMin = std::max(tMin, t0);
		tMax = std::min(tMax, t1);
		return tMin <= tMax;
	}

	bool rayIntersectsBox(const Vector3& origin, const Vector3& end, const BoundingBox& box, float& hitDist)
	{
		Vector3 delta = end - origin;
		const float maxLen = delta.Length();
		if (maxLen < 1e-4f) return false;

		Vector3 dir = delta * (1.0f / maxLen);
		float tMin = 0.0f;
		float tMax = maxLen;

		if (!raySlab(origin.x, dir.x, box.minX, box.maxX, tMin, tMax)) return false;
		if (!raySlab(origin.y, dir.y, box.minY, box.maxY, tMin, tMax)) return false;
		if (!raySlab(origin.z, dir.z, box.minZ, box.maxZ, tMin, tMax)) return false;

		if (tMin > maxLen || tMax < 0.0f) return false;
		hitDist = (tMin >= 0.0f) ? tMin : tMax;
		return hitDist >= 0.0f && hitDist <= maxLen;
	}

	BoundingBox expandBox(const BoundingBox& bb, float border)
	{
		return BoundingBox{
			bb.minX - border, bb.minY - border, bb.minZ - border,
			bb.maxX + border, bb.maxY + border, bb.maxZ + border
		};
	}

	BoundingBox expandHitbox(const BoundingBox& bb)
	{
		constexpr float kRayPadding = 0.1f;
		return BoundingBox{
			bb.minX - kRayPadding, bb.minY - kRayPadding, bb.minZ - kRayPadding,
			bb.maxX + kRayPadding, bb.maxY + kRayPadding, bb.maxZ + kRayPadding
		};
	}

	void setLwjglMouseButton(JNIEnv* env, int button, bool down)
	{
		static jclass bridgeClass = nullptr;
		static jmethodID mid = nullptr;
		if (!bridgeClass) {
			jclass local = nullptr;
			if (Java::AssignClass("io.github.lefraudeur.RuntimeBridge", local) && local)
				bridgeClass = local;
		}
		if (bridgeClass && !mid) {
			mid = env->GetStaticMethodID(bridgeClass, "setLwjglMouseButton", "(IZ)V");
			if (env->ExceptionCheck()) env->ExceptionClear();
		}
		if (!bridgeClass || !mid) return;
		env->CallStaticVoidMethod(bridgeClass, mid, (jint)button, down ? JNI_TRUE : JNI_FALSE);
		JniResolve::ClearException(env);
	}
}

bool CombatBridge::CanCombat()
{
	return StrayCache::IsReady()
		&& Java::Initialized
		&& Java::GetEnv()
		&& SDK::Minecraft
		&& SDK::Minecraft->IsReady()
		&& !Menu::Open;
}

bool CombatBridge::InGame()
{
	if (!CanCombat()) return false;
	if (!CommonData::SanityCheck()) return false;
	if (SDK::Minecraft->IsInGuiState()) return false;
	return true;
}

namespace
{
	HWND resolveMcWindow()
	{
		if (Menu::HandleWindow && IsWindow(Menu::HandleWindow))
			return Menu::HandleWindow;

		HWND hwnd = FindWindowA("LWJGL", nullptr);
		if (!hwnd)
			hwnd = GetActiveWindow();
		return hwnd;
	}

	LPARAM cursorClientLParam(HWND hwnd)
	{
		POINT pt{};
		GetCursorPos(&pt);
		ScreenToClient(hwnd, &pt);
		return MAKELPARAM(pt.x, pt.y);
	}
}

void CombatBridge::PostLeftButton(bool pressed)
{
	HWND hwnd = resolveMcWindow();
	if (!hwnd)
		return;

	const LPARAM lp = cursorClientLParam(hwnd);
	if (pressed)
		PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
	else
		PostMessageA(hwnd, WM_LBUTTONUP, 0, lp);
}

void CombatBridge::PostLeftClick()
{
	PostLeftButton(true);
	PostLeftButton(false);
}

void CombatBridge::PostRightButton(bool pressed)
{
	HWND hwnd = resolveMcWindow();
	if (!hwnd)
		return;

	const LPARAM lp = cursorClientLParam(hwnd);
	if (pressed)
		PostMessageA(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lp);
	else
		PostMessageA(hwnd, WM_RBUTTONUP, 0, lp);
}

void CombatBridge::PostRightClick()
{
	PostRightButton(true);
	PostRightButton(false);
}

void CombatBridge::SelectHotbarSlot(int slot)
{
	if (slot < 0) slot = 0;
	if (slot > 8) slot = 8;

	HWND hwnd = resolveMcWindow();
	if (!hwnd)
		return;

	const WPARAM vk = static_cast<WPARAM>('1' + slot);
	PostMessageA(hwnd, WM_KEYDOWN, vk, 1);
	PostMessageA(hwnd, WM_KEYUP, vk, 0xC0000001);
}

void CombatBridge::LeftClick()
{
	// Inventory / GUI path: OS post works without an in-world client tick.
	if (!InGame())
	{
		PostLeftClick();
		return;
	}

	// In-world clicks must run on the Minecraft client thread.
	s_pendingAttackClicks.fetch_add(1, std::memory_order_relaxed);
}

void CombatBridge::SetLeftClickState(bool pressed)
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !ensureKeyBinding(env) || !SDK::Minecraft || !SDK::Minecraft->gameSettings) return;

	jobject gs = SDK::Minecraft->gameSettings->GetInstance();
	if (!gs) return;

	jobject keyBind = env->GetObjectField(gs, keys().keyBindAttack);
	JniResolve::ClearException(env);
	if (!keyBind) return;

	jclass kbClass = env->GetObjectClass(keyBind);
	jfieldID fPressed = JniResolve::Field(env, kbClass, "Z", "pressed");
	if (!fPressed) {
		fPressed = JniResolve::Field(env, kbClass, "Z", "field_74513_e");
	}
	jfieldID fPressTime = JniResolve::Field(env, kbClass, "I", "pressTime");
	if (!fPressTime) {
		fPressTime = JniResolve::Field(env, kbClass, "I", "field_151474_i");
	}

	if (fPressed) {
		env->SetBooleanField(keyBind, fPressed, pressed ? JNI_TRUE : JNI_FALSE);
		JniResolve::ClearException(env);
	}
	if (pressed && fPressTime) {
		env->SetIntField(keyBind, fPressTime, 1);
		JniResolve::ClearException(env);
	}

	// Update KeyBinding static state if pressed
	if (keys().setKeyBindState) {
		const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
		JniResolve::ClearException(env);
		env->CallStaticVoidMethod(keys().cls, keys().setKeyBindState, keyCode, pressed ? JNI_TRUE : JNI_FALSE);
		JniResolve::ClearException(env);
	}

	env->DeleteLocalRef(kbClass);
	env->DeleteLocalRef(keyBind);

	// OS Message Queue injection mapping
	HWND mcHwnd = FindWindowA("LWJGL", nullptr);
	if (!mcHwnd) {
		mcHwnd = GetActiveWindow();
	}
	if (mcHwnd) {
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(mcHwnd, &pt);
		if (pressed) {
			PostMessageA(mcHwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
		} else {
			PostMessageA(mcHwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
		}
	}
}

void CombatBridge::RightClick()
{
	if (!InGame())
	{
		PostRightClick();
		return;
	}

	s_pendingUseClicks.fetch_add(1, std::memory_order_relaxed);
}

void CombatBridge::SetRightClickState(bool pressed)
{
	JNIEnv* env = Java::GetEnv();
	if (!env || !ensureKeyBinding(env) || !keys().keyBindUseItem) return;
	if (!SDK::Minecraft || !SDK::Minecraft->gameSettings) return;

	jobject gs = SDK::Minecraft->gameSettings->GetInstance();
	if (!gs) return;

	jobject keyBind = env->GetObjectField(gs, keys().keyBindUseItem);
	JniResolve::ClearException(env);
	if (!keyBind) return;

	const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
	JniResolve::ClearException(env);

	applyKeyBindingPressed(env, keyBind, keyCode, pressed);

	if (pressed)
	{
		// Fire rightClickMouse() once so the item-in-use (block) actually
		// starts.  Vanilla keeps blocking while keyBindUseItem.pressed
		// stays true, and stops it when pressed goes false.
		SDK::Minecraft->RightClickMouse();
	}

	env->DeleteLocalRef(keyBind);
}

void CombatBridge::AttackEntity(jobject targetEntity)
{
	if (!InGame() || !targetEntity) return;

	JNIEnv* env = Java::GetEnv();
	if (!env || !StrayCache::playerControllerMP_attackEntity) return;

	jobject player = SDK::Minecraft->GetThePlayerObject();
	jobject controller = SDK::Minecraft->GetPlayerControllerObject();
	if (!player || !controller)
	{
		if (player) env->DeleteLocalRef(player);
		if (controller) env->DeleteLocalRef(controller);
		return;
	}

	// Defensive self-check: never let a self-attack packet leave the client.
	// Without this the server kicks with "Cannot interact with self" if the
	// caller (e.g. autoclicker) ever forwards the local player as the target.
	if (env->IsSameObject(targetEntity, player) == JNI_TRUE)
	{
		env->DeleteLocalRef(player);
		env->DeleteLocalRef(controller);
		return;
	}

	env->CallVoidMethod(controller, StrayCache::playerControllerMP_attackEntity, player, targetEntity);
	JniResolve::ClearException(env);

	if (StrayCache::entityLivingBase_swingItem)
	{
		env->CallVoidMethod(player, StrayCache::entityLivingBase_swingItem);
		JniResolve::ClearException(env);
	}

	env->DeleteLocalRef(player);
	env->DeleteLocalRef(controller);
}

void CombatBridge::SetReach(float blocks)
{
	Patcher::put("reach_distance", std::to_string(blocks));
}

void CombatBridge::ApplyVelocity(float horizontalPct, float verticalPct)
{
	if (!InGame()) return;

	JNIEnv* env = Java::GetEnv();
	jobject player = SDK::Minecraft->GetThePlayerObject();
	if (!env || !player || !StrayCache::entityLivingBase_hurtTime) return;

	int hurtTime = env->GetIntField(player, StrayCache::entityLivingBase_hurtTime);
	if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(player); return; }

	if (hurtTime < 1 || hurtTime > 10) { env->DeleteLocalRef(player); return; }

	if (!StrayCache::entity_motionX || !StrayCache::entity_motionY || !StrayCache::entity_motionZ)
	{
		env->DeleteLocalRef(player);
		return;
	}

	double h = horizontalPct / 100.0;
	double v = verticalPct / 100.0;
	double mx = env->GetDoubleField(player, StrayCache::entity_motionX);
	double my = env->GetDoubleField(player, StrayCache::entity_motionY);
	double mz = env->GetDoubleField(player, StrayCache::entity_motionZ);
	if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(player); return; }

	env->SetDoubleField(player, StrayCache::entity_motionX, mx * h);
	env->SetDoubleField(player, StrayCache::entity_motionY, my * v);
	env->SetDoubleField(player, StrayCache::entity_motionZ, mz * h);
	JniResolve::ClearException(env);
	env->DeleteLocalRef(player);
}

bool CombatBridge::CrosshairOnEntity()
{
	if (!InGame()) return false;
	CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
	return mop.GetInstance() && mop.IsTypeOfEntity();
}

jobject CombatBridge::CrosshairEntity()
{
	if (!CrosshairOnEntity()) return nullptr;
	CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
	CEntity entity = mop.GetEntity();
	jobject inst = entity.GetInstance();
	if (!inst) return nullptr;
	JNIEnv* env = Java::GetEnv();
	if (!env) return nullptr;
	// NewLocalRef: temporary CEntity's global ref dies when this returns.
	return env->NewLocalRef(inst);
}

bool CombatBridge::CrosshairOnFriend()
{
	if (!InGame() || !SDK::Minecraft) return false;
	CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
	if (!mop.GetInstance() || !mop.IsTypeOfEntity()) return false;

	CEntity entity = mop.GetEntity();
	jobject entityObj = entity.GetInstance();
	if (!entityObj) return false;

	std::string name = entity.GetName();
	return Friends::IsFriend(name);
}

void CombatBridge::RefreshMouseOverPost()
{
	if (!InGame() || !Piercing::IsActive()) return;

	JNIEnv* env = Java::GetEnv();
	if (!env || !StrayCache::EnsureEspBridge() || !StrayCache::espBridge_onGetMouseOverPost)
		return;

	float partial = 0.0f;
	if (SDK::Minecraft && SDK::Minecraft->timer)
		partial = SDK::Minecraft->timer->GetRenderPartialTicks();

	env->CallStaticVoidMethod(StrayCache::espBridge_class,
		StrayCache::espBridge_onGetMouseOverPost, partial);
	JniResolve::ClearException(env);
}

void CombatBridge::SuppressHeldAttackInput()
{
	if (!InGame()) return;

	JNIEnv* env = Java::GetEnv();
	if (!env) return;

	setLwjglMouseButton(env, 0, false);

	if (!ensureKeyBinding(env) || !SDK::Minecraft || !SDK::Minecraft->gameSettings)
		return;

	jobject gs = SDK::Minecraft->gameSettings->GetInstance();
	if (!gs) return;

	jobject keyBind = env->GetObjectField(gs, keys().keyBindAttack);
	JniResolve::ClearException(env);
	if (!keyBind) return;

	const int keyCode = env->CallIntMethod(keyBind, keys().getKeyCode);
	JniResolve::ClearException(env);

	applyKeyBindingPressed(env, keyBind, keyCode, false);

	env->DeleteLocalRef(keyBind);
}

void CombatBridge::PulseAttackClick()
{
	if (!InGame() || !SDK::Minecraft) return;

	JNIEnv* env = Java::GetEnv();
	if (!env) return;

	// Only refresh the Java-side raycast when piercing is active AND there's
	// actually a friend under the crosshair.  Without this guard every click
	// re-runs EntityRenderer.getMouseOver() through JNI — even during normal
	// 1v1 combat where no friend is in the way — which desyncs the target and
	// causes hits to miss (the "getting combo'd" symptom).
	if (Piercing::IsActive() && CrosshairOnFriend())
		RefreshMouseOverPost();

	// Final self-check before we ask the game to call clickMouse().  Without
	// this guard, if objectMouseOver somehow resolved to the local player
	// (e.g. stale ref, Lunar wrapper) the game will happily send a
	// C02PacketUseEntity(self) and the server kicks "Cannot interact with
	// self".  Better to skip the click than to eat a kick.
	if (Piercing::IsActive() || CrosshairOnEntity())
	{
		jobject localPlayer = SDK::Minecraft->GetThePlayerObject();
		if (localPlayer)
		{
			CMovingObjectPosition mop = SDK::Minecraft->GetMouseOver();
			jobject targetEntity = mop.GetEntity().GetInstance();
			const bool targetIsSelf = targetEntity
				&& env->IsSameObject(targetEntity, localPlayer) == JNI_TRUE;
			env->DeleteLocalRef(localPlayer);
			if (targetIsSelf)
				return;
		}
	}

	if (!fireAttackKeyBinding())
	{
		SDK::Minecraft->SetLeftClickCounter(0);
		SDK::Minecraft->ClickMouse();
	}
	setLwjglMouseButton(env, 0, false);
}

void CombatBridge::DrainPendingInputs()
{
	int attacks = s_pendingAttackClicks.exchange(0, std::memory_order_acq_rel);
	int uses = s_pendingUseClicks.exchange(0, std::memory_order_acq_rel);

	// Cap so a stalled client thread can't dump a huge backlog in one tick.
	if (attacks > 4) attacks = 4;
	if (uses > 4) uses = 4;

	for (int i = 0; i < attacks; ++i)
		PulseAttackClick();

	for (int i = 0; i < uses; ++i)
	{
		if (!InGame())
			break;
		fireUseItemKeyBinding();
	}

	const int block = s_pendingBlock.exchange(-1, std::memory_order_acq_rel);
	if (block == 0 || block == 1)
		SetRightClickState(block == 1);

	// InvMove: drive the real movement keybinds. Writing moveForward/moveStrafing
	// fields directly never worked — EntityPlayerSP.onLivingUpdate overwrites
	// them from movementInput every tick, and vanilla only refreshes keybind
	// states from hardware while no screen is open. Setting the keybind pressed
	// states makes updatePlayerMoveState() pick the movement up natively.
	// Deliberately NOT gated by InGame(): the whole point is moving while a
	// GUI screen is open (IsInGuiState() == true there).
	const bool mkActive = s_moveKeysActive.load(std::memory_order_acquire);
	if ((mkActive || s_moveKeysApplied.load(std::memory_order_acquire))
		&& SDK::Minecraft && SDK::Minecraft->gameSettings)
	{
		JNIEnv* env = Java::GetEnv();
		jobject gs = SDK::Minecraft->gameSettings->GetInstance();
		if (env && gs && ensureKeyBinding(env))
		{
			applyKeyBindStateByField(env, gs, keys().keyBindForward, mkActive && s_mkForward.load(std::memory_order_acquire));
			applyKeyBindStateByField(env, gs, keys().keyBindBack,    mkActive && s_mkBack.load(std::memory_order_acquire));
			applyKeyBindStateByField(env, gs, keys().keyBindLeft,    mkActive && s_mkLeft.load(std::memory_order_acquire));
			applyKeyBindStateByField(env, gs, keys().keyBindRight,   mkActive && s_mkRight.load(std::memory_order_acquire));
			applyKeyBindStateByField(env, gs, keys().keyBindJump,    mkActive && s_mkJump.load(std::memory_order_acquire));
			s_moveKeysApplied.store(mkActive, std::memory_order_release);
		}
		if (gs) env->DeleteLocalRef(gs);
	}

	// SafeWalk sneak press. Release must apply even when InGame() is false
	// (e.g. a screen opened while edge-sneaking) or the key would stick.
	const int sneak = s_pendingSneak.exchange(-1, std::memory_order_acq_rel);
	if ((sneak == 0 || sneak == 1) && SDK::Minecraft && SDK::Minecraft->gameSettings)
	{
		JNIEnv* env = Java::GetEnv();
		jobject gs = SDK::Minecraft->gameSettings->GetInstance();
		if (env && gs && ensureKeyBinding(env))
			applyKeyBindStateByField(env, gs, keys().keyBindSneak, sneak == 1);
		if (gs) env->DeleteLocalRef(gs);
	}

	if (s_zeroMotionXZ.exchange(false, std::memory_order_acq_rel) && InGame() && SDK::Minecraft)
	{
		JNIEnv* env = Java::GetEnv();
		jobject player = SDK::Minecraft->GetThePlayerObject();
		if (env && player)
		{
			if (StrayCache::entity_motionX) env->SetDoubleField(player, StrayCache::entity_motionX, 0.0);
			if (StrayCache::entity_motionZ) env->SetDoubleField(player, StrayCache::entity_motionZ, 0.0);
			JniResolve::ClearException(env);
			env->DeleteLocalRef(player);
		}
	}

	const int hotbar = s_pendingHotbar.exchange(-1, std::memory_order_acq_rel);
	if (hotbar >= 0 && hotbar <= 8)
		SelectHotbarSlot(hotbar);

	const int rcd = s_pendingRightClickDelay.exchange(-1, std::memory_order_acq_rel);
	if (rcd >= 0 && InGame() && SDK::Minecraft)
		SDK::Minecraft->SetRightClickDelayTimer(rcd);
}

void CombatBridge::DrainUrgentInputs()
{
	if (s_zeroLeftClick.exchange(false, std::memory_order_acq_rel) && InGame() && SDK::Minecraft)
		SDK::Minecraft->SetLeftClickCounter(0);

	if (s_zeroJumpTicks.exchange(false, std::memory_order_acq_rel) && InGame() && SDK::Minecraft)
	{
		jobject playerObj = SDK::Minecraft->GetThePlayerObject();
		if (playerObj)
		{
			CEntityLivingBase living(playerObj);
			if (living.GetJumpTicks() > 0)
				living.SetJumpTicks(0);
			Java::GetEnv()->DeleteLocalRef(playerObj);
		}
	}
}

void CombatBridge::RequestBlock(bool pressed)
{
	s_pendingBlock.store(pressed ? 1 : 0, std::memory_order_release);
}

void CombatBridge::RequestMoveKeyStates(bool forward, bool back, bool left, bool right, bool jump)
{
	s_mkForward.store(forward, std::memory_order_release);
	s_mkBack.store(back, std::memory_order_release);
	s_mkLeft.store(left, std::memory_order_release);
	s_mkRight.store(right, std::memory_order_release);
	s_mkJump.store(jump, std::memory_order_release);
	s_moveKeysActive.store(true, std::memory_order_release);
}

void CombatBridge::ClearMoveKeyStates()
{
	// Keep s_moveKeysApplied set so the drain runs one final all-released pass;
	// otherwise a key held when the screen closed would stick pressed forever.
	s_moveKeysActive.store(false, std::memory_order_release);
	s_mkForward.store(false, std::memory_order_release);
	s_mkBack.store(false, std::memory_order_release);
	s_mkLeft.store(false, std::memory_order_release);
	s_mkRight.store(false, std::memory_order_release);
	s_mkJump.store(false, std::memory_order_release);
}

void CombatBridge::RequestSneak(bool pressed)
{
	s_pendingSneak.store(pressed ? 1 : 0, std::memory_order_release);
}

void CombatBridge::RequestZeroMotionXZ()
{
	s_zeroMotionXZ.store(true, std::memory_order_release);
}

void CombatBridge::RequestHotbarSlot(int slot)
{
	if (slot < 0 || slot > 8) return;
	s_pendingHotbar.store(slot, std::memory_order_release);
}

void CombatBridge::RequestLeftClickCounterZero()
{
	s_zeroLeftClick.store(true, std::memory_order_release);
}

void CombatBridge::RequestRightClickDelay(int ticks)
{
	if (ticks < 0) ticks = 0;
	s_pendingRightClickDelay.store(ticks, std::memory_order_release);
}

void CombatBridge::RequestJumpTicksZero()
{
	s_zeroJumpTicks.store(true, std::memory_order_release);
}

void CombatBridge::ClearSyntheticAttackInput()
{
	SuppressHeldAttackInput();
}

CombatBridge::EntityRayHit CombatBridge::RaycastEntity(float reachBlocks, bool skipFriends)
{
	EntityRayHit result{};
	if (!InGame() || reachBlocks <= 0.0f) return result;

	CEntityPlayerSP* thePlayer = SDK::Minecraft->thePlayer;
	if (!thePlayer) return result;

	JNIEnv* env = Java::GetEnv();
	jobject localPlayer = SDK::Minecraft->GetThePlayerObject();
	if (!env || !localPlayer) return result;

	Vector3 eyes = thePlayer->GetEyePos();
	Vector2 angles = thePlayer->GetAngles();
	Vector3 look = Math::getLookVector(angles.x, angles.y);
	Vector3 end = eyes + look * reachBlocks;

	float bestDist = reachBlocks;
	jobject bestEntity = nullptr;

	// Walk the live list in place. Copying it produced a JNI global ref per
	// player (PlayerData holds a JObjectWrapper) three times per tick from the
	// combat modules alone.
	std::lock_guard<std::mutex> playersLock(CommonData::playerListMutex);
	for (const auto& pd : CommonData::nativePlayerList)
	{
		if (pd.isLocalPlayer) continue;
		jobject ent = pd.obj.Instance.Get();
		if (!ent) continue;
		if (env->IsSameObject(ent, localPlayer)) continue;
		// Combat piercing skips friends; middle-click friend toggle must not.
		if (skipFriends && Piercing::IsActive() && Friends::IsFriend(pd.name)) continue;

		const float partial = CommonData::renderPartialTicks;
		Vector3 interp = pd.lastPos + (pd.pos - pd.lastPos) * partial;
		// Half-width comes from the entity's own AABB (player width is 0.6, so
		// 0.3). It used to be derived from the *height* (1.8 * 0.35 = 0.63),
		// which made every hitbox more than twice as wide as the real one and
		// skewed the reported hit distance.
		const float bbWidth = static_cast<float>(pd.boundingBox.maxX - pd.boundingBox.minX);
		const float halfW = bbWidth > 0.01f ? bbWidth * 0.5f : 0.3f;
		BoundingBox box{
			interp.x - halfW, interp.y, interp.z - halfW,
			interp.x + halfW, interp.y + pd.height, interp.z + halfW
		};
		box = expandHitbox(box);
		float hitDist = 0.0f;
		if (!rayIntersectsBox(eyes, end, box, hitDist)) continue;

		if (hitDist < bestDist)
		{
			if (bestEntity) env->DeleteLocalRef(bestEntity);
			bestEntity = env->NewLocalRef(ent);
			bestDist = hitDist;
		}
	}

	env->DeleteLocalRef(localPlayer);

	if (bestEntity)
	{
		result.hit = true;
		result.entity = bestEntity;
		result.distance = bestDist;
	}
	return result;
}

void CombatBridge::TickTargetingTrack()
{
	if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
		s_lastAttackInputMs = nowMs();

	if (InGame())
	{
		jobject target = CrosshairEntity();
		if (target)
		{
			JNIEnv* env = Java::GetEnv();
			if (env && StrayCache::entityPlayer_class
				&& env->IsInstanceOf(target, StrayCache::entityPlayer_class)
				&& StrayCache::entityLivingBase_hurtTime)
			{
				int hurtTime = env->GetIntField(target, StrayCache::entityLivingBase_hurtTime);
				JniResolve::ClearException(env);

				if (s_prevCrosshairHurtTime == 0 && hurtTime > 0
					&& nowMs() - s_lastAttackInputMs < kAttackInputWindowMs)
				{
					s_lastPlayerHitMs = nowMs();
				}

				s_prevCrosshairHurtTime = hurtTime;
			}
			else
			{
				s_prevCrosshairHurtTime = 0;
			}
			if (env)
				env->DeleteLocalRef(target);
		}
		else
		{
			s_prevCrosshairHurtTime = 0;
		}
	}
	else
	{
		s_prevCrosshairHurtTime = 0;
	}

	const bool targetingActive = s_lastPlayerHitMs > 0
		&& nowMs() - s_lastPlayerHitMs < kTargetingWindowMs;
	Patcher::put("combat_targeting_active", targetingActive ? "true" : "false");
}

bool CombatBridge::IsRecentlyAttackingPlayer()
{
	return s_lastPlayerHitMs > 0 && nowMs() - s_lastPlayerHitMs < kTargetingWindowMs;
}
