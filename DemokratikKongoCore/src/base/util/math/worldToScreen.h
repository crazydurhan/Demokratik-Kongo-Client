#pragma once
#include "geometry.h"
class CWorldToScreen
{
public:
	static Vector4 Multiply(Vector4 vector, Matrix matrix);
	static bool WorldToScreen(Vector3 point, Matrix modelView, Matrix projection, int screenWidth, int screenHeight, Vector2& screenPos);
	/** Standard frustum test (-1 <= ndc.z <= 1) for on-screen ESP boxes. */
	static bool WorldToScreenVisible(Vector3 point, Matrix modelView, Matrix projection, int screenWidth, int screenHeight, Vector2& screenPos);
};

