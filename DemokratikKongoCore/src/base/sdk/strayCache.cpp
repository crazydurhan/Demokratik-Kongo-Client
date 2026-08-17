#include "strayCache.h"
#include "mappingLoader.h"
#include "mappedRegistry.h"
#include "automap/autoMapper.h"
#include <mutex>
#include "jniResolve.h"

#include "../util/logger.h"

namespace
{
	void reportMissing(const void* id, const char* what)
	{
		if (!id)
			Logger::Err(std::string("[StrayCache] MISSING mapping: ") + what);
	}
}

void StrayCache::Initialize()
{
	if (IsReady())
	{
		initialized = true;
		return;
	}

	static bool mappingLoaderDone = false;
	if (!mappingLoaderDone)
	{
		if (!AutoMapper::IsReady())
			AutoMapper::Init();
		else
		{
			MappingLoader::Init();
			MappedRegistry::Init();
		}
		mappingLoaderDone = true;
	}

	JNIEnv* env = Java::GetEnv();
	if (!env || !Java::Initialized)
	{
		Logger::Err("[StrayCache] Cannot initialize - Java not ready.");
		return;
	}

	if (!Java::AssignClass("net.minecraft.entity.Entity", entity_class) && !entity_class)
		Logger::Err("[StrayCache] Failed to load net.minecraft.entity.Entity");

	if (entity_class)
	{
		entity_getName = JniResolve::Method(env, entity_class, "()Ljava/lang/String;", "getName");
		entity_getDisplayName = JniResolve::Method(env, entity_class, "()Lnet/minecraft/util/IChatComponent;", "getDisplayName");
		entity_isSneaking = JniResolve::Method(env, entity_class, "()Z", "isSneaking");
		entity_isInvisible = JniResolve::Method(env, entity_class, "()Z", "isInvisible");
		entity_posX = JniResolve::Field(env, entity_class, "D", "posX");
		entity_posY = JniResolve::Field(env, entity_class, "D", "posY");
		entity_posZ = JniResolve::Field(env, entity_class, "D", "posZ");
		entity_lastTickPosX = JniResolve::Field(env, entity_class, "D", "lastTickPosX");
		entity_lastTickPosY = JniResolve::Field(env, entity_class, "D", "lastTickPosY");
		entity_lastTickPosZ = JniResolve::Field(env, entity_class, "D", "lastTickPosZ");
		entity_width = JniResolve::Field(env, entity_class, "F", "width");
		entity_height = JniResolve::Field(env, entity_class, "F", "height");
		entity_distanceWalkedModified = JniResolve::Field(env, entity_class, "F", "distanceWalkedModified");
		entity_prevDistanceWalkedModified = JniResolve::Field(env, entity_class, "F", "prevDistanceWalkedModified");
		entity_rotationYaw = JniResolve::Field(env, entity_class, "F", "rotationYaw");
		entity_rotationPitch = JniResolve::Field(env, entity_class, "F", "rotationPitch");
		entity_prevRotationYaw = JniResolve::Field(env, entity_class, "F", "prevRotationYaw");
		entity_prevRotationPitch = JniResolve::Field(env, entity_class, "F", "prevRotationPitch");
		entity_boundingBox = JniResolve::Field(env, entity_class, "Lnet/minecraft/util/AxisAlignedBB;", "boundingBox");
		entity_motionX = JniResolve::Field(env, entity_class, "D", "motionX");
		entity_motionY = JniResolve::Field(env, entity_class, "D", "motionY");
		entity_motionZ = JniResolve::Field(env, entity_class, "D", "motionZ");
		entity_setSprinting = JniResolve::Method(env, entity_class, "(Z)V", "setSprinting");
		entity_isSprinting = JniResolve::Method(env, entity_class, "()Z", "isSprinting");
		entity_isInWater = JniResolve::Method(env, entity_class, "()Z", "isInWater");
		entity_onGround = JniResolve::Field(env, entity_class, "Z", "onGround");
		entity_fallDistance = JniResolve::Field(env, entity_class, "F", "fallDistance");
	}

	if (Java::AssignClass("net.minecraft.entity.EntityLivingBase", entityLivingBase_class) && entityLivingBase_class)
	{
		entityLivingBase_getHealth = JniResolve::Method(env, entityLivingBase_class, "()F", "getHealth");
		entityLivingBase_getMaxHealth = JniResolve::Method(env, entityLivingBase_class, "()F", "getMaxHealth");
		entityLivingBase_canEntityBeSeen = JniResolve::Method(env, entityLivingBase_class, "(Lnet/minecraft/entity/Entity;)Z", "canEntityBeSeen");
		entityLivingBase_getHeldItem = JniResolve::Method(env, entityLivingBase_class, "()Lnet/minecraft/item/ItemStack;", "getHeldItem");
		entityLivingBase_swingItem = JniResolve::Method(env, entityLivingBase_class, "()V", "swingItem");
		entityLivingBase_hurtTime = JniResolve::Field(env, entityLivingBase_class, "I", "hurtTime");
		entityLivingBase_jumpTicks = JniResolve::Field(env, entityLivingBase_class, "I", "jumpTicks");
		entityLivingBase_removePotionEffect = JniResolve::Method(env, entityLivingBase_class, "(I)V", "removePotionEffect");
		if (!entityLivingBase_removePotionEffect)
			entityLivingBase_removePotionEffect = JniResolve::Method(env, entityLivingBase_class, "(I)V", "removePotionEffectClient");
		entityLivingBase_isPotionActive = JniResolve::Method(env, entityLivingBase_class, "(I)Z", "isPotionActive");
		entityLivingBase_moveForward = JniResolve::Field(env, entityLivingBase_class, "F", "moveForward");
		entityLivingBase_moveStrafing = JniResolve::Field(env, entityLivingBase_class, "F", "moveStrafing");
	}

	if (Java::AssignClass("net.minecraft.client.renderer.entity.RenderManager", renderManager_class) && renderManager_class)
	{
		renderManager_renderPosX = JniResolve::Field(env, renderManager_class, "D", "renderPosX");
		renderManager_renderPosY = JniResolve::Field(env, renderManager_class, "D", "renderPosY");
		renderManager_renderPosZ = JniResolve::Field(env, renderManager_class, "D", "renderPosZ");
		renderManager_playerViewX = JniResolve::Field(env, renderManager_class, "F", "playerViewX");
		renderManager_playerViewY = JniResolve::Field(env, renderManager_class, "F", "playerViewY");
	}

	if (Java::AssignClass("net.minecraft.entity.player.EntityPlayer", entityPlayer_class))
	{
		entityPlayer_inventory = JniResolve::Field(env, entityPlayer_class,
			"Lnet/minecraft/entity/player/InventoryPlayer;", "inventory");
		entityPlayer_getCurrentArmor = JniResolve::Method(env, entityPlayer_class, "(I)Lnet/minecraft/item/ItemStack;", "getCurrentArmor");
	}

	if (Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", entityPlayerSP_class))
	{
		entityPlayerSP_sendChatMessage = JniResolve::Method(env, entityPlayerSP_class,
			"(Ljava/lang/String;)V", "sendChatMessage");
		entityPlayerSP_timeInPortal = JniResolve::Field(env, entityPlayerSP_class, "F", "timeInPortal");
		entityPlayerSP_prevTimeInPortal = JniResolve::Field(env, entityPlayerSP_class, "F", "prevTimeInPortal");
	}

	if (Java::AssignClass("net.minecraft.util.AxisAlignedBB", axisAlignedBB_class))
	{
		axisAlignedBB_minX = JniResolve::Field(env, axisAlignedBB_class, "D", "minX");
		axisAlignedBB_minY = JniResolve::Field(env, axisAlignedBB_class, "D", "minY");
		axisAlignedBB_minZ = JniResolve::Field(env, axisAlignedBB_class, "D", "minZ");
		axisAlignedBB_maxX = JniResolve::Field(env, axisAlignedBB_class, "D", "maxX");
		axisAlignedBB_maxY = JniResolve::Field(env, axisAlignedBB_class, "D", "maxY");
		axisAlignedBB_maxZ = JniResolve::Field(env, axisAlignedBB_class, "D", "maxZ");
	}

	if (Java::AssignClass("net.minecraft.util.MovingObjectPosition", movingObjectPosition_class))
	{
		movingObjectPosition_hitVec = JniResolve::Field(env, movingObjectPosition_class, "Lnet/minecraft/util/Vec3;", "hitVec");
		movingObjectPosition_blockPos = JniResolve::Field(env, movingObjectPosition_class, "Lnet/minecraft/util/BlockPos;", "blockPos");
		movingObjectPosition_sideHit = JniResolve::Field(env, movingObjectPosition_class, "Lnet/minecraft/util/EnumFacing;", "sideHit");
		movingObjectPosition_typeOfHit = JniResolve::Field(env, movingObjectPosition_class, "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;", "typeOfHit");
		movingObjectPosition_entityHit = JniResolve::Field(env, movingObjectPosition_class, "Lnet/minecraft/entity/Entity;", "entityHit");
		movingObjectPosition_initEntity = env->GetMethodID(movingObjectPosition_class, "<init>", "(Lnet/minecraft/entity/Entity;)V");
		JniResolve::ClearException(env);
	}

	if (Java::AssignClass("net.minecraft.util.Vec3", vec3_class))
	{
		vec3_xCoord = JniResolve::Field(env, vec3_class, "D", "xCoord");
		vec3_yCoord = JniResolve::Field(env, vec3_class, "D", "yCoord");
		vec3_zCoord = JniResolve::Field(env, vec3_class, "D", "zCoord");
	}

	if (Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryPlayer_class))
	{
		inventoryPlayer_getCurrentItem = JniResolve::Method(env, inventoryPlayer_class, "()Lnet/minecraft/item/ItemStack;", "getCurrentItem");
		inventoryPlayer_currentItem = JniResolve::Field(env, inventoryPlayer_class, "I", "currentItem");
		inventoryPlayer_mainInventory = JniResolve::Field(env, inventoryPlayer_class, "[Lnet/minecraft/item/ItemStack;", "mainInventory");
	}

	if (Java::AssignClass("net.minecraft.item.ItemStack", itemStack_class))
	{
		itemStack_getItem = JniResolve::Method(env, itemStack_class, "()Lnet/minecraft/item/Item;", "getItem");
		itemStack_getDisplayName = JniResolve::Method(env, itemStack_class, "()Ljava/lang/String;", "getDisplayName");
		itemStack_getItemDamage = JniResolve::Method(env, itemStack_class, "()I", "getItemDamage");
		itemStack_getMaxDamage = JniResolve::Method(env, itemStack_class, "()I", "getMaxDamage");
		itemStack_stackSize = JniResolve::Field(env, itemStack_class, "I", "stackSize");
	}

	if (Java::AssignClass("net.minecraft.item.Item", item_class))
		item_getIdFromItem = JniResolve::StaticMethod(env, item_class, "(Lnet/minecraft/item/Item;)I", "getIdFromItem");

	Java::AssignClass("net.minecraft.item.ItemBlock", itemBlock_class);
	Java::AssignClass("net.minecraft.client.gui.inventory.GuiInventory", guiInventory_class);

	if (Java::AssignClass("java.util.List", list_class))
		list_toArray = JniResolve::Method(env, list_class, "()[Ljava/lang/Object;", "toArray");

	if (Java::AssignClass("java.util.Set", set_class))
		set_toArray = JniResolve::Method(env, set_class, "()[Ljava/lang/Object;", "toArray");

	if (Java::AssignClass("net.minecraft.util.IChatComponent", iChatComponent_class))
		iChatComponent_getFormattedText = JniResolve::Method(env, iChatComponent_class, "()Ljava/lang/String;", "getFormattedText");

	if (Java::AssignClass("net.minecraft.enchantment.EnchantmentHelper", enchantmentHelper_class))
		enchantmentHelper_getEnchantmentLevel = JniResolve::StaticMethod(env, enchantmentHelper_class, "(ILnet/minecraft/item/ItemStack;)I", "getEnchantmentLevel");

	if (Java::AssignClass("net.minecraft.client.multiplayer.PlayerControllerMP", playerControllerMP_class))
	{
		playerControllerMP_getBlockReachDistance = JniResolve::Method(env, playerControllerMP_class, "()F", "getBlockReachDistance");
		playerControllerMP_extendedReach = JniResolve::Method(env, playerControllerMP_class, "()Z", "extendedReach");
		playerControllerMP_attackEntity = JniResolve::Method(env, playerControllerMP_class,
			"(Lnet/minecraft/entity/player/EntityPlayer;Lnet/minecraft/entity/Entity;)V", "attackEntity");
		if (!playerControllerMP_attackEntity)
		{
			playerControllerMP_attackEntity = JniResolve::Method(env, playerControllerMP_class,
				"(Lnet/minecraft/entity/player/EntityPlayer;Lnet/minecraft/entity/Entity;)V", "func_78764_a");
		}
		playerControllerMP_onPlayerRightClick = JniResolve::Method(env, playerControllerMP_class,
			"(Lnet/minecraft/client/entity/EntityPlayerSP;Lnet/minecraft/client/multiplayer/WorldClient;Lnet/minecraft/item/ItemStack;Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;Lnet/minecraft/util/Vec3;)Z",
			"onPlayerRightClick");
		// 1.8.9 signature is 4 ints: windowClick(windowId, slot, button, mode, player).
		playerControllerMP_windowClick = JniResolve::Method(env, playerControllerMP_class,
			"(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;", "windowClick");
		if (!playerControllerMP_windowClick)
		{
			// Lunar renames the method past our alias list; match by signature instead.
			playerControllerMP_windowClick = JniResolve::MethodByDescriptor(env, playerControllerMP_class,
				"(IIIILnet/minecraft/entity/player/EntityPlayer;)Lnet/minecraft/item/ItemStack;");
			if (playerControllerMP_windowClick)
				Logger::Log("[StrayCache] PlayerControllerMP.windowClick resolved via descriptor scan (renamed method)");
		}
		playerControllerMP_syncCurrentPlayItem = JniResolve::Method(env, playerControllerMP_class,
			"()V", "syncCurrentPlayItem");
		playerControllerMP_curBlockDamageMP = JniResolve::Field(env, playerControllerMP_class,
			"F", "curBlockDamageMP");
		playerControllerMP_blockHitDelay = JniResolve::Field(env, playerControllerMP_class,
			"I", "blockHitDelay");
		playerControllerMP_isHittingBlock = JniResolve::Field(env, playerControllerMP_class,
			"Z", "isHittingBlock");
	}

	jclass mcClass = nullptr;
	if (Java::AssignClass("net.minecraft.client.Minecraft", mcClass))
	{
		minecraft_getMinecraft = JniResolve::StaticMethod(env, mcClass, "()Lnet/minecraft/client/Minecraft;", "getMinecraft");
		minecraft_playerController = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/multiplayer/PlayerControllerMP;", "playerController");
		minecraft_thePlayer = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/entity/EntityPlayerSP;", "thePlayer");
		minecraft_theWorld = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/multiplayer/WorldClient;", "theWorld");
		minecraft_gameSettings = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/settings/GameSettings;", "gameSettings");
		minecraft_timer = JniResolve::Field(env, mcClass, "Lnet/minecraft/util/Timer;", "timer");
		minecraft_objectMouseOver = JniResolve::Field(env, mcClass, "Lnet/minecraft/util/MovingObjectPosition;", "objectMouseOver");
		minecraft_currentScreen = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/gui/GuiScreen;", "currentScreen");
		minecraft_entityRenderer = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/renderer/EntityRenderer;", "entityRenderer");
		minecraft_renderManager = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/renderer/entity/RenderManager;", "renderManager");
		minecraft_ingameGUI = JniResolve::Field(env, mcClass, "Lnet/minecraft/client/gui/GuiIngame;", "ingameGUI");
		minecraft_leftClickCounter = JniResolve::Field(env, mcClass, "I", "leftClickCounter");
		minecraft_rightClickDelayTimer = JniResolve::Field(env, mcClass, "I", "rightClickDelayTimer");
		minecraft_inGameHasFocus = JniResolve::Field(env, mcClass, "Z", "inGameHasFocus");
		minecraft_clickMouse = JniResolve::Method(env, mcClass, "()V", "clickMouse");
		minecraft_rightClickMouse = JniResolve::Method(env, mcClass, "()V", "rightClickMouse");
		env->DeleteLocalRef(mcClass);
	}

	jclass worldClass = nullptr;
	if (Java::AssignClass("net.minecraft.world.World", worldClass))
	{
		world_playerEntities = JniResolve::Field(env, worldClass, "Ljava/util/List;", "playerEntities");
		env->DeleteLocalRef(worldClass);
	}

	auto fillClass = [&](jclass& dst, const char* mcp) {
		if (dst || !mcp) return;
		if (jclass c = AutoMapper::Class(mcp))
			dst = static_cast<jclass>(env->NewGlobalRef(c));
	};
	auto fillField = [&](jfieldID& dst, const char* owner, const char* name) {
		if (!dst) dst = AutoMapper::Field(owner, name);
	};
	auto fillMethod = [&](jmethodID& dst, const char* owner, const char* name) {
		if (!dst) dst = AutoMapper::Method(owner, name);
	};

	fillClass(entity_class, "net/minecraft/entity/Entity");
	fillClass(entityLivingBase_class, "net/minecraft/entity/EntityLivingBase");
	fillClass(entityPlayer_class, "net/minecraft/entity/player/EntityPlayer");
	fillClass(entityPlayerSP_class, "net/minecraft/client/entity/EntityPlayerSP");
	fillClass(playerControllerMP_class, "net/minecraft/client/multiplayer/PlayerControllerMP");
	fillClass(movingObjectPosition_class, "net/minecraft/util/MovingObjectPosition");
	fillClass(axisAlignedBB_class, "net/minecraft/util/AxisAlignedBB");
	fillClass(vec3_class, "net/minecraft/util/Vec3");
	fillClass(inventoryPlayer_class, "net/minecraft/entity/player/InventoryPlayer");
	fillClass(itemStack_class, "net/minecraft/item/ItemStack");
	fillClass(item_class, "net/minecraft/item/Item");
	fillClass(renderManager_class, "net/minecraft/client/renderer/entity/RenderManager");

	fillField(entity_posX, "net/minecraft/entity/Entity", "posX");
	fillField(entity_posY, "net/minecraft/entity/Entity", "posY");
	fillField(entity_posZ, "net/minecraft/entity/Entity", "posZ");
	fillField(entity_lastTickPosX, "net/minecraft/entity/Entity", "lastTickPosX");
	fillField(entity_lastTickPosY, "net/minecraft/entity/Entity", "lastTickPosY");
	fillField(entity_lastTickPosZ, "net/minecraft/entity/Entity", "lastTickPosZ");
	fillField(entity_motionX, "net/minecraft/entity/Entity", "motionX");
	fillField(entity_motionY, "net/minecraft/entity/Entity", "motionY");
	fillField(entity_motionZ, "net/minecraft/entity/Entity", "motionZ");
	fillField(entity_rotationYaw, "net/minecraft/entity/Entity", "rotationYaw");
	fillField(entity_rotationPitch, "net/minecraft/entity/Entity", "rotationPitch");
	fillField(entity_prevRotationYaw, "net/minecraft/entity/Entity", "prevRotationYaw");
	fillField(entity_prevRotationPitch, "net/minecraft/entity/Entity", "prevRotationPitch");
	fillField(entityLivingBase_hurtTime, "net/minecraft/entity/EntityLivingBase", "hurtTime");
	fillField(minecraft_thePlayer, "net/minecraft/client/Minecraft", "thePlayer");
	fillField(minecraft_theWorld, "net/minecraft/client/Minecraft", "theWorld");
	fillField(minecraft_objectMouseOver, "net/minecraft/client/Minecraft", "objectMouseOver");
	fillField(minecraft_leftClickCounter, "net/minecraft/client/Minecraft", "leftClickCounter");
	fillField(minecraft_rightClickDelayTimer, "net/minecraft/client/Minecraft", "rightClickDelayTimer");
	fillField(minecraft_inGameHasFocus, "net/minecraft/client/Minecraft", "inGameHasFocus");
	fillField(minecraft_playerController, "net/minecraft/client/Minecraft", "playerController");
	fillField(minecraft_timer, "net/minecraft/client/Minecraft", "timer");
	fillField(minecraft_gameSettings, "net/minecraft/client/Minecraft", "gameSettings");
	fillField(minecraft_renderManager, "net/minecraft/client/Minecraft", "renderManager");
	fillField(world_playerEntities, "net/minecraft/world/World", "playerEntities");

	fillMethod(minecraft_getMinecraft, "net/minecraft/client/Minecraft", "getMinecraft");
	fillMethod(minecraft_clickMouse, "net/minecraft/client/Minecraft", "clickMouse");
	fillMethod(minecraft_rightClickMouse, "net/minecraft/client/Minecraft", "rightClickMouse");
	fillMethod(playerControllerMP_attackEntity, "net/minecraft/client/multiplayer/PlayerControllerMP", "attackEntity");
	fillMethod(playerControllerMP_windowClick, "net/minecraft/client/multiplayer/PlayerControllerMP", "windowClick");
	fillMethod(playerControllerMP_syncCurrentPlayItem, "net/minecraft/client/multiplayer/PlayerControllerMP", "syncCurrentPlayItem");
	fillMethod(entityLivingBase_getHeldItem, "net/minecraft/entity/EntityLivingBase", "getHeldItem");

	reportMissing(entity_class, "Entity class");
	reportMissing(entity_rotationYaw, "Entity.rotationYaw");
	reportMissing(entity_rotationPitch, "Entity.rotationPitch");
	reportMissing(entity_posX, "Entity.posX");
	reportMissing(entityLivingBase_hurtTime, "EntityLivingBase.hurtTime");
	reportMissing(entityLivingBase_getHeldItem, "EntityLivingBase.getHeldItem");
	reportMissing(playerControllerMP_attackEntity, "PlayerControllerMP.attackEntity");
	reportMissing(minecraft_thePlayer, "Minecraft.thePlayer");
	reportMissing(minecraft_theWorld, "Minecraft.theWorld");
	reportMissing(minecraft_objectMouseOver, "Minecraft.objectMouseOver");
	reportMissing(minecraft_clickMouse, "Minecraft.clickMouse");
	reportMissing(minecraft_leftClickCounter, "Minecraft.leftClickCounter");
	reportMissing(minecraft_rightClickDelayTimer, "Minecraft.rightClickDelayTimer");
	reportMissing(minecraft_inGameHasFocus, "Minecraft.inGameHasFocus");
	reportMissing(minecraft_rightClickMouse, "Minecraft.rightClickMouse");
	reportMissing(playerControllerMP_windowClick, "PlayerControllerMP.windowClick");
	reportMissing(playerControllerMP_syncCurrentPlayItem, "PlayerControllerMP.syncCurrentPlayItem");
	reportMissing(inventoryPlayer_mainInventory, "InventoryPlayer.mainInventory");
	reportMissing(entityPlayer_getCurrentArmor, "EntityPlayer.getCurrentArmor");
	reportMissing(minecraft_getMinecraft, "Minecraft.getMinecraft");
	reportMissing(world_playerEntities, "World.playerEntities");
	reportMissing(list_toArray, "List.toArray");

	// EspBridge cannot be resolved here because it is injected later during Patcher::Init().
	// It will be resolved dynamically via EnsureEspBridge() when needed.

	initialized = IsReady();
	if (initialized)
		Logger::Log("[StrayCache] All critical mappings resolved.");
	else
		Logger::Err("[StrayCache] Critical mappings missing - combat modules disabled until resolved.");
}

bool StrayCache::IsReady()
{
	return entity_class
		&& entity_rotationYaw && entity_rotationPitch
		&& entity_posX && entity_posY && entity_posZ
		&& entityLivingBase_hurtTime
		&& entityLivingBase_getHeldItem
		&& playerControllerMP_attackEntity
		&& minecraft_thePlayer && minecraft_theWorld
		&& minecraft_objectMouseOver
		&& minecraft_clickMouse && minecraft_leftClickCounter
		&& minecraft_getMinecraft
		&& world_playerEntities && list_toArray;
}

void StrayCache::DeleteRefs()
{
	JNIEnv* env = Java::GetEnv();
	if (!env) return;

	auto drop = [&](jclass& c) {
		if (c) { env->DeleteGlobalRef(c); c = nullptr; }
	};
	drop(entity_class);
	drop(entityLivingBase_class);
	drop(renderManager_class);
	drop(entityPlayer_class);
	drop(entityPlayerSP_class);
	drop(axisAlignedBB_class);
	drop(movingObjectPosition_class);
	drop(vec3_class);
	drop(inventoryPlayer_class);
	drop(itemStack_class);
	drop(item_class);
	drop(itemBlock_class);
	drop(list_class);
	drop(set_class);
	drop(playerControllerMP_class);
	drop(enchantmentHelper_class);
	drop(iChatComponent_class);
	drop(espBridge_class);
	drop(guiInventory_class);
	initialized = false;
}

