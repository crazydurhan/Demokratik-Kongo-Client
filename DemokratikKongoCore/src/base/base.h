#pragma once

#include <atomic>

struct Base
{
	static void Init();
	static void Kill();

	static void CheatLoop();
	static void RenderLoop();

	static void WaitForRenderQuiescent();

	// Running state is exposed via accessors whose atomic lives in base.cpp
	// only. A shared std::atomic<bool> variable recorded as different types
	// across TUs (align(1) vs natural) and triggered LNK C4744 under LTCG.
	static bool IsRunning();
	static void SetRunning(bool v);

	static std::atomic<bool> ShuttingDown;
	static std::atomic<int> RenderHookDepth;

	// Lifecycle safety tracking flags
	static inline bool MH_Initialized;
	static inline bool Java_Initialized;
	static inline bool SDK_Initialized;
	static inline bool Patcher_Initialized;
	static inline bool Menu_Initialized;
	static inline bool ModuleManager_Initialized;
};

