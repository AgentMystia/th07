#include "Controller.hpp"

#include <dinput.h>

#include "GameErrorContext.hpp"
#include "Supervisor.hpp"
#include "i18n.hpp"
#include "utils.hpp"

namespace th07
{
DIFFABLE_STATIC(JOYCAPSA, g_JoystickCaps)
DIFFABLE_STATIC(u16, g_FocusButtonConflictState)

// NOTE: th07's i18n string for TH_ERR_NO_PAD_FOUND in the binary differs from
// the current th06-inherited i18n.tpl value. The binary string at 0x00497d9c
// is the Shift-JIS text "\r\n".
// The i18n.tpl macro is consumed as-is; updating it is out of scope for this
// module (belongs to the i18n module owner).

u16 __fastcall Controller::GetJoystickCaps(void)
{
    JOYINFOEX pji;

    pji.dwSize = sizeof(JOYINFOEX);
    pji.dwFlags = JOY_RETURNALL;

    if (joyGetPosEx(0, &pji) != MMSYSERR_NOERROR)
    {
        g_GameErrorContext.Log(TH_ERR_NO_PAD_FOUND);
        return 1;
    }

    joyGetDevCapsA(0, &g_JoystickCaps, sizeof(g_JoystickCaps));
    return 0;
}

#define JOYSTICK_MIDPOINT(min, max) ((min + max) / 2)
#define JOYSTICK_BUTTON_PRESSED(button, x, y) (x > y ? button : 0)
#define JOYSTICK_BUTTON_PRESSED_INVERT(button, x, y) (x < y ? button : 0)
#define KEYBOARD_KEY_PRESSED(button, x) keyboardState[x] & 0x80 ? button : 0

u16 __fastcall Controller::GetControllerInput(u16 buttons)
{
    // NOTE: Those names are like this to get perfect stack frame matching.
    JOYINFOEX aa;
    u32 ab;
    u32 ac;
    DIJOYSTATE2 a0;
    u32 a2;
    HRESULT aaa;

    if (g_Supervisor.controller == NULL)
    {
        memset(&aa, 0, sizeof(aa));
        aa.dwSize = sizeof(JOYINFOEX);
        aa.dwFlags = JOY_RETURNALL;

        if (joyGetPosEx(0, &aa) != MMSYSERR_NOERROR)
        {
            return buttons;
        }

        ac = SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.shootButton, TH_BUTTON_SHOOT,
                                           aa.dwButtons);

        // th07: the shoot/focus conflict workaround is gated on the
        // per-character config byte (cfg.unk26) instead of
        // "shootButton == focusButton", and its thresholds differ from th06.
        if (g_Supervisor.cfg.unk26 != 0)
        {
            if (ac != 0)
            {
                if (g_FocusButtonConflictState < 20)
                {
                    g_FocusButtonConflictState++;
                }

                if (g_FocusButtonConflictState > 9)
                {
                    buttons |= TH_BUTTON_FOCUS;
                }
            }
            else
            {
                if (g_FocusButtonConflictState < 11)
                {
                    g_FocusButtonConflictState = 0;
                }
                else
                {
                    g_FocusButtonConflictState -= 10;
                    buttons |= TH_BUTTON_FOCUS;
                }
            }
        }

        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.bombButton, TH_BUTTON_BOMB,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.focusButton, TH_BUTTON_FOCUS,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.menuButton, TH_BUTTON_MENU,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.upButton, TH_BUTTON_UP,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.downButton, TH_BUTTON_DOWN,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.leftButton, TH_BUTTON_LEFT,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.rightButton, TH_BUTTON_RIGHT,
                                      aa.dwButtons);
        SetButtonFromControllerInputs(&buttons, g_Supervisor.cfg.controllerMapping.skipButton, TH_BUTTON_SKIP,
                                      aa.dwButtons);

        ab = ((g_JoystickCaps.wXmax - g_JoystickCaps.wXmin) / 2 / 2);

