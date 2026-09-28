#pragma once

#include "../util/math/geometry.h"
#include "../sdk/sdk.h"
#include "../sdk/strayCache.h"
#include "../sdk/jniResolve.h"
#include "../java/java.h"
#include "../util/logger.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

/*
This file is mainly for optimization purposes, instead of loading the data inside each module, we just load them here and then pass the data onto
the modules that will eventually use them.

The modules that write data will still need to access required java objects to do so.
*/
struct CommonData
{
	// Atomic lives in moduleManager.cpp; exposed via accessors because a shared
	// std::atomic<bool> variable recorded as different types across TUs (LNK C4744).
	static bool DataUpdated();
	static void SetDataUpdated(bool v);
	inline static bool inGui = false;
	inline static Matrix modelView;      // ROTATION-ONLY (translation zeroed)
	inline static Matrix projection;
	inline static Vector3 renderPos;
	inline static Vector3 camPos;        // raw camera render position (renderManager)
	inline static float renderPartialTicks;
	inline static float fov;
	inline static int thirdPersonView;
	inline static Vector2 localPlayerAngles;

	// Camera/render state is written by the cheat thread (UpdateData) and read
	// by the render thread (module onRender2D). Reads MUST go through
	// GetRenderState() to obtain a consistent, tear-free snapshot.
	inline static std::mutex renderStateMutex;
	struct RenderState {
		Matrix  modelView;          // rotation-only; pair with camPos
		Matrix  projection;
		Vector3 renderPos;
		Vector3 camPos;
		float   renderPartialTicks = 0.0f;
		float   fov = 0.0f;
		int     thirdPersonView = 0;
		Vector2 localPlayerAngles;
	};
	static RenderState GetRenderState()
	{
		std::lock_guard<std::mutex> lock(renderStateMutex);
		return RenderState{ modelView, projection, renderPos, camPos,
			renderPartialTicks, fov, thirdPersonView, localPlayerAngles };
	}
	
	struct ItemData {
		int itemId = 0;
		int itemMeta = 0;
		bool isBlockItem = false;
		std::string displayName;
		int protection = 0;
		int sharpness = 0;
		int power = 0;
		int featherFalling = 0;
		int fireAspect = 0;
		int unbreaking = 0;
		int durability = 0;     // remaining durability points
		int maxDurability = 0;  // max durability (0 = not damageable)
	};

	struct PlayerData{
		CEntityPlayer obj;
		Vector3 pos;
		Vector3 lastPos;
		BoundingBox boundingBox;
		std::string name;
		std::string displayName;
		float height;
		float health;
		float maxHealth;
		float absorptionAmount;
		std::vector<std::string> equipmentNames;
		std::vector<ItemData> equipmentItems; // [Held, Helmet, Chestplate, Leggings, Boots]
		bool isLocalPlayer;
		bool isInvisible = false;
		// Absolute steady_clock ms when the player's Regeneration potion
		// expires (0 = no regen).  Read on the slow pass every 400ms from
		// getActivePotionEffects (potion id 10 = Regeneration).
		long long regenEndMs = 0;
	};

	// Copy-cheap view of PlayerData for consumers that never touch the Java
	// object. Copying PlayerData allocates a JNI global ref per element (see
	// JObjectWrapper's copy ctor) plus its equipment strings, so "snapshot the
	// whole list" turned into a global-ref storm on the tick/render path.
	struct PlayerSnapshot {
		Vector3     pos;
		Vector3     lastPos;
		BoundingBox boundingBox{};
		std::string name;
		std::string displayName;
		float       height = 0.0f;
		float       health = 0.0f;
		float       maxHealth = 0.0f;
		float       absorptionAmount = 0.0f;
		bool        isLocalPlayer = false;
		bool        isInvisible = false;
		long long   regenEndMs = 0;
	};

	inline static std::vector<PlayerData> nativePlayerList;
	inline static std::mutex playerListMutex;
	inline static int localHotbarSlot = 0;

