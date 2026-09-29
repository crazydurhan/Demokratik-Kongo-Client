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

	// Negative cache for the shape-based field lookup. Resolving PROJECTION /
	// MODELVIEW by matrix shape costs up to 9 GetStaticFieldID plus 9*16
	// CallFloatMethod per attempt, and both accessors are called every
	// world-pass frame. On a client where the shape never matches, retrying
	// forever means ~300 extra JNI calls per frame indefinitely. These flags
	// are only ever set on the client thread that runs SDK::Init().
	bool projectionLookupTried = false;
	bool modelViewLookupTried = false;
};

