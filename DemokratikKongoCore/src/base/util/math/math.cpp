#include "math.h"

#include <cmath>

float Math::wrapAngleTo180(float angle)
{
	angle = std::fmod(angle, 360.0f);
	if (angle >= 180.0f) {
		angle -= 360.0f;
	}

	if (angle < -180.0f) {
		angle += 360.0f;
	}

	return angle;
}

Vector2 Math::vec_wrapAngleTo180(Vector2 angle)
{
	return Vector2{
		wrapAngleTo180(angle.x),
		wrapAngleTo180(angle.y),
	};
}

float Math::coterminal(float angle) {
	// Canonical coterminal angle in [-180, 180). The previous body added a
	// magic 170 degrees, which is not a coterminal transform at all.
	return wrapAngleTo180(angle);
}


float Math::magnitude(Vector3 v)
{
	return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 Math::crossProduct(Vector3 v1, Vector3 v2)
{
	return Vector3{
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x
	};
}

float Math::shortestDistance(Vector3 p, Vector3 a, Vector3 b)
{

	Vector3 ab{ b.x - a.x, b.y - a.y, b.z - a.z };
	Vector3 cp = crossProduct(
		Vector3{p.x - a.x, p.y - a.y, p.z - a.z},
		ab
	);

	return magnitude(cp) / magnitude(ab);
}

Vector2 Math::getAngles(Vector3 pos, Vector3 pos1)
{
	double d_x = pos1.x - pos.x;
	double d_y = pos1.y - pos.y;
	double d_z = pos1.z - pos.z;

	double hypothenuse = sqrt(d_x * d_x + d_z * d_z);

	// atan2 returns [-180, 180], so the -90 offset pushed yaw down to -270.
	// Wrapping keeps the result in MC's [-180, 180] range; without it every
	// consumer that compared raw yaw values could be a full turn off.
	float yaw = wrapAngleTo180(radiantsToDeg((float)atan2(d_z, d_x)) - 90.f);

	// Straight up / down: hypotenuse is 0 and atan2(dy, 0) is well defined
	// (+/-90), but guard against the degenerate all-zero delta.
	float pitch = 0.0f;
	if (hypothenuse > 1e-6 || d_y != 0.0)
		pitch = radiantsToDeg((float)-atan2(d_y, hypothenuse));
	if (pitch > 90.0f)  pitch = 90.0f;
	if (pitch < -90.0f) pitch = -90.0f;

	return Vector2(yaw, pitch);
}

Vector3 Math::getLookVector(float yaw, float pitch)
{
	// Matches Minecraft 1.8 Entity.getLook(partialTicks).
	const float yr = degToRadiants(-yaw) - (float)PI;
	const float pr = degToRadiants(-pitch);
	const float cosY = cosf(yr);
	const float sinY = sinf(yr);
	const float cosP = -cosf(pr);
	const float sinP = sinf(pr);
	return Vector3{ sinY * cosP, sinP, cosY * cosP };
}

float Math::radiantsToDeg(float x)
{
	return (float)(x * 180.f / PI);
}

float Math::degToRadiants(float x)
{
	return (float)(x * PI / 180.f);
}