	static PlayerSnapshot MakeSnapshot(const PlayerData& pd)
	{
		PlayerSnapshot s;
		s.pos = pd.pos;
		s.lastPos = pd.lastPos;
		s.boundingBox = pd.boundingBox;
		s.name = pd.name;
		s.displayName = pd.displayName;
		s.height = pd.height;
		s.health = pd.health;
		s.maxHealth = pd.maxHealth;
		s.absorptionAmount = pd.absorptionAmount;
		s.isLocalPlayer = pd.isLocalPlayer;
		s.isInvisible = pd.isInvisible;
		s.regenEndMs = pd.regenEndMs;
		return s;
	}

	static std::vector<PlayerSnapshot> SnapshotPlayers()
	{
		std::vector<PlayerSnapshot> out;
		std::lock_guard<std::mutex> lock(playerListMutex);
		out.reserve(nativePlayerList.size());
		for (const auto& pd : nativePlayerList)
			out.push_back(MakeSnapshot(pd));
		return out;
	}

	// Lightweight per-frame refresh for the render thread (60+ fps).
	// UpdateData() runs on the cheat thread and may lag or fail; nametags/ESP
	// need live camera matrices and entity positions every swap-buffer.
	static void RefreshRenderState()
	{
		if (!SanityCheck()) return;

		// The MODELVIEW snapshot the game exposes carries the camera ROTATION
		// only (its translation stays near the origin). Points must first be
		// made camera-relative with the raw render position, so zero out the
		// residual translation and publish camPos alongside.
		Matrix  l_modelView   = SDK::Minecraft->activeRenderInfo->ModelViewMatrix();
		l_modelView.m30 = 0.0f; l_modelView.m31 = 0.0f; l_modelView.m32 = 0.0f;
		l_modelView.m33 = 1.0f;

		Matrix  l_projection  = SDK::Minecraft->activeRenderInfo->ProjectionMatrix();
		float   l_fov         = SDK::Minecraft->gameSettings->GetFOV();
		int     l_thirdPerson = SDK::Minecraft->gameSettings->GetThirdPersonView();
		Vector2 l_angles      = SDK::Minecraft->thePlayer->GetAngles();

		float ySubtractValue = 3.4f;
		if (SDK::Minecraft->thePlayer->IsSneaking())
			ySubtractValue -= .175f;

		Vector3 l_renderPos    = SDK::Minecraft->renderManager->RenderPos()
		                       + Vector3{ 0, ySubtractValue, 0 };
		Vector3 l_camPos       = SDK::Minecraft->renderManager->RenderPos();
		float   l_partialTicks = SDK::Minecraft->timer->GetRenderPartialTicks();

		std::lock_guard<std::mutex> lock(renderStateMutex);
		modelView          = l_modelView;
		projection         = l_projection;
		fov                = l_fov;
		thirdPersonView    = l_thirdPerson;
		localPlayerAngles  = l_angles;
		renderPos          = l_renderPos;
		camPos             = l_camPos;
		renderPartialTicks = l_partialTicks;
	}

	static void RefreshPlayerPositions()
	{
		if (!SanityCheck()) return;

		std::lock_guard<std::mutex> lock(playerListMutex);
		for (auto& pd : nativePlayerList)
		{
			if (!pd.obj.GetInstance()) continue;
			pd.pos      = pd.obj.GetPos();
			pd.lastPos  = pd.obj.GetLastTickPos();
			pd.height   = pd.obj.GetHeight();
		}
	}