        buttons |= JOYSTICK_BUTTON_PRESSED(TH_BUTTON_RIGHT, aa.dwXpos,
                                           JOYSTICK_MIDPOINT(g_JoystickCaps.wXmin, g_JoystickCaps.wXmax) + ab);
        buttons |= JOYSTICK_BUTTON_PRESSED(
            TH_BUTTON_LEFT, JOYSTICK_MIDPOINT(g_JoystickCaps.wXmin, g_JoystickCaps.wXmax) - ab, aa.dwXpos);

        ab = ((g_JoystickCaps.wYmax - g_JoystickCaps.wYmin) / 2 / 2);
        buttons |= JOYSTICK_BUTTON_PRESSED(TH_BUTTON_DOWN, aa.dwYpos,
                                           JOYSTICK_MIDPOINT(g_JoystickCaps.wYmin, g_JoystickCaps.wYmax) + ab);
        buttons |= JOYSTICK_BUTTON_PRESSED(
            TH_BUTTON_UP, JOYSTICK_MIDPOINT(g_JoystickCaps.wYmin, g_JoystickCaps.wYmax) - ab, aa.dwYpos);

        return buttons;
    }
    else
    {
        aaa = ((IDirectInputDevice8A *)g_Supervisor.controller)->Poll();
        if (FAILED(aaa))
        {
            i32 retryCount = 0;

            utils::DebugPrint2("error : DIERR_INPUTLOST\r\n");
            aaa = ((IDirectInputDevice8A *)g_Supervisor.controller)->Acquire();

            // th07: the log of the retry counter happens inside the loop,
            // before the increment; the loop keeps going while retryCount <
            // 400 and falls through to the function's bottom return when it
            // gives up.
            do
            {
                if (aaa != DIERR_INPUTLOST)
                {
                    return buttons;
                }
                aaa = ((IDirectInputDevice8A *)g_Supervisor.controller)->Acquire();
                utils::DebugPrint2("error : DIERR_INPUTLOST %d\r\n", retryCount);

                retryCount++;
            } while (retryCount < 400);
        }
        else
        {
            memset(&a0, 0, sizeof(a0));

            aaa = ((IDirectInputDevice8A *)g_Supervisor.controller)->GetDeviceState(sizeof(a0), &a0);

            if (FAILED(aaa))
            {
                return buttons;
            }

            a2 = SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.shootButton,
                                                  TH_BUTTON_SHOOT, a0.rgbButtons);

            if (g_Supervisor.cfg.unk26 != 0)
            {
                if (a2 != 0)
                {
                    if (g_FocusButtonConflictState < 20)
                    {
                        g_FocusButtonConflictState++;
                    }

                    if (g_FocusButtonConflictState > 9)
                    {
                        buttons |= TH_BUTTON_FOCUS;
                    }
                }
                else
                {
                    if (g_FocusButtonConflictState < 11)
                    {
                        g_FocusButtonConflictState = 0;
                    }
                    else
                    {
                        g_FocusButtonConflictState -= 10;
                        buttons |= TH_BUTTON_FOCUS;
                    }
                }
            }

            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.bombButton, TH_BUTTON_BOMB,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.focusButton, TH_BUTTON_FOCUS,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.menuButton, TH_BUTTON_MENU,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.upButton, TH_BUTTON_UP,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.downButton, TH_BUTTON_DOWN,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.leftButton, TH_BUTTON_LEFT,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.rightButton, TH_BUTTON_RIGHT,
                                             a0.rgbButtons);
            SetButtonFromDirectInputJoystate(&buttons, g_Supervisor.cfg.controllerMapping.skipButton, TH_BUTTON_SKIP,
                                             a0.rgbButtons);
            // th07: extra button binding sampled only on the DirectInput path.
            // Uses a hardcoded controller button index (7) rather than a
            // config-mapped field.
            SetButtonFromDirectInputJoystate(&buttons, 7, TH_BUTTON_D, a0.rgbButtons);

            buttons |= JOYSTICK_BUTTON_PRESSED(TH_BUTTON_RIGHT, a0.lX, g_Supervisor.cfg.padXAxis);
            buttons |= JOYSTICK_BUTTON_PRESSED_INVERT(TH_BUTTON_LEFT, a0.lX, -g_Supervisor.cfg.padXAxis);
            buttons |= JOYSTICK_BUTTON_PRESSED(TH_BUTTON_DOWN, a0.lY, g_Supervisor.cfg.padYAxis);
            buttons |= JOYSTICK_BUTTON_PRESSED_INVERT(TH_BUTTON_UP, a0.lY, -g_Supervisor.cfg.padYAxis);
        }
    }

    return buttons;
}

