#include "mappingLoader.h"

#include "../util/logger.h"

#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <Windows.h>

namespace
{
	std::unordered_map<std::string, std::vector<std::string>> g_aliases;

	void addAlias(const char* mcp, const char* alt)
	{
		if (!mcp || !alt || !*mcp || !*alt) return;
		auto& list = g_aliases[mcp];
		for (const auto& existing : list)
			if (existing == alt) return;
		list.push_back(alt);
	}

	void seedAliases()
	{
		// MCP name first, then known 1.8.9 / Lunar obf + SRG fallbacks.
		struct Seed { const char* mcp; const char* const* alts; size_t count; };
		static const char* kFallDistance[] = { "O", "field_70143_R" };
		static const char* kHurtTime[] = { "au", "field_70737_aN" };
		static const char* kJumpTicks[] = { "field_70773_bE" };
		static const char* kClickMouse[] = { "ax", "func_147116_af" };
		static const char* kLeftClick[] = { "ah", "field_71429_W" };
		static const char* kRightClickDelay[] = { "field_71467_ac", "ap" };
		static const char* kInGameHasFocus[] = { "field_71415_G" };
		static const char* kRightClickMouse[] = { "func_147121_ag" };
		static const char* kWindowClick[] = { "func_78753_a" };
		static const char* kSyncCurrentPlayItem[] = { "func_78750_j" };
		static const char* kMainInventory[] = { "field_70462_a" };
		static const char* kCurrentItem[] = { "field_70461_c" };
		// EntityPlayer.inventory (InventoryPlayer) — needed by StrayCache and CEntityPlayer
		static const char* kInventory[] = { "field_71071_by" };
		// WorldClient.entityList (Set) — best effort; currently unused path
		static const char* kEntityList[] = { "field_73032_d" };
		static const char* kAttackEntity[] = { "func_78764_a" };
		static const char* kGetHeldItem[] = { "func_70694_bm" };
		static const char* kSwingItem[] = { "func_71038_c" };
		static const char* kGetDisplayName[] = { "func_82833_r" };
		static const char* kGetItem[] = { "func_77973_b" };
		static const char* kGetIdFromItem[] = { "func_150891_b" };
		static const char* kGetCurrentArmor[] = { "func_82169_q" };
		static const char* kGetFormattedText[] = { "func_150260_c" };
		static const char* kGetEnchantmentLevel[] = { "func_77506_a" };
		static const char* kIsInvisible[] = { "func_82150_aj" };
		static const char* kGetDisplayNameEntity[] = { "func_145748_c_" };
		static const char* kGetMaxDamage[] = { "func_77958_k" };
		static const char* kStackSize[] = { "field_77994_a" };
		// field_73010_i = playerEntities. NOT field_72996_f — that is loadedEntityList,
		// which RESOLVES successfully on SRG clients but returns every loaded entity
		// (mobs, items, ...) instead of players, silently corrupting CommonData.
		static const char* kPlayerEntities[] = { "field_73010_i" };
		static const char* kSendChatMessage[] = { "func_71165_d" };
		static const char* kRenderManager[] = { "field_175616_W", "aa" };
		static const char* kRenderPosX[] = { "field_78725_b", "o" };
		static const char* kRenderPosY[] = { "field_78726_c", "p" };
		static const char* kRenderPosZ[] = { "field_78727_d", "q" };
		static const char* kPlayerViewX[] = { "field_78732_j", "j" };
		static const char* kPlayerViewY[] = { "field_78735_i", "i" };
		static const char* kMoveForward[] = { "field_70701_bs", "ba" };
		static const char* kMoveStrafing[] = { "field_70702_br", "bb" };
		static const char* kTimer[] = { "field_71428_T", "Q" };
		static const char* kIngameGUI[] = { "field_71456_v", "q" };
		static const char* kRemovePotionEffect[] = { "func_70618_k", "func_82170_o", "removePotionEffectClient" };
		static const char* kIsPotionActive[] = { "func_70644_a", "func_82165_m" };
		static const char* kIsAirBlock[] = { "func_175623_d" };
		static const char* kGetBlockState[] = { "func_180495_p" };
		static const char* kGetBlock[] = { "func_177230_c" };
		static const char* kGetStrVsBlock[] = { "func_150997_a" };
		static const char* kGetIdFromBlock[] = { "func_149682_b" };
		// GameSettings keybinds (InvMove / SafeWalk drive these via KeyBinding states)
		static const char* kKeyBindAttack[] = { "field_74312_F" };
		static const char* kKeyBindUseItem[] = { "field_74313_G" };
		static const char* kKeyBindForward[] = { "field_74351_w" };
		static const char* kKeyBindBack[] = { "field_74368_y" };
		static const char* kKeyBindLeft[] = { "field_74370_x" };
		static const char* kKeyBindRight[] = { "field_74366_z" };
		static const char* kKeyBindJump[] = { "field_74314_A" };
		static const char* kKeyBindSneak[] = { "field_74311_E" };

		const Seed seeds[] = {
			{ "fallDistance", kFallDistance, sizeof(kFallDistance) / sizeof(kFallDistance[0]) },
			{ "hurtTime", kHurtTime, sizeof(kHurtTime) / sizeof(kHurtTime[0]) },
			{ "jumpTicks", kJumpTicks, sizeof(kJumpTicks) / sizeof(kJumpTicks[0]) },
			{ "clickMouse", kClickMouse, sizeof(kClickMouse) / sizeof(kClickMouse[0]) },
			{ "leftClickCounter", kLeftClick, sizeof(kLeftClick) / sizeof(kLeftClick[0]) },
			{ "rightClickDelayTimer", kRightClickDelay, sizeof(kRightClickDelay) / sizeof(kRightClickDelay[0]) },
			{ "inGameHasFocus", kInGameHasFocus, sizeof(kInGameHasFocus) / sizeof(kInGameHasFocus[0]) },
			{ "rightClickMouse", kRightClickMouse, sizeof(kRightClickMouse) / sizeof(kRightClickMouse[0]) },
			{ "windowClick", kWindowClick, sizeof(kWindowClick) / sizeof(kWindowClick[0]) },
			{ "syncCurrentPlayItem", kSyncCurrentPlayItem, sizeof(kSyncCurrentPlayItem) / sizeof(kSyncCurrentPlayItem[0]) },
		{ "mainInventory", kMainInventory, sizeof(kMainInventory) / sizeof(kMainInventory[0]) },
		{ "currentItem", kCurrentItem, sizeof(kCurrentItem) / sizeof(kCurrentItem[0]) },
		{ "inventory", kInventory, sizeof(kInventory) / sizeof(kInventory[0]) },
		{ "entityList", kEntityList, sizeof(kEntityList) / sizeof(kEntityList[0]) },
			{ "attackEntity", kAttackEntity, sizeof(kAttackEntity) / sizeof(kAttackEntity[0]) },
			{ "getHeldItem", kGetHeldItem, sizeof(kGetHeldItem) / sizeof(kGetHeldItem[0]) },
			{ "swingItem", kSwingItem, sizeof(kSwingItem) / sizeof(kSwingItem[0]) },
			{ "getDisplayName", kGetDisplayName, sizeof(kGetDisplayName) / sizeof(kGetDisplayName[0]) },
			{ "getItem", kGetItem, sizeof(kGetItem) / sizeof(kGetItem[0]) },
			{ "getIdFromItem", kGetIdFromItem, sizeof(kGetIdFromItem) / sizeof(kGetIdFromItem[0]) },
			{ "getCurrentArmor", kGetCurrentArmor, sizeof(kGetCurrentArmor) / sizeof(kGetCurrentArmor[0]) },
			{ "getFormattedText", kGetFormattedText, sizeof(kGetFormattedText) / sizeof(kGetFormattedText[0]) },
			{ "getEnchantmentLevel", kGetEnchantmentLevel, sizeof(kGetEnchantmentLevel) / sizeof(kGetEnchantmentLevel[0]) },
			{ "isInvisible", kIsInvisible, sizeof(kIsInvisible) / sizeof(kIsInvisible[0]) },
			{ "getDisplayName", kGetDisplayNameEntity, 1 },
			{ "getMaxDamage", kGetMaxDamage, sizeof(kGetMaxDamage) / sizeof(kGetMaxDamage[0]) },
			{ "stackSize", kStackSize, sizeof(kStackSize) / sizeof(kStackSize[0]) },
			{ "playerEntities", kPlayerEntities, sizeof(kPlayerEntities) / sizeof(kPlayerEntities[0]) },
			{ "sendChatMessage", kSendChatMessage, sizeof(kSendChatMessage) / sizeof(kSendChatMessage[0]) },
			{ "renderManager", kRenderManager, sizeof(kRenderManager) / sizeof(kRenderManager[0]) },
			{ "renderPosX", kRenderPosX, sizeof(kRenderPosX) / sizeof(kRenderPosX[0]) },
			{ "renderPosY", kRenderPosY, sizeof(kRenderPosY) / sizeof(kRenderPosY[0]) },
			{ "renderPosZ", kRenderPosZ, sizeof(kRenderPosZ) / sizeof(kRenderPosZ[0]) },
			{ "playerViewX", kPlayerViewX, sizeof(kPlayerViewX) / sizeof(kPlayerViewX[0]) },
			{ "playerViewY", kPlayerViewY, sizeof(kPlayerViewY) / sizeof(kPlayerViewY[0]) },
			{ "moveForward", kMoveForward, sizeof(kMoveForward) / sizeof(kMoveForward[0]) },
			{ "moveStrafing", kMoveStrafing, sizeof(kMoveStrafing) / sizeof(kMoveStrafing[0]) },
			{ "timer", kTimer, sizeof(kTimer) / sizeof(kTimer[0]) },
			{ "ingameGUI", kIngameGUI, sizeof(kIngameGUI) / sizeof(kIngameGUI[0]) },
		{ "removePotionEffect", kRemovePotionEffect, sizeof(kRemovePotionEffect) / sizeof(kRemovePotionEffect[0]) },
		{ "isPotionActive", kIsPotionActive, sizeof(kIsPotionActive) / sizeof(kIsPotionActive[0]) },
		{ "isAirBlock", kIsAirBlock, sizeof(kIsAirBlock) / sizeof(kIsAirBlock[0]) },
		{ "getBlockState", kGetBlockState, sizeof(kGetBlockState) / sizeof(kGetBlockState[0]) },
		{ "getBlock", kGetBlock, sizeof(kGetBlock) / sizeof(kGetBlock[0]) },
		{ "getStrVsBlock", kGetStrVsBlock, sizeof(kGetStrVsBlock) / sizeof(kGetStrVsBlock[0]) },
		{ "getIdFromBlock", kGetIdFromBlock, sizeof(kGetIdFromBlock) / sizeof(kGetIdFromBlock[0]) },
		{ "keyBindAttack", kKeyBindAttack, sizeof(kKeyBindAttack) / sizeof(kKeyBindAttack[0]) },
		{ "keyBindUseItem", kKeyBindUseItem, sizeof(kKeyBindUseItem) / sizeof(kKeyBindUseItem[0]) },
		{ "keyBindForward", kKeyBindForward, sizeof(kKeyBindForward) / sizeof(kKeyBindForward[0]) },
		{ "keyBindBack", kKeyBindBack, sizeof(kKeyBindBack) / sizeof(kKeyBindBack[0]) },
		{ "keyBindLeft", kKeyBindLeft, sizeof(kKeyBindLeft) / sizeof(kKeyBindLeft[0]) },
		{ "keyBindRight", kKeyBindRight, sizeof(kKeyBindRight) / sizeof(kKeyBindRight[0]) },
		{ "keyBindJump", kKeyBindJump, sizeof(kKeyBindJump) / sizeof(kKeyBindJump[0]) },
		{ "keyBindSneak", kKeyBindSneak, sizeof(kKeyBindSneak) / sizeof(kKeyBindSneak[0]) },
	};

		for (const Seed& s : seeds)
		{
			addAlias(s.mcp, s.mcp);
			for (size_t i = 0; i < s.count; ++i)
				addAlias(s.mcp, s.alts[i]);
		}
	}

	std::string scrubContent(const std::string& raw)
	{
		std::string out;
		out.reserve(raw.size());
		for (unsigned char c : raw)
		{
			if (c == '\n' || c == '\r' || (c >= 32 && c < 127))
				out.push_back((char)c);
		}
		return out;
	}

	void augmentFromFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return;

		std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		const std::string content = scrubContent(raw);
		if (content.empty())
			return;

		Logger::Log("[MappingLoader] Loaded mapping file: " + path);

		for (const auto& entry : g_aliases)
		{
			const std::string& mcp = entry.first;
			size_t pos = 0;
			while ((pos = content.find(mcp, pos)) != std::string::npos)
			{
				size_t i = pos + mcp.size();
				while (i < content.size() && (unsigned char)content[i] < 32)
					++i;

				std::string obf;
				while (i < content.size() && std::isalnum((unsigned char)content[i]))
					obf.push_back(content[i++]);

				if (!obf.empty() && obf != mcp && obf.size() <= 32)
					addAlias(mcp.c_str(), obf.c_str());

				pos = i;
			}
		}
	}
}

void MappingLoader::Init()
{
	g_aliases.clear();
	seedAliases();

	// Get mappings directory from environment variable, fallback to default
	char mappingsDir[MAX_PATH] = {0};
	if (GetEnvironmentVariableA("DEMOKRATIKKONGO_MAPPINGS", mappingsDir, MAX_PATH) == 0)
	{
		// Default to current directory if env var not set
		GetCurrentDirectoryA(MAX_PATH, mappingsDir);
	}

	std::string dir = mappingsDir;
	static const char* kFileNames[] = {
		"lunar_named_b5_1.8.9.txt",
		"v1_8_inflight_vanilla.txt",
		"v1_8_inflight_optifine.txt",
		"v1_8_inflight_forge.txt",
		"v1_8_inflight_optiforge.txt",
	};

	for (const char* fileName : kFileNames)
	{
		std::string path = dir + "\\" + fileName;
		augmentFromFile(path.c_str());
	}
}

void MappingLoader::AddAlias(const char* mcpName, const char* alt)
{
	addAlias(mcpName, alt);
}

const std::vector<std::string>& MappingLoader::Names(const char* mcpName)
{
	static const std::vector<std::string> kEmpty;
	if (!mcpName)
		return kEmpty;
	auto it = g_aliases.find(mcpName);
	if (it == g_aliases.end())
	{
		static std::unordered_map<std::string, std::vector<std::string>> kFallback;
		auto fb = kFallback.find(mcpName);
		if (fb == kFallback.end())
		{
			kFallback[mcpName] = { mcpName };
			return kFallback[mcpName];
		}
		return fb->second;
	}
	return it->second;
}