bool StrayCache::EnsureEspBridge()
{
	if (espBridge_class)
		return true;

	static std::mutex mtx;
	std::lock_guard<std::mutex> lock(mtx);

	if (espBridge_class)
		return true;

	JNIEnv* env = Java::GetEnv();
	if (!env || !Java::Initialized)
		return false;

	if (Java::AssignClass("io.github.lefraudeur.EspBridge", espBridge_class) && espBridge_class)
	{
		espBridge_isMultiplayerScreen = env->GetStaticMethodID(espBridge_class, "isMultiplayerScreen", "()Z");
		if (env->ExceptionCheck()) env->ExceptionClear();
		
		espBridge_displayDummyScreen = env->GetStaticMethodID(espBridge_class, "displayDummyScreen", "(Z)V");
		if (env->ExceptionCheck()) env->ExceptionClear();

		espBridge_getSessionUsername = env->GetStaticMethodID(espBridge_class, "getSessionUsername", "()Ljava/lang/String;");
		if (env->ExceptionCheck()) env->ExceptionClear();

		espBridge_changeSession = env->GetStaticMethodID(espBridge_class, "changeSession", "(Ljava/lang/String;)V");
		if (env->ExceptionCheck()) env->ExceptionClear();

		espBridge_onGetMouseOverPost = env->GetStaticMethodID(espBridge_class, "onGetMouseOverPost", "(F)V");
		if (env->ExceptionCheck()) env->ExceptionClear();

		Logger::Log("[StrayCache] EspBridge dynamic resolution succeeded.");
		return true;
	}
	return false;
}
