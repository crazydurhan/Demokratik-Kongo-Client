#include "rotationHelper.h"
#include "../patcher/patcher.h"

#include <sstream>

void RotationHelper::requestRotation(float yaw, float pitch,
                                     const char* source,
                                     Priority    priority)
{
    std::lock_guard<std::mutex> lock(m_mtx);

    if (!m_haveCandidate || (int)priority > (int)m_candidatePriority)
    {
        m_haveCandidate     = true;
        m_candidatePriority = priority;
        m_candidateYaw      = yaw;
        m_candidatePitch    = pitch;
        m_candidateSource   = source ? source : "";
    }
}

void RotationHelper::flush()
{
    bool   active = false;
    float  yaw    = 0.0f;
    float  pitch  = 0.0f;
    const char* src = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_haveCandidate)
        {
            active = true;
            yaw    = m_candidateYaw;
            pitch  = m_candidatePitch;
            src    = m_candidateSource;
        }

        // Mirror into the read-only state for HUD readback.
        m_active        = active;
        m_currentYaw    = yaw;
        m_currentPitch  = pitch;
        m_currentSource = src;

        // Reset pending winner so each module must re-arm next tick.
        m_haveCandidate     = false;
        m_candidatePriority = Priority::None;
        m_candidateSource   = nullptr;
    }

    // Push to the JVM-side EMPTY_MAP. The ASM patch on
    // EntityPlayerSP.onUpdateWalkingPlayer reads these on every game
    // tick to decide whether to swap rotationYaw/rotationPitch before
    // packets get assembled.
    if (active)
    {
        std::ostringstream oy, op;
        oy << yaw;
        op << pitch;
        Patcher::put("silent_yaw", oy.str());
        Patcher::put("silent_pitch", op.str());
        Patcher::put("silent_rotation_active", "true");
        m_lastJvmActive = true;
    }
    else if (m_lastJvmActive)
    {
        // Only write "false" once when transitioning out of active so
        // we don't spam Patcher::put every idle tick.
        Patcher::put("silent_rotation_active", "false");
        m_lastJvmActive = false;
    }
}