u32 __fastcall Controller::SetButtonFromDirectInputJoystate(u16 *outButtons, i16 controllerButtonToTest,
                                                            enum TouhouButton touhouButton, u8 *inputButtons)
{
    if (controllerButtonToTest < 0)
    {
        return 0;
    }

    *outButtons |= (inputButtons[controllerButtonToTest] & 0x80 ? (u16)touhouButton : 0);

    return inputButtons[controllerButtonToTest] & 0x80 ? (u16)touhouButton : 0;
}

u32 __fastcall Controller::SetButtonFromControllerInputs(u16 *outButtons, i16 controllerButtonToTest,
                                                         enum TouhouButton touhouButton, u32 inputButtons)
{
    DWORD mask;

    if (controllerButtonToTest < 0)
    {
        return 0;
    }

    mask = 1 << controllerButtonToTest;

    *outButtons |= (inputButtons & mask ? (u16)touhouButton : 0);

    return inputButtons & mask ? (u16)touhouButton : 0;
}

DIFFABLE_STATIC_ARRAY(u8, (32 * 4), g_ControllerData)

#pragma var_order(joyinfoex, joyButtonBit, joyButtonIndex, dires, dijoystate2, diRetryCount)
// This is for rebinding keys
u8 *th07::Controller::GetControllerState()
{
    JOYINFOEX joyinfoex;
    u32 joyButtonBit;
    u32 joyButtonIndex;

    i32 dires;
    DIJOYSTATE2 dijoystate2;
    i32 diRetryCount;

    memset(&g_ControllerData, 0, sizeof(g_ControllerData));
    if (g_Supervisor.controller == NULL)
    {
        memset(&joyinfoex, 0, sizeof(JOYINFOEX));
        joyinfoex.dwSize = sizeof(JOYINFOEX);
        joyinfoex.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(0, &joyinfoex) != JOYERR_NOERROR)
        {
            return g_ControllerData;
        }
        for (joyButtonBit = joyinfoex.dwButtons, joyButtonIndex = 0; joyButtonIndex < 32;
             joyButtonIndex += 1, joyButtonBit >>= 1)
        {
            if ((joyButtonBit & 1) != 0)
            {
                g_ControllerData[joyButtonIndex] = 0x80;
            }
        }
        return g_ControllerData;
    }
    else
    {
        dires = ((IDirectInputDevice8A *)g_Supervisor.controller)->Poll();
        if (FAILED(dires))
        {
            diRetryCount = 0;
            utils::DebugPrint2("error : DIERR_INPUTLOST\r\n");
            dires = ((IDirectInputDevice8A *)g_Supervisor.controller)->Acquire();
            // th07: unlike GetControllerInput, the retry-counter log is only
            // emitted after the loop exhausts its 400 retries.
            do
            {
                if (dires != DIERR_INPUTLOST)
                {
                    goto LAB_00430b42;
                }
                dires = ((IDirectInputDevice8A *)g_Supervisor.controller)->Acquire();
                diRetryCount++;
            } while (diRetryCount < 400);
            utils::DebugPrint2("error : DIERR_INPUTLOST %d\r\n", diRetryCount);
        }
        else
        {
            /* dires = */ ((IDirectInputDevice8A *)g_Supervisor.controller)->GetDeviceState(sizeof(DIJOYSTATE2),
                                                                                            &dijoystate2);
            // ZUN bug: the return value of GetDeviceState is discarded and
            // the stale Poll() result in `dires` is re-tested here. Since we
            // only reach this branch when Poll() succeeded, the test is always
            // true and the memcpy always runs.
            if (FAILED(dires))
            {
                return g_ControllerData;
            }
            // th07 copies 0x80 bytes starting at rgbButtons (overshooting the
            // 32-byte rgbButtons array into the following force-feedback
            // fields). Matched as a raw byte copy.
            memcpy(&g_ControllerData, &dijoystate2.rgbButtons, 0x80);
        }
    }
