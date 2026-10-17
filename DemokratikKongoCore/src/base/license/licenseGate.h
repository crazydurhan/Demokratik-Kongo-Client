#pragma once

/*
========================================================================
    PROJECTX :: LicenseGate
------------------------------------------------------------------------
    Phase-2 licensing: the core DLL verifies the licence on its OWN,
    independently of the launcher. Fail-closed - Active() starts false
    and only flips true after a server validation carrying this
    machine's HWID, or from a fresh local record inside the offline
    grace window (see LISANS_VE_YUKLEME_STANDARTLARI.md, sections 3 & 7).

    The launcher writes the DPAPI record (%APPDATA%\RuntimeHost\
    license.dat); the core only ever READS it. A DLL copied to another
    machine therefore has no record (or one that cannot be decrypted by
    that user) and starts locked.
========================================================================
*/

struct LicenseGate
{
    // Starts the verifier thread: an immediate check, then a 30-minute
    // heartbeat. Safe to call once during boot; further calls are ignored.
    static void Init();

    // Stops and joins the worker. Safe to call without Init.
    static void Kill();

    // Cached, thread-safe, fail-closed. Module dispatch points and render
    // passes consult this before doing any work.
    static bool Active();
};
