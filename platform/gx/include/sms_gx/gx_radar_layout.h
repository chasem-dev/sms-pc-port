#pragma once

namespace gx {
namespace RadarPlace { enum Value { Corner, Yield, Beside, Below, BottomLeft }; }
struct RadarLayout { float x, y, radius; };
}

// While drawing the small radar, use its own centred projection and fit.
// False leaves the legacy Centre placement and drawing untouched.
extern "C" bool GXPC_RadarBegin(gx::RadarPlace::Value place, float radius, gx::RadarLayout* layout);
extern "C" void GXPC_RadarEnd();