LAB_00430b42:
    return g_ControllerData;
}

u16 Controller::GetInput(void)
{
    u8 keyboardState[256];
    u16 buttons;

    buttons = 0;

    if (g_Supervisor.keyboard == NULL)
    {
        GetKeyboardState(keyboardState);

        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP, VK_UP);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN, VK_DOWN);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_LEFT, VK_LEFT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_RIGHT, VK_RIGHT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP, VK_NUMPAD8);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN, VK_NUMPAD2);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_LEFT, VK_NUMPAD4);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_RIGHT, VK_NUMPAD6);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP_LEFT, VK_NUMPAD7);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP_RIGHT, VK_NUMPAD9);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN_LEFT, VK_NUMPAD1);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN_RIGHT, VK_NUMPAD3);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_HOME, VK_HOME);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_D, 'D');
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_SHOOT, 'Z');
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_BOMB, 'X');
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_FOCUS, VK_SHIFT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_MENU, VK_ESCAPE);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_SKIP, VK_CONTROL);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_Q, 'Q');
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_S, 'S');
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_ENTER, VK_RETURN);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_R, 'R');
    }
    else
    {
        HRESULT res = ((IDirectInputDevice8A *)g_Supervisor.keyboard)->GetDeviceState(sizeof(keyboardState),
                                                                                       keyboardState);

        buttons = 0;

        if (res == DIERR_INPUTLOST)
        {
            ((IDirectInputDevice8A *)g_Supervisor.keyboard)->Acquire();

            return Controller::GetControllerInput(buttons);
        }

        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP, DIK_UP);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN, DIK_DOWN);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_LEFT, DIK_LEFT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_RIGHT, DIK_RIGHT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP, DIK_NUMPAD8);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN, DIK_NUMPAD2);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_LEFT, DIK_NUMPAD4);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_RIGHT, DIK_NUMPAD6);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP_LEFT, DIK_NUMPAD7);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_UP_RIGHT, DIK_NUMPAD9);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN_LEFT, DIK_NUMPAD1);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_DOWN_RIGHT, DIK_NUMPAD3);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_HOME, DIK_HOME);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_D, DIK_D);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_SHOOT, DIK_Z);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_BOMB, DIK_X);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_FOCUS, DIK_LSHIFT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_FOCUS, DIK_RSHIFT);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_MENU, DIK_ESCAPE);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_SKIP, DIK_LCONTROL);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_SKIP, DIK_RCONTROL);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_Q, DIK_Q);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_S, DIK_S);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_ENTER, DIK_RETURN);
        buttons |= KEYBOARD_KEY_PRESSED(TH_BUTTON_R, DIK_R);
    }

    return Controller::GetControllerInput(buttons);
}

void Controller::ResetKeyboard(void)
{
    u8 key_states[256];

    GetKeyboardState(key_states);
    for (i32 idx = 0; idx < 256; idx++)
    {
        *(key_states + idx) &= 0x7f;
    }
    SetKeyboardState(key_states);
}
}; // namespace th07
