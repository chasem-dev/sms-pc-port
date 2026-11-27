#pragma once
// Draw roles for both the retail layouts and the port's own menu art.
namespace gx {
namespace dock { enum Value { None, TopLeft, Top, TopRight, Right, BottomLeft, Bottom, BottomRight, Centre,
                  TopBand, BottomBand }; }
typedef dock::Value Dock;
namespace menu { enum Value { None, Pause, Load, Option, Episode, GameplayHud }; }
typedef menu::Value MenuScreen;
}
extern "C" gx::MenuScreen GXPC_MenuScreen(const char* filename);
extern "C" gx::MenuScreen GXPC_SetMenuScreen(gx::MenuScreen screen);
extern "C" void GXPC_MenuPieceBegin(gx::Dock dock, float legacyLeft = 0, float legacyRight = 0);
extern "C" void GXPC_MenuPieceEnd();
