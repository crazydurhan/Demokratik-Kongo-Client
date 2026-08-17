#include "worldToScreen.h"
#include "../../base.h"
#include "../logger.h"

#include <limits>

Vector4 CWorldToScreen::Multiply(Vector4 v, Matrix m)
{
	return Vector4{
		v.x * m.m00 + v.y * m.m10 + v.z * m.m20 + v.w * m.m30,
		v.x * m.m01 + v.y * m.m11 + v.z * m.m21 + v.w * m.m31,
		v.x * m.m02 + v.y * m.m12 + v.z * m.m22 + v.w * m.m32,
		v.x * m.m03 + v.y * m.m13 + v.z * m.m23 + v.w * m.m33
	};
}

static bool WorldToScreenInternal(Vector3 point, Matrix modelView, Matrix projection,
	int screenWidth, int screenHeight, Vector2& screenPos, bool allowDepthOverflow)
{
	Vector4 csp = CWorldToScreen::Multiply(
		CWorldToScreen::Multiply(
			Vector4{ point.x, point.y, point.z, 1.0f },
			modelView
		),
		projection
	);

	// w <= 0 means the point is at or behind the camera plane; only rejecting
	// |w| ~ 0 let points behind the viewer project to mirrored coordinates.
	if (csp.w <= 1e-6f) return false;

	Vector3 ndc{
		csp.x / csp.w,
		csp.y / csp.w,
		csp.z / csp.w
	};

	// Inside the normal depth range is always valid. allowDepthOverflow only
	// *additionally* accepts points slightly past the far plane (the old code
	// accepted the overflow band exclusively, so every genuinely visible point
	// was rejected).
	const bool inRange = ndc.z >= -1.0f && ndc.z <= 1.0f;
	const bool inOverflow = allowDepthOverflow && ndc.z > 1.0f && ndc.z < 1.15f;
	if (!inRange && !inOverflow) return false;

	screenPos = Vector2{
		((ndc.x + 1.0f) / 2.0f) * screenWidth,
		((1.0f - ndc.y) / 2.0f) * screenHeight,
	};
	return true;
}

bool CWorldToScreen::WorldToScreen(Vector3 point, Matrix modelView, Matrix projection, int screenWidth, int screenHeight, Vector2& screenPos)
{
	return WorldToScreenInternal(point, modelView, projection, screenWidth, screenHeight, screenPos, true);
}

bool CWorldToScreen::WorldToScreenVisible(Vector3 point, Matrix modelView, Matrix projection, int screenWidth, int screenHeight, Vector2& screenPos)
{
	return WorldToScreenInternal(point, modelView, projection, screenWidth, screenHeight, screenPos, false);
}
