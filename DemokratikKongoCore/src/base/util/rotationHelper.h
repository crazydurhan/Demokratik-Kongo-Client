#pragma once

#include <string>
#include <mutex>

/*
========================================================================
    PROJECTX :: RotationHelper
------------------------------------------------------------------------
    The single coordination point for SILENT rotations.

    Modules that need coordinated rotations can call:

        RotationHelper::I().requestRotation(yaw, pitch,
                                            "ModuleName",
                                            Priority::Combat);

    every onTick() that they want to drive the rotation. After all
    modules have ticked, the C++ cheat loop calls:

        RotationHelper::I().flush();

    which:
      - picks the highest-priority pending request,
      - converts it into the EMPTY_MAP key/value pairs the JVM-side
        ASM patch reads,
      - clears all pending requests so each module must re-arm next
        tick (no stale silent aim if a module disables itself).

    Java side: a patch on EntityPlayerSP.onUpdateWalkingPlayer reads
    the silent_rotation_active / silent_yaw / silent_pitch keys from
    ThreadContext.EMPTY_MAP and TEMPORARILY swaps the player's
    rotationYaw / rotationPitch fields so the outbound packets carry
    the silent values. The visual rotation never changes.

    Priority enum
        - None       : sentinel; never wins
        - Visual     : low priority, can be overridden (Scaffold look)
        - Movement   : middle priority (Speed/Bhop angle bias)
        - Combat     : highest sane priority for combat rotation requests
        - Forced     : reserved for hard overrides (Anti-AFK kicks)
========================================================================
*/

class RotationHelper
{
public:
    enum class Priority : int
    {
        None     = 0,
        Visual   = 10,
        Movement = 20,
        Combat   = 30,
        Forced   = 100,
    };

    static RotationHelper& I() { static RotationHelper r; return r; }

    // Modules call this every tick they want to dictate rotation. The
    // highest-priority winner is committed to the JVM in flush().
    void requestRotation(float yaw, float pitch,
                         const char* source,
                         Priority    priority);

    // Called once at the END of the cheat-loop tick (after every module
    // has had a chance to call requestRotation). Pushes the chosen
    // rotation to the JVM-side patch and clears pending state.
    void flush();

    // Read-only access for HUDs / debug overlays.
    bool        isActive()      const { std::lock_guard<std::mutex> l(m_mtx); return m_active; }
    float       currentYaw()    const { std::lock_guard<std::mutex> l(m_mtx); return m_currentYaw;   }
    float       currentPitch()  const { std::lock_guard<std::mutex> l(m_mtx); return m_currentPitch; }
    const char* currentSource() const { std::lock_guard<std::mutex> l(m_mtx); return m_currentSource ? m_currentSource : ""; }

private:
    RotationHelper() = default;

    mutable std::mutex m_mtx;

    // Pending winner, reset every flush().
    bool        m_haveCandidate = false;
    Priority    m_candidatePriority = Priority::None;
    float       m_candidateYaw   = 0.0f;
    float       m_candidatePitch = 0.0f;
    const char* m_candidateSource = nullptr;

    // Last-flushed state, kept around for HUD readback only.
    bool        m_active = false;
    float       m_currentYaw   = 0.0f;
    float       m_currentPitch = 0.0f;
    const char* m_currentSource = nullptr;

    // Tracks whether we wrote "true" to the JVM last tick so we can
    // skip redundant Patcher::put calls when nothing changed.
    bool        m_lastJvmActive = false;
};
