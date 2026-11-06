#pragma once

// Single source of truth for the licensing / update service.
//
// THIS IS THE ONLY PLACE the service host names and the Keygen account slug
// appear in the launcher. Change them here (or run tools/configure-service.ps1)
// and rebuild - nothing else has to be touched.
//
// The same values must match native/license_client.c in the client repository
// (DripLoader). Keep the two files in sync when switching providers.
//
// Placeholders below are intentionally NOT a working deployment: a release
// built without setting them will fail licence validation with a network
// error, which is the safe failure mode (no silent fallback to an old host).

namespace dk::service {

// Scheme-less host names (they are passed straight to WinHttpConnect).
inline constexpr wchar_t kApiHost[]    = L"api.drip.invalid";   // Keygen CE API
inline constexpr wchar_t kUpdateHost[] = L"drip.invalid";       // panel / update / token

// Keygen CE account slug that owns the licences.
inline constexpr wchar_t kAccount[] = L"drip";

// Licence endpoints. Keep the {account} segment equal to kAccount above.
inline constexpr wchar_t kPathValidateKey[] =
        L"/v1/accounts/drip/licenses/actions/validate-key";
inline constexpr wchar_t kPathMachines[] =
        L"/v1/accounts/drip/machines";

// Update / token endpoints.
inline constexpr wchar_t kPathLicenseToken[]  = L"/up/api/token";
inline constexpr wchar_t kPathCoreCurrent[]   = L"/up/dl/current";
inline constexpr wchar_t kPathLauncherInfo[]  = L"/up/dl/launcher/info";
inline constexpr wchar_t kPathLauncherExe[]   = L"/up/dl/launcher";

} // namespace dk::service
