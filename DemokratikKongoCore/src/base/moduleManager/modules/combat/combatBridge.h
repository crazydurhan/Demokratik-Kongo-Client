#pragma once

// Forward-declare JNI jobject without pulling jni.h into every TU.
struct _jobject;
typedef _jobject* jobject;

namespace CombatBridge
{
	struct EntityRayHit
	{
		bool  hit = false;
		jobject entity = nullptr; // local ref; caller must DeleteLocalRef
		float distance = 0.0f;
	};

	bool CanCombat();
	bool InGame();

	void LeftClick();
	void PostLeftClick();
	void PostLeftButton(bool pressed);
	void PostRightClick();
	void PostRightButton(bool pressed);

	// Hotbar slot 0-8 via number-key input (no C09 / syncCurrentPlayItem).
	void SelectHotbarSlot(int slot);

	void SetLeftClickState(bool pressed);

	// Holds / releases the use-item keybind (keyBindUseItem) so the player
	// keeps blocking while held.  On press it also fires rightClickMouse()
	// once so the block actually starts (vanilla holds item-in-use while
	// keyBindUseItem.pressed == true).
	void SetRightClickState(bool pressed);

	void RightClick();

	void AttackEntity(jobject targetEntity);

	void SetReach(float blocks);

	void ApplyVelocity(float horizontalPct, float verticalPct);

	bool CrosshairOnEntity();

	jobject CrosshairEntity();

	// Returns true when the entity under the crosshair is on the friend list.
	bool CrosshairOnFriend();

	// Re-run EspBridge piercing raycast so objectMouseOver is fresh before attack.
	void RefreshMouseOverPost();

	// AutoClicker: vanilla KeyBinding pulse on client thread (pre position packet).
	void PulseAttackClick();

	// Queue clicks from worker threads; drained on the Minecraft client thread.
	void DrainPendingInputs();

	// Latency-critical single-field writes (leftClickCounter, jumpTicks). Runs
	// uncapped from the per-frame getMouseOver hook so NoHitDelay/NoJumpDelay
	// apply within the same frame instead of waiting for the 45ms drain gate.
	void DrainUrgentInputs();

	// Hide physical LMB hold from vanilla between scheduled autoclicker pulses.
	void SuppressHeldAttackInput();

	void ClearSyntheticAttackInput();

	// --- Client-thread actuation queue (worker decides, drain applies) ---
	void RequestBlock(bool pressed);
	// InvMove: drives the real movement KeyBinding pressed states (vanilla
	// updatePlayerMoveState picks them up even while a GUI screen is open).
	void RequestMoveKeyStates(bool forward, bool back, bool left, bool right, bool jump);
	void ClearMoveKeyStates();
	// SafeWalk: presses/releases the sneak keybind on the client thread.
	void RequestSneak(bool pressed);
	void RequestZeroMotionXZ();
	void RequestHotbarSlot(int slot); // 0-8
	void RequestLeftClickCounterZero();
	void RequestRightClickDelay(int ticks);
	void RequestJumpTicksZero();

	// Vanilla-style entity ray trace (eyes + look * reach). Closest intercept wins.
	// When skipFriends is true and Piercing is active, friends are ignored (combat).
	// Middle-click friend toggle must pass skipFriends=false.
	EntityRayHit RaycastEntity(float reachBlocks, bool skipFriends = true);

	// Tracks recent hits on crosshair players; feeds combat_targeting_active for Velocity/KBD.
	void TickTargetingTrack();
	bool IsRecentlyAttackingPlayer();
}
