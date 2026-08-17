#pragma once
#include "../java/java.h"
#include "../util/logger.h"
#include <string>

struct StrayCache {

	inline static bool initialized;

	// ENTITY CLASS
	inline static jclass entity_class;
	inline static jmethodID entity_getName;
	inline static jmethodID entity_isSneaking;
	inline static jmethodID entity_isInvisible;
	inline static jfieldID entity_posX;
	inline static jfieldID entity_posY;
	inline static jfieldID entity_posZ;
	inline static jfieldID entity_lastTickPosX;
	inline static jfieldID entity_lastTickPosY;
	inline static jfieldID entity_lastTickPosZ;
	inline static jfieldID entity_width;
	inline static jfieldID entity_height;
	inline static jfieldID entity_distanceWalkedModified;
	inline static jfieldID entity_prevDistanceWalkedModified;
	inline static jfieldID entity_rotationYaw;
	inline static jfieldID entity_rotationPitch;
	inline static jfieldID entity_prevRotationYaw;
	inline static jfieldID entity_prevRotationPitch;
	inline static jfieldID entity_boundingBox;
	inline static jfieldID entity_motionX;
	inline static jfieldID entity_motionY;
	inline static jfieldID entity_motionZ;
	inline static jmethodID entity_setSprinting;
	inline static jmethodID entity_isSprinting;
	inline static jmethodID entity_isInWater;
	inline static jfieldID entity_onGround;
	inline static jfieldID entity_fallDistance;

	// ENTITY LIVING BASE CLASS
	inline static jclass entityLivingBase_class;
	inline static jmethodID entityLivingBase_getHealth;
	inline static jmethodID entityLivingBase_getMaxHealth;
	inline static jmethodID entityLivingBase_canEntityBeSeen;
	inline static jmethodID entityLivingBase_getHeldItem;
	inline static jmethodID entityLivingBase_swingItem;
	inline static jfieldID entityLivingBase_hurtTime;
	inline static jfieldID entityLivingBase_jumpTicks;
	inline static jmethodID entityLivingBase_removePotionEffect;
	inline static jmethodID entityLivingBase_isPotionActive;
	inline static jfieldID entityLivingBase_moveForward;
	inline static jfieldID entityLivingBase_moveStrafing;

	// RENDER MANAGER CLASS
	inline static jclass renderManager_class;
	inline static jfieldID renderManager_renderPosX;
	inline static jfieldID renderManager_renderPosY;
	inline static jfieldID renderManager_renderPosZ;
	inline static jfieldID renderManager_playerViewX;
	inline static jfieldID renderManager_playerViewY;

	// ENTITY PLAYER CLASS
	inline static jclass entityPlayer_class;
	inline static jfieldID entityPlayer_inventory;
	inline static jmethodID entityPlayer_getCurrentArmor;

	// ENTITY PLAYER SP CLASS
	inline static jclass entityPlayerSP_class;
	inline static jmethodID entityPlayerSP_sendChatMessage;
	inline static jfieldID entityPlayerSP_timeInPortal;
	inline static jfieldID entityPlayerSP_prevTimeInPortal;

	// ENCHANTMENT HELPER CLASS
	inline static jclass enchantmentHelper_class;
	inline static jmethodID enchantmentHelper_getEnchantmentLevel;

	// ICHAT COMPONENT CLASS
	inline static jclass iChatComponent_class;
	inline static jmethodID iChatComponent_getFormattedText;

	// ENTITY GET DISPLAY NAME METHOD
	inline static jmethodID entity_getDisplayName;


	// AXIS ALIGNED BB CLASS
	inline static jclass axisAlignedBB_class;
	inline static jfieldID axisAlignedBB_minX;
	inline static jfieldID axisAlignedBB_minY;
	inline static jfieldID axisAlignedBB_minZ;
	inline static jfieldID axisAlignedBB_maxX;
	inline static jfieldID axisAlignedBB_maxY;
	inline static jfieldID axisAlignedBB_maxZ;

	// MOVING OBJECT POSITION CLASS
	inline static jclass movingObjectPosition_class;
	inline static jfieldID movingObjectPosition_hitVec;
	inline static jfieldID movingObjectPosition_blockPos;
	inline static jfieldID movingObjectPosition_sideHit;
	inline static jfieldID movingObjectPosition_typeOfHit;
	inline static jfieldID movingObjectPosition_entityHit;
	inline static jmethodID movingObjectPosition_initEntity;

	inline static jclass vec3_class;
	inline static jfieldID vec3_xCoord;
	inline static jfieldID vec3_yCoord;
	inline static jfieldID vec3_zCoord;

	inline static jclass inventoryPlayer_class;
	inline static jmethodID inventoryPlayer_getCurrentItem;
	inline static jfieldID inventoryPlayer_currentItem;
	inline static jfieldID inventoryPlayer_mainInventory;

	inline static jclass itemStack_class;
	inline static jmethodID itemStack_getItem;
	inline static jmethodID itemStack_getDisplayName;
	inline static jmethodID itemStack_getItemDamage;
	inline static jmethodID itemStack_getMaxDamage;
	inline static jfieldID itemStack_stackSize;

	inline static jclass item_class;
	inline static jmethodID item_getIdFromItem;

	inline static jclass itemBlock_class;

	// PlayerControllerMP
	inline static jclass playerControllerMP_class;
	inline static jmethodID playerControllerMP_getBlockReachDistance;
	inline static jmethodID playerControllerMP_extendedReach;
	inline static jmethodID playerControllerMP_attackEntity;
	inline static jmethodID playerControllerMP_onPlayerRightClick;
	inline static jmethodID playerControllerMP_windowClick;
	inline static jmethodID playerControllerMP_syncCurrentPlayItem;
	inline static jfieldID playerControllerMP_curBlockDamageMP;
	inline static jfieldID playerControllerMP_blockHitDelay;
	inline static jfieldID playerControllerMP_isHittingBlock;

	// Minecraft fields
	inline static jfieldID minecraft_playerController;
	inline static jfieldID minecraft_thePlayer;
	inline static jfieldID minecraft_theWorld;
	inline static jfieldID minecraft_gameSettings;
	inline static jfieldID minecraft_timer;
	inline static jfieldID minecraft_objectMouseOver;
	inline static jfieldID minecraft_currentScreen;
	inline static jfieldID minecraft_entityRenderer;
	inline static jfieldID minecraft_renderManager;
	inline static jfieldID minecraft_ingameGUI;
	inline static jfieldID minecraft_leftClickCounter;
	inline static jfieldID minecraft_rightClickDelayTimer;
	inline static jfieldID minecraft_inGameHasFocus;
	inline static jmethodID minecraft_clickMouse;
	inline static jmethodID minecraft_rightClickMouse;
	inline static jmethodID minecraft_getMinecraft;

	// World fields
	inline static jfieldID world_playerEntities;

	// LIST AND SET CLASSES
	inline static jclass list_class;
	inline static jclass guiInventory_class;
	inline static jmethodID list_toArray;
	inline static jclass set_class;
	inline static jmethodID set_toArray;

	// EspBridge cache
	inline static jclass espBridge_class;
	inline static jmethodID espBridge_isMultiplayerScreen;
	inline static jmethodID espBridge_displayDummyScreen;
	inline static jmethodID espBridge_getSessionUsername;
	inline static jmethodID espBridge_changeSession;
	inline static jmethodID espBridge_onGetMouseOverPost;

	static void Initialize();
	static bool IsReady();
	static void DeleteRefs();
	static bool EnsureEspBridge();
};