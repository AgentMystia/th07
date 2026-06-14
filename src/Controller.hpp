#pragma once

// Controller module for th07. Handles keyboard / joystick / DirectInput
// controller state sampling and maps raw inputs to the abstract TouhouButton
// bitmask used by the rest of the game.
//
// Differences from th06 (verified against the th07 binary):
//   * TouhouButton gained two new bits: TH_BUTTON_D (0x2000) and
//     TH_BUTTON_R (0x4000).  Both are sampled from the keyboard ('D'/'R')
//     and from the DirectInput joystate (TH_BUTTON_D only).
//   * The shoot/focus conflict workaround is now gated on
//     g_Supervisor.cfg.unk26 (the per-character config byte) instead of
//     "shootButton == focusButton", and its thresholds changed (see
//     GetControllerInput).
//   * GetControllerInput always forwards focusButton to the SetButton*
//     helpers, regardless of the conflict branch.
//   * GetInput gained the 'D' and 'R' keyboard bindings.

#include "diffbuild.hpp"
#include "inttypes.hpp"

namespace th07
{
enum TouhouButton
{
    TH_BUTTON_SHOOT = 1 << 0,
    TH_BUTTON_BOMB = 1 << 1,
    TH_BUTTON_FOCUS = 1 << 2,
    TH_BUTTON_MENU = 1 << 3,
    TH_BUTTON_UP = 1 << 4,
    TH_BUTTON_DOWN = 1 << 5,
    TH_BUTTON_LEFT = 1 << 6,
    TH_BUTTON_RIGHT = 1 << 7,
    TH_BUTTON_SKIP = 1 << 8,
    TH_BUTTON_Q = 1 << 9,
    TH_BUTTON_S = 1 << 10,
    TH_BUTTON_HOME = 1 << 11,
    TH_BUTTON_ENTER = 1 << 12,
    TH_BUTTON_D = 1 << 13,
    TH_BUTTON_R = 1 << 14,

    TH_BUTTON_UP_LEFT = TH_BUTTON_UP | TH_BUTTON_LEFT,
    TH_BUTTON_UP_RIGHT = TH_BUTTON_UP | TH_BUTTON_RIGHT,
    TH_BUTTON_DOWN_LEFT = TH_BUTTON_DOWN | TH_BUTTON_LEFT,
    TH_BUTTON_DOWN_RIGHT = TH_BUTTON_DOWN | TH_BUTTON_RIGHT,
    TH_BUTTON_DIRECTION = TH_BUTTON_DOWN | TH_BUTTON_RIGHT | TH_BUTTON_UP | TH_BUTTON_LEFT,

    TH_BUTTON_SELECTMENU = TH_BUTTON_ENTER | TH_BUTTON_SHOOT,
    TH_BUTTON_RETURNMENU = TH_BUTTON_MENU | TH_BUTTON_BOMB,
    TH_BUTTON_WRONG_CHEATCODE =
        TH_BUTTON_SHOOT | TH_BUTTON_BOMB | TH_BUTTON_MENU | TH_BUTTON_Q | TH_BUTTON_S | TH_BUTTON_ENTER,
    TH_BUTTON_ANY = 0xFFFF,
};

namespace Controller
{
u16 __fastcall GetJoystickCaps(void);
u32 __fastcall SetButtonFromControllerInputs(u16 *outButtons, i16 controllerButtonToTest,
                                             enum TouhouButton touhouButton, u32 inputButtons);

u32 __fastcall SetButtonFromDirectInputJoystate(u16 *outButtons, i16 controllerButtonToTest,
                                                enum TouhouButton touhouButton, u8 *inputButtons);

u16 __fastcall GetControllerInput(u16 buttons);
u8 *GetControllerState();
u16 GetInput(void);
void ResetKeyboard(void);
}; // namespace Controller
}; // namespace th07