	static void UpdateData()
	{
		if (!SanityCheck()) return;

		// NOTE: matrices are NOT read here. A tick-phase read can catch a
		// different GL pass (hand/GUI) and corrupt the camera transform;
		// RefreshRenderState() (called from the world-pass hook) is the only
		// writer for modelView/projection.
		float   l_fov         = SDK::Minecraft->gameSettings->GetFOV();
		int     l_thirdPerson = SDK::Minecraft->gameSettings->GetThirdPersonView();
		Vector2 l_angles      = SDK::Minecraft->thePlayer->GetAngles();

		float ySubtractValue = 3.4f;
		if (SDK::Minecraft->thePlayer->IsSneaking())
			ySubtractValue -= .175f;

		Vector3 l_renderPos    = SDK::Minecraft->renderManager->RenderPos() + Vector3{ 0, ySubtractValue, 0 };
		float   l_partialTicks = SDK::Minecraft->timer->GetRenderPartialTicks();
		bool    l_inGui        = SDK::Minecraft->IsInGuiState();

		{
			std::lock_guard<std::mutex> lock(renderStateMutex);
			fov                = l_fov;
			thirdPersonView    = l_thirdPerson;
			localPlayerAngles  = l_angles;
			renderPos          = l_renderPos;
			renderPartialTicks = l_partialTicks;
			inGui              = l_inGui;
		}

		// ---------------------------------------------------------------
		// FAST / SLOW split for the per-player JNI sweep.
		//
		// A full pass costs ~30+ JNI calls per player (display name, held
		// item, 4 armor stacks, 6 enchant lookups, durability...).  With
		// 16 players on a 40ms loop that's ~500 JNI calls / tick — the main
		// in-game stutter source.
		//
		// FAST pass (every tick): position, health, bounding box, name,
		// invisibility.  These drive combat targeting, so they stay fresh.
		//
		// SLOW pass (every 10th tick = 400ms): display name, equipment
		// names/items, enchants, durability.  These only feed nametags /
		// ESP visuals, which don't need 25Hz refresh.
		// ---------------------------------------------------------------
		static int s_slowPhase = 0;
		const bool slowPass = (++s_slowPhase % 10 == 0);

		const long long nowMsLocal = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();

		// --- Packet-driven regen tracking: Java tracks S1D/S0B/S1E packets,
		// C++ only queries entityId + the Java map (2 cheap JNI calls). ---

		std::vector<PlayerData> newData;
		std::vector<CEntityPlayer> playerList = SDK::Minecraft->theWorld->GetPlayerList();

		JNIEnv* env = Java::Env;
		jclass entityPlayerClass = StrayCache::entityPlayer_class;
		jmethodID getHeldItemMethod = StrayCache::entityLivingBase_getHeldItem;
		jmethodID getCurrentArmorMethod = StrayCache::entityPlayer_getCurrentArmor;
		jmethodID getDisplayNameMethod = StrayCache::itemStack_getDisplayName;
		jobject localPlayerInstance = SDK::Minecraft->thePlayer ? SDK::Minecraft->thePlayer->GetInstance() : nullptr;

		jclass enchantmentHelperClass = StrayCache::enchantmentHelper_class;
		jmethodID getEnchantmentLevelMethod = StrayCache::enchantmentHelper_getEnchantmentLevel;

		jmethodID getPlayerDisplayNameMethod = StrayCache::entity_getDisplayName;

		jclass iChatComponentClass = StrayCache::iChatComponent_class;
		jmethodID getFormattedTextMethod = StrayCache::iChatComponent_getFormattedText;

		// On fast ticks, reuse the previous slow fields (equipment, display
		// name) from the existing list so we don't pay the JNI cost.
		std::unordered_map<std::string, PlayerData> prevByName;
		if (!slowPass)
		{
			std::lock_guard<std::mutex> lock(playerListMutex);
			for (const auto& pd : nativePlayerList)
				if (!pd.name.empty())
					prevByName.emplace(pd.name, pd);
		}

		auto readDurability = [&](jobject stack, ItemData& out) {
			if (!env || !stack) return;
			if (StrayCache::itemStack_getMaxDamage && StrayCache::itemStack_getItemDamage) {
				int max = env->CallIntMethod(stack, StrayCache::itemStack_getMaxDamage);
				if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
				if (max > 0) {
					int dmg = env->CallIntMethod(stack, StrayCache::itemStack_getItemDamage);
					if (env->ExceptionCheck()) { env->ExceptionClear(); return; }
					out.maxDurability = max;
					out.durability = (max - dmg < 0) ? 0 : (max - dmg);
				}
			}
		};

		auto enchLevel = [&](int id, jobject stack) -> int {
			if (!env || !stack || !getEnchantmentLevelMethod || !enchantmentHelperClass)
				return 0;
			int v = env->CallStaticIntMethod(
				enchantmentHelperClass, getEnchantmentLevelMethod, id, stack);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
			return v;
		};

		// Potion-effect fallback for servers that don't hide effects.
		// Only runs when the packet tracker has no entry for this player.
		auto readRegenEndMsPotion = [&](jobject entityObj) -> long long {
			if (!env || !entityObj) return 0;

			static jclass  s_potionEffectClass = nullptr;
			static jmethodID s_getActivePotionEffects = nullptr;
			static jmethodID s_potionGetId = nullptr;
			static jmethodID s_potionGetDuration = nullptr;

			if (!s_getActivePotionEffects && StrayCache::entityLivingBase_class) {
				s_getActivePotionEffects = JniResolve::Method(env, StrayCache::entityLivingBase_class,
					"()Ljava/util/Collection;", "getActivePotionEffects");
			}
			if (!s_getActivePotionEffects) return 0;

			jobject effects = env->CallObjectMethod(entityObj, s_getActivePotionEffects);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
			if (!effects) return 0;

			if (!s_potionEffectClass) {
				jclass local = nullptr;
				if (Java::AssignClass("net.minecraft.potion.PotionEffect", local))
					s_potionEffectClass = local;
			}
			if (!s_potionEffectClass) { env->DeleteLocalRef(effects); return 0; }

			if (!s_potionGetId)
				s_potionGetId = JniResolve::Method(env, s_potionEffectClass, "()I", "getPotionID");
			if (!s_potionGetDuration)
				s_potionGetDuration = JniResolve::Method(env, s_potionEffectClass, "()I", "getDuration");
			if (!s_potionGetId || !s_potionGetDuration) { env->DeleteLocalRef(effects); return 0; }

			// Resolved once and pinned: this used to run FindClass +
			// GetMethodID for every player on every slow pass.
			static jclass    s_collectionClass = nullptr;
			static jmethodID s_toArrayMid = nullptr;
			if (!s_collectionClass)
			{
				jclass local = env->FindClass("java/util/Collection");
				JniResolve::ClearException(env);
				if (local)
				{
					s_collectionClass = (jclass)env->NewGlobalRef(local);
					env->DeleteLocalRef(local);
				}
			}
			if (s_collectionClass && !s_toArrayMid)
			{
				s_toArrayMid = env->GetMethodID(s_collectionClass, "toArray", "()[Ljava/lang/Object;");
				JniResolve::ClearException(env);
			}
			if (!s_toArrayMid) { env->DeleteLocalRef(effects); return 0; }

			jobjectArray arr = (jobjectArray)env->CallObjectMethod(effects, s_toArrayMid);
			if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(effects); return 0; }

			long long regenEnd = 0;
			if (arr) {
				const jsize n = env->GetArrayLength(arr);
				for (jsize i = 0; i < n; ++i) {
					jobject eff = env->GetObjectArrayElement(arr, i);
					if (!eff) continue;
					const int id = env->CallIntMethod(eff, s_potionGetId);
					if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(eff); continue; }
					if (id == 10) {
						const int durTicks = env->CallIntMethod(eff, s_potionGetDuration);
						if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(eff); continue; }
						const long long endMs = nowMsLocal + (long long)durTicks * 50LL;
						if (endMs > regenEnd) regenEnd = endMs;
					}
					env->DeleteLocalRef(eff);
				}
				env->DeleteLocalRef(arr);
			}
			env->DeleteLocalRef(effects);
			return regenEnd;
		};

		// Returns absolute ms (steady_clock base) when Regeneration expires, 0 if none.
		// Packet tracker first; potion fallback if tracker has no entry.
		auto readRegenEndMs = [&](jobject entityObj) -> long long {
			if (!env || !entityObj) return 0;

			static jmethodID s_getEntityId = nullptr;
			if (!s_getEntityId && StrayCache::entity_class) {
				s_getEntityId = JniResolve::Method(env, StrayCache::entity_class, "()I", "getEntityId");
			}
			if (!s_getEntityId) return 0;

			const int entityId = env->CallIntMethod(entityObj, s_getEntityId);
			if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }

			static jclass s_bridgeClass = nullptr;
			static jmethodID s_getRegenRemainingMs = nullptr;
			if (!s_bridgeClass) {
				jclass local = nullptr;
				if (Java::AssignClass("io.github.lefraudeur.RuntimeBridge", local))
					s_bridgeClass = local;
			}
			if (s_bridgeClass && !s_getRegenRemainingMs) {
				s_getRegenRemainingMs = env->GetStaticMethodID(s_bridgeClass, "getRegenRemainingMs", "(I)J");
				if (env->ExceptionCheck()) env->ExceptionClear();
			}
			if (s_bridgeClass && s_getRegenRemainingMs) {
				const jlong remMs = env->CallStaticLongMethod(s_bridgeClass, s_getRegenRemainingMs, entityId);
				if (env->ExceptionCheck()) { env->ExceptionClear(); }
				else if (remMs > 0) return nowMsLocal + (long long)remMs;
			}

			// Tracker has no entry → potion fallback.
			return readRegenEndMsPotion(entityObj);
		};

		for (CEntityPlayer& player : playerList) {
			std::vector<std::string> eqNames;
			std::vector<ItemData> eqItems;

			jobject entityObj = player.GetInstance();
			bool isLocal = false;
			bool invisible = false;
			long long regenEndMs = 0;
			std::string dNameStr = player.GetName();
			if (env && entityObj) {
				if (localPlayerInstance) {
					isLocal = env->IsSameObject(entityObj, localPlayerInstance);
				}
				if (StrayCache::entity_isInvisible) {
					invisible = env->CallBooleanMethod(entityObj, StrayCache::entity_isInvisible);
					if (env->ExceptionCheck()) { env->ExceptionClear(); invisible = false; }
				}

				if (slowPass)
				{
					regenEndMs = readRegenEndMs(entityObj);
					if (getPlayerDisplayNameMethod && getFormattedTextMethod && iChatComponentClass) {
						jobject chatComp = env->CallObjectMethod(entityObj, getPlayerDisplayNameMethod);
						if (env->ExceptionCheck()) { env->ExceptionClear(); chatComp = nullptr; }
						if (chatComp) {
							jstring formattedTextStr = (jstring)env->CallObjectMethod(chatComp, getFormattedTextMethod);
							if (env->ExceptionCheck()) { env->ExceptionClear(); formattedTextStr = nullptr; }
							if (formattedTextStr) {
								const char* chars = env->GetStringUTFChars(formattedTextStr, nullptr);
								if (chars) {
									dNameStr = chars;
									env->ReleaseStringUTFChars(formattedTextStr, chars);
								}
								env->DeleteLocalRef(formattedTextStr);
							}
							env->DeleteLocalRef(chatComp);
						}
					}

					// Query held item
					ItemData heldData;
					if (getHeldItemMethod && getDisplayNameMethod) {
						jobject heldStack = env->CallObjectMethod(entityObj, getHeldItemMethod);
						if (env->ExceptionCheck()) { env->ExceptionClear(); heldStack = nullptr; }
						if (heldStack) {
							jstring dName = (jstring)env->CallObjectMethod(heldStack, getDisplayNameMethod);
							if (env->ExceptionCheck()) { env->ExceptionClear(); dName = nullptr; }
							if (dName) {
								const char* chars = env->GetStringUTFChars(dName, nullptr);
								if (chars) {
									heldData.displayName = chars;
									eqNames.push_back("Held: " + std::string(chars));
									env->ReleaseStringUTFChars(dName, chars);
								}
								env->DeleteLocalRef(dName);
							}

							if (StrayCache::itemStack_getItemDamage) {
								heldData.itemMeta = env->CallIntMethod(heldStack, StrayCache::itemStack_getItemDamage);
								if (env->ExceptionCheck()) { env->ExceptionClear(); heldData.itemMeta = 0; }
							}

							jmethodID getItemMethod = StrayCache::itemStack_getItem;
							if (getItemMethod) {
								jobject itemObj = env->CallObjectMethod(heldStack, getItemMethod);
								if (env->ExceptionCheck()) { env->ExceptionClear(); itemObj = nullptr; }
								if (itemObj) {
									jmethodID getIdMethod = StrayCache::item_getIdFromItem;
									if (getIdMethod && StrayCache::item_class) {
										heldData.itemId = env->CallStaticIntMethod(StrayCache::item_class, getIdMethod, itemObj);
										if (env->ExceptionCheck()) { env->ExceptionClear(); heldData.itemId = 0; }
									}
									if (StrayCache::itemBlock_class) {
										heldData.isBlockItem = env->IsInstanceOf(itemObj, StrayCache::itemBlock_class) == JNI_TRUE;
									}
									env->DeleteLocalRef(itemObj);
								}
							}

							if (isLocal && StrayCache::entityPlayer_inventory && StrayCache::inventoryPlayer_currentItem) {
								jobject invObj = env->GetObjectField(entityObj, StrayCache::entityPlayer_inventory);
								if (env->ExceptionCheck()) { env->ExceptionClear(); invObj = nullptr; }
								if (invObj) {
									localHotbarSlot = env->GetIntField(invObj, StrayCache::inventoryPlayer_currentItem);
									if (env->ExceptionCheck()) { env->ExceptionClear(); localHotbarSlot = 0; }
									env->DeleteLocalRef(invObj);
								}
							}

							if (getEnchantmentLevelMethod) {
								heldData.protection     = enchLevel(0,  heldStack);
								heldData.sharpness      = enchLevel(16, heldStack);
								heldData.power          = enchLevel(48, heldStack);
								heldData.featherFalling = enchLevel(2,  heldStack);
								heldData.fireAspect     = enchLevel(20, heldStack);
								heldData.unbreaking     = enchLevel(34, heldStack);
							}
							readDurability(heldStack, heldData);

							env->DeleteLocalRef(heldStack);
						}
					}
					eqItems.push_back(heldData);

					// Query armor slots: 3 (Helmet) down to 0 (Boots)
					if (entityPlayerClass && getCurrentArmorMethod && getDisplayNameMethod && env->IsInstanceOf(entityObj, entityPlayerClass)) {
						for (int i = 3; i >= 0; i--) {
							ItemData armorData;
							jobject armorStack = env->CallObjectMethod(entityObj, getCurrentArmorMethod, i);
							if (env->ExceptionCheck()) { env->ExceptionClear(); armorStack = nullptr; }
							if (armorStack) {
								jstring dName = (jstring)env->CallObjectMethod(armorStack, getDisplayNameMethod);
								if (env->ExceptionCheck()) { env->ExceptionClear(); dName = nullptr; }
								if (dName) {
									const char* chars = env->GetStringUTFChars(dName, nullptr);
									if (chars) {
										armorData.displayName = chars;
										eqNames.push_back(std::string(chars));
										env->ReleaseStringUTFChars(dName, chars);
									}
									env->DeleteLocalRef(dName);
								}

								jmethodID getItemMethod = StrayCache::itemStack_getItem;
								if (getItemMethod) {
									jobject itemObj = env->CallObjectMethod(armorStack, getItemMethod);
									if (itemObj) {
										jmethodID getIdMethod = StrayCache::item_getIdFromItem;
										if (getIdMethod) {
											armorData.itemId = env->CallStaticIntMethod(StrayCache::item_class, getIdMethod, itemObj);
										}
										env->DeleteLocalRef(itemObj);
									}
								}

								if (getEnchantmentLevelMethod) {
									armorData.protection     = enchLevel(0,  armorStack);
									armorData.sharpness      = enchLevel(16, armorStack);
									armorData.power          = enchLevel(48, armorStack);
									armorData.featherFalling = enchLevel(2,  armorStack);
									armorData.fireAspect     = enchLevel(20, armorStack);
									armorData.unbreaking     = enchLevel(34, armorStack);
								}
								readDurability(armorStack, armorData);

								env->DeleteLocalRef(armorStack);
							}
							eqItems.push_back(armorData);
						}
					} else {
						for (int i = 0; i < 4; i++) {
							eqItems.push_back(ItemData{});
						}
					}
				}
				else
				{
					// FAST pass: carry over equipment/display name from the
					// previous snapshot so modules don't see empty gear.
					const std::string key = player.GetName();
					auto it = prevByName.find(key);
					if (it != prevByName.end())
					{
						dNameStr = it->second.displayName;
						eqNames  = it->second.equipmentNames;
						eqItems  = it->second.equipmentItems;
						regenEndMs = it->second.regenEndMs;
					}
					else
					{
						for (int i = 0; i < 5; i++) {
							eqItems.push_back(ItemData{});
						}
					}
				}
			} else {
				for (int i = 0; i < 5; i++) {
					eqItems.push_back(ItemData{});
				}
			}

			newData.push_back(PlayerData{
					player,
					player.GetPos(),
					player.GetLastTickPos(),
					player.GetBB().GetNativeBoundingBox(),
					player.GetName(),
					dNameStr,
					player.GetHeight(),
					player.GetHealth(),
					player.GetMaxHealth(),
					player.GetAbsorptionAmount(),
					eqNames,
					eqItems,
					isLocal,
					invisible,
					regenEndMs
				});
		}

		{
			std::lock_guard<std::mutex> lock(playerListMutex);
			nativePlayerList = std::move(newData);
		}

		if (localPlayerInstance)
			env->DeleteLocalRef(localPlayerInstance);

		SetDataUpdated(true);
							// Mainly for sanity checks for rendering functions, it prevents crashing whenever the user is not in a game because some data
							// might be needed from within the render functions.
	}

	// Return false if sanity check failed
	static bool SanityCheck() {
		if (!Java::Initialized) return false;
		JNIEnv* env = Java::GetEnv();
		if (!env) return false;
		if (!StrayCache::IsReady()) return false;
		if (!SDK::Minecraft || !SDK::Minecraft->IsReady()) return false;

		jobject playerInstance = SDK::Minecraft->GetThePlayerObject();
		jobject worldInstance = SDK::Minecraft->GetTheWorldObject();
		const bool ok = playerInstance && worldInstance;
		if (playerInstance) env->DeleteLocalRef(playerInstance);
		if (worldInstance) env->DeleteLocalRef(worldInstance);

		if (!ok)
		{
			CommonData::SetDataUpdated(false);
			// Leaving the previous snapshot alive here kept global refs to
			// entities of a world that no longer exists, so any module still
			// touching pd.obj after a disconnect / dimension change operated on
			// dead objects. Dropping it releases those refs (JObjectWrapper
			// destructor) and makes every consumer see an empty world.
			ClearWorldState();
			return false;
		}
		return true;
	}

	// Drops all cached entity references. Safe to call repeatedly.
	static void ClearWorldState()
	{
		std::lock_guard<std::mutex> lock(playerListMutex);
		if (!nativePlayerList.empty())
			nativePlayerList.clear();
	}
};