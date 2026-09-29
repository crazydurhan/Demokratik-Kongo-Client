#pragma once

#include "../../../../java/IClass.h"
#include "../../../../../util/math/geometry.h"


struct CActiveRenderInfo : IClass
{
	CActiveRenderInfo();

	jclass GetClass();
	jobject GetInstance();

	Matrix ProjectionMatrix();
	Matrix ModelViewMatrix();

	// Bounded retry budget for the shape-based field lookup. Resolving
	// PROJECTION / MODELVIEW by matrix shape costs up to 9 GetStaticFieldID
	// plus 9*16 CallFloatMethod per attempt.
	//
	// The constructor runs from SDK::Init(), long before Minecraft has
	// rendered a frame, when the buffers are still all zero and the shape test
	// cannot match. The per-frame retry is what recovers from that, so it must
	// stay -- but a client whose matrices genuinely do not match must not pay
	// that cost on every frame forever, hence the budget: enough frames to
	// cover the startup window, then it gives up.
	enum { kMatrixLookupBudget = 240 };
	int projectionRetriesLeft = kMatrixLookupBudget;
	int modelViewRetriesLeft = kMatrixLookupBudget;
};

