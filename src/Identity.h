#pragma once

// Single source of truth for product identity. Anything user-visible or format-visible
// derives from here so renaming never means hunting through string literals.

namespace vbd::identity
{
    inline constexpr auto productName  = "VIIBEDEALER";
    inline constexpr auto companyName  = "FLOOD";
    inline constexpr auto bundleId     = "com.flood.viibedealer";

    // Bumped only when the state format changes in a way that needs migration.
    inline constexpr int  stateVersion = 1;

    // Root tag of the serialised plugin state.
    inline constexpr auto stateTag     = "VIIBEDEALER_STATE";
    inline constexpr auto uiStateTag   = "ui";
    inline constexpr auto apvtsTag     = "params";
}
