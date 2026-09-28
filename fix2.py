# -*- coding: utf-8 -*-
p='DemokratikKongoCore/src/base/moduleManager/modules/render/xrayBypass.cpp'
s=open(p,encoding='utf-8').read()

# onTick gate: scan while GUI is open (drop the IsInGuiState dependency);
# module still stops when out of world / dead.
old='''    if (!CombatBridge::InGame() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        m_marks.clear();
        m_lastScanMs = 0;
        return;
    }'''
new='''    if (!CombatBridge::CanCombat() || !CommonData::SanityCheck() ||
        !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        std::lock_guard<std::mutex> lock(m_marksMutex);
        m_marks.clear();
        m_lastScanMs = 0;
        return;
    }'''
assert old in s, 'gate'
s=s.replace(old,new)

# rescan logic: one-shot flow (first tick / recenter only)
old='''    const long long scanDelayMs = std::max<long long>(200, static_cast<long long>(m_scanDelay->value));

    // A pass is atomic: the cursor runs to completion with no periodic resets.
    // A new pass starts only when the player left the box (recenter), on first
    // tick, or after the previous pass finished and ScanDelay elapsed.
    const bool firstEver = (m_lastScanMs == 0);
    const bool afterDone = m_passDone && (now - m_lastScanMs >= scanDelayMs);
    if (outsideBox || firstEver || afterDone)
    {
        m_lastScanMs = now;
        m_passDone = false;
        resetScan();
    }'''
new='''    // One-shot flow: a pass is atomic (cursor runs to completion, no periodic
    // resets) and starts on the first tick after enable or when the player
    // left the previous box. When it finishes, the module auto-disables.
    const bool firstEver = (m_lastScanMs == 0);
    if (outsideBox || firstEver)
    {
        m_lastScanMs = now;
        m_passDone = false;
        resetScan();
    }'''
assert old in s, 'rescan'
s=s.replace(old,new)

# pass complete: one-shot auto-disable (marks are kept and still rendered)
old='''            g_diag.summarize();
            Logger::Info("XrayBypass", "Pass complete — " + std::to_string(m_marks.size())
                + " blocks marked.");
            m_passDone = true;   // onTick starts the next pass after ScanDelay
            return;'''
new='''            g_diag.summarize();
            m_passDone = true;
            Logger::Info("XrayBypass", "Pass complete — " + std::to_string(m_marks.size())
                + " blocks marked. Auto-disabling (one-shot scan).");
            setEnabled(false);   // re-enable to scan again from the new position
            return;'''
assert old in s, 'complete'
s=s.replace(old,new)

# heartbeat: add passDone flag
old='''            + " marks=" + std::to_string(m_marks.size())'''
new='''            + " marks=" + std::to_string(m_marks.size())
            + " passDone=" + std::to_string(m_passDone ? 1 : 0)'''
assert old in s, 'beat'
s=s.replace(old,new)

open(p,'w',encoding='utf-8',newline='').write(s)
print('part2 ok')