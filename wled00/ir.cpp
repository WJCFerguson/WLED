// Support for Infra-red Remote Controls.
//
// Entry point is `handleIR()` at the bottom, called in the loop.
//
// On reception of an IR code it is checked in the `KeyMap` array corresponding
// to the configured remote, to look up the `ActionType` to be performed for the
// received IR code.
//
// The `Action` for that `ActionType` is looked up in the `actions` array, and
// executed.
//
// An `Action` object simply holds an argument-less function that will perform
// the action along with metadata like a name, and whether it can repeat.
//
// This indirection between ActionType and Action means that the list of
// possible Actions can be enumerated (e.g. for a GUI), and defining a new
// remote in code or dynamically becomes easy.

#ifdef WLED_DISABLE_INFRARED
void handleIR() {}
#else

#include "wled.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

byte presetButtonsConfigured();


// =============================================================================
// ========================== Action support functions =========================
// =============================================================================

// change byte `property` by `amount`, keeping the result within supplied
// boundaries
void relativeChange(byte& property,
                    const int8_t amount,
                    const byte lowerBoundary = 0,
                    const byte higherBoundary = 255) {
  int16_t new_val = (int16_t)property + amount;
  if (new_val > higherBoundary)
    new_val = higherBoundary;
  else if (new_val < lowerBoundary)
    new_val = lowerBoundary;
  property = (byte)new_val;
}

// ================================= Brightness ================================

// brightnessSteps: a static array of brightness levels following a geometric
// progression.  Can be generated from the following Python, adjusting the
// arbitrary 4.5 value to taste:
//
// def values(level):
//     while level >= 5:
//         yield int(level)
//         level -= level / 4.5
// result = [v for v in reversed(list(values(255)))]
// print("%d values: %s" % (len(result), result))
//
// It would be hard to maintain repeatable steps if calculating this on the fly.
const byte brightnessSteps[] = {
  5, 7, 9, 12, 16, 20, 26, 34, 43, 56, 72, 93, 119, 154, 198, 255
};
const size_t numBrightnessSteps = sizeof(brightnessSteps) / sizeof(uint8_t);

// increment destination (default `bri`) to the next `brightnessSteps` value
void incBrightness(byte* destination) {
  for (int index = 0; index < numBrightnessSteps; ++index) {
    if (brightnessSteps[index] > *destination) {
      *destination = brightnessSteps[index];
      break;
    }
  }
}

// decrement destination (default `bri`) to the next `brightnessSteps` value
void decBrightness(byte* destination) {
  for (int index = numBrightnessSteps - 1; index >= 0; --index) {
    if (brightnessSteps[index] < *destination) {
      *destination = brightnessSteps[index];
      break;
    }
  }
}

void incBri() { incBrightness(&bri); }
void decBri() { decBrightness(&bri); }
void incCol3() { incBrightness(&col[3]); }
void decCol3() { decBrightness(&col[3]); }

void setBri25() { bri = 63; }
void setBri50() { bri = 127; }
void setBri75() { bri = 191; }
void setBri100() { bri = 255; }

// =================================== Colors ==================================

// Set white colors with awareness of whether it's an RBGW strip.
void setWhite(uint32_t rgbColor, uint32_t rgbwColor) {
  if (strip.isRgbw) {
    colorFromUint32(rgbwColor);
    effectCurrent = 0;
  } else
    colorFromUint24(rgbColor);
}

void actionColorAqua() { colorFromUint24(COLOR_AQUA, false); }
void actionColorBlue() { colorFromUint24(COLOR_BLUE, false); }
void actionColorCyan() { colorFromUint24(COLOR_CYAN, false); }
void actionColorDeepblue() { colorFromUint24(COLOR_DEEPBLUE, false); }
void actionColorGreen() { colorFromUint24(COLOR_GREEN, false); }
void actionColorGreenish() { colorFromUint24(COLOR_GREENISH, false); }
void actionColorMagenta() { colorFromUint24(COLOR_MAGENTA, false); }
void actionColorOrange() { colorFromUint24(COLOR_ORANGE, false); }
void actionColorPink() { colorFromUint24(COLOR_PINK, false); }
void actionColorPurple() { colorFromUint24(COLOR_PURPLE, false); }
void actionColorRed() { colorFromUint24(COLOR_RED, false); }
void actionColorReddish() { colorFromUint24(COLOR_REDDISH, false); }
void actionColorTurquoise() { colorFromUint24(COLOR_TURQUOISE, false); }
void actionColorWhite() { colorFromUint24(COLOR_WHITE, false); }
void actionColorYellow() { colorFromUint24(COLOR_YELLOW, false); }
void actionColorYellowish() { colorFromUint24(COLOR_YELLOWISH, false); }

void actionColorColdwhite() { setWhite(COLOR_COLDWHITE, COLOR2_COLDWHITE); }
void actionColorColdwhite2() { setWhite(COLOR_COLDWHITE2, COLOR2_COLDWHITE2); }
void actionColorNeutralWhite() { setWhite(COLOR_NEUTRALWHITE, COLOR2_NEUTRALWHITE); }
void actionColorWarmWhite() { setWhite(COLOR_WARMWHITE, COLOR2_WARMWHITE); }
void actionColorWarmWhite2() { setWhite(COLOR_WARMWHITE2, COLOR2_WARMWHITE2); }

void actionColorRotate() {
  static const uint32_t colors[] = {
    COLOR_RED,
    COLOR_REDDISH,
    COLOR_ORANGE,
    COLOR_YELLOWISH,
    COLOR_GREEN,
    COLOR_GREENISH,
    COLOR_TURQUOISE,
    COLOR_CYAN,
    COLOR_BLUE,
    COLOR_DEEPBLUE,
    COLOR_PURPLE,
    COLOR_PINK,
    COLOR_WHITE,
  };
  static const uint8_t num_colors = sizeof(colors) / sizeof(uint32_t);
  static uint8_t color_index = 0;
  colorFromUint32(colors[color_index]);
  color_index = (color_index + 1) % num_colors;
}

// ============================ Speed and Intensity ============================

void changeEffectSpeed(int8_t amount) {
  if (effectCurrent != 0)
    relativeChange(effectSpeed, amount);
  else
    changeHue(amount);
}

void incEffectSpeed() { changeEffectSpeed(10); }
void decEffectSpeed() { changeEffectSpeed(-10); }

void changeEffectIntensity(int8_t amount) {
  if (effectCurrent != 0)
    relativeChange(effectIntensity, amount);
  else
    changeSaturation(amount);
}

void incEffectIntensity() { changeEffectIntensity(10); }
void decEffectIntensity() { changeEffectIntensity(-10); }

// ============================= Presets & Palettes ============================

// Action to apply preset `presetNum`.
//
// If no preset `presetNum` is defined then fall back to some default effects.
// If the action is repeated, increment `presetNum` by
// `presetButtonsConfigured()`.
void actionApplyPreset(byte presetNum) {
  // effects to use if no presets are defined:
  static const uint16_t fallbacks[] = {
    FX_MODE_STATIC,
    FX_MODE_TWINKLE,
    FX_MODE_BREATH,
    FX_MODE_COLORTWINKLE,
    FX_MODE_RAINBOW_CYCLE,
    FX_MODE_RAINBOW,
    FX_MODE_METEOR_SMOOTH,
    FX_MODE_FIRE_FLICKER,
    FX_MODE_PALETTE,
    FX_MODE_TWINKLEFOX,
  };
  static const size_t fallback_count = sizeof(fallbacks) / sizeof(uint16_t);
  static unsigned long presetSelectTime = 0;

  // on second press within 20s, apply next group of presets
  unsigned long sinceLast = millis() - presetSelectTime;
  if (presetNum == currentPreset && sinceLast > 500 && sinceLast < 20000)
    presetNum += presetButtonsConfigured();

  // apply preset, or if it doesn't exist, pick from fallbacks
  if (!applyPreset(presetNum)) {
    effectPalette = 0;          // use default palette
    effectCurrent = fallbacks[(presetNum - 1) % fallback_count];
  }
  presetSelectTime = millis();
}

void actionApplyPreset1() { actionApplyPreset(1); }
void actionApplyPreset2() { actionApplyPreset(2); }
void actionApplyPreset3() { actionApplyPreset(3); }
void actionApplyPreset4() { actionApplyPreset(4); }
void actionApplyPreset5() { actionApplyPreset(5); }
void actionApplyPreset6() { actionApplyPreset(6); }
void actionApplyPreset7() { actionApplyPreset(7); }
void actionApplyPreset8() { actionApplyPreset(8); }
void actionApplyPreset9() { actionApplyPreset(9); }
void actionApplyPreset10() { actionApplyPreset(10); }

void actionChangePreset(uint8_t offset) {
  effectCurrent = ((int16_t)effectCurrent + offset) % MODE_COUNT;
}

void actionIncPreset() { actionChangePreset(1); }
void actionDecPreset() { actionChangePreset(-1); }

void actionChangePalette(uint8_t offset) {
  effectPalette = ((int16_t)effectPalette + offset) % strip.getPaletteCount();
}

void actionIncPalette() { actionChangePalette(1); }
void actionDecPalette() { actionChangePalette(-1); }

// =============================================================================
// ============================= Action Definition =============================
// =============================================================================

// Details of an `Action` to be performed on a button press:
//
// - The function (without arguments) that performs the `Action`
// - a human readable `name` for it
// - whether it can be repeated
struct Action {
  Action(const char *name,
         void (*action_fn)(),
         bool is_repeatable = false)
    : name(name)
    , _action_fn(action_fn)
    , _is_repeatable(is_repeatable)
  {}

  // call operator performs the `action_fn`, with associated housekeeping
  void operator()() {
    if (_lastRepeatableAction == this) // a repeat
    {
      // if it's not a repeatable button and was pressed in last 0.5s, ignore.
      // Some remotes handle repeats by just re-firing the key, which can make,
      // for instance, power toggling codes come very rapidly.
      if (!_is_repeatable && (millis() - _last_action_time) < 500)
        return;
      Serial.println("Repeating Action");
      _repeat_count++;
    } else {
      _lastRepeatableAction = this;
      _repeat_count = 1;
    }

    DEBUG_PRINTF("Performing Action: %s\r\n", name);
    _last_action_time = millis();
    _action_fn();
    colorUpdated(NOTIFIER_CALL_MODE_BUTTON);
  }

  // repeat the last action, if there is one.
  static void repeatLast() {
    if (_lastRepeatableAction)
      (*_lastRepeatableAction)();
  }

  static void clearLastRepeatableAction() { _lastRepeatableAction = 0; }

  // members:
  const char *name;

private:
  void (*_action_fn)(); // fn that performs the action
  const bool _is_repeatable = true;

  static Action* _lastRepeatableAction;
  static uint8_t _repeat_count;
  static unsigned long _last_action_time;
};

Action* Action::_lastRepeatableAction = 0;
uint8_t Action::_repeat_count = 0;
unsigned long Action::_last_action_time = 0;


// An enumeration of all the available `Action` types.
//
// To add a new `Action`:
//
// - insert an identifier somewhere appropriate above `Action_Count`
// - insert an Action object to the *same location* in the `actions` array
// - add a pairing from IR code to that action in a Remote's definition mapping.
enum ActionType {
  Action_PowerOff,
  Action_PowerOn,
  Action_PowerToggle,

  Action_PowerOffWhite,
  Action_PowerOnWhite,
  Action_PowerToggleWhite,

  Action_Bright_Up,
  Action_Bright_Down,
  Action_Bright_25,
  Action_Bright_50,
  Action_Bright_75,
  Action_Bright_100,
  Action_White_Bright_Up,
  Action_White_Bright_Down,

  Action_Speed_Up,
  Action_Speed_Down,
  Action_Intensity_Up,
  Action_Intensity_Down,

  Action_Preset_1,
  Action_Preset_2,
  Action_Preset_3,
  Action_Preset_4,
  Action_Preset_5,
  Action_Preset_6,
  Action_Preset_7,
  Action_Preset_8,
  Action_Preset_9,
  Action_Preset_10,
  Action_Preset_Next,
  Action_Preset_Prev,
  Action_Palette_Next,
  Action_Palette_Prev,

  Action_Color_Aqua,
  Action_Color_Blue,
  Action_Color_ColdWhite,
  Action_Color_ColdWhite2,
  Action_Color_Cyan,
  Action_Color_Deepblue,
  Action_Color_Green,
  Action_Color_Greenish,
  Action_Color_Magenta,
  Action_Color_NeutralWhite,
  Action_Color_Orange,
  Action_Color_Pink,
  Action_Color_Purple,
  Action_Color_Red,
  Action_Color_Reddish,
  Action_Color_Turquoise,
  Action_Color_WarmWhite,
  Action_Color_WarmWhite2,
  Action_Color_White,
  Action_Color_Yellow,
  Action_Color_Yellowish,
  Action_Color_Rotate,

  Action_Count,                 // Action_Count must be the last item.
};

// Array of the `Action` objects indexed by `ActionType`.
//
// The names provided here are a building block to being able to configure
// remotes dynamically from the UI.
Action actions[Action_Count] = {
  {"Power Off", powerOff},
  {"Power On", powerOn},
  {"Power Toggle", toggleOnOff},

  {"Power Off White (for RGBW)", powerOffWhite},
  {"Power On White (for RGBW)", powerOnWhite},
  {"Power Toggle White (for RGBW)", toggleOnOffWhite},

  {"Brightness Up", incBri, true},
  {"Brightness Down", decBri, true},
  {"Brightness 25%", setBri25},
  {"Brightness 50%", setBri50},
  {"Brightness 75%", setBri75},
  {"Brightness 100%", setBri100},
  {"White Brightness Up (for RGBW)", incCol3, true},
  {"White Brightness Down (for RGBW)", decCol3, true},

  {"Speed Up", incEffectSpeed, true},
  {"Speed Down", decEffectSpeed, true},
  {"Intensity Up", incEffectIntensity, true},
  {"Intensity Down", decEffectIntensity, true},

  {"Preset 1", actionApplyPreset1},
  {"Preset 2", actionApplyPreset2},
  {"Preset 3", actionApplyPreset3},
  {"Preset 4", actionApplyPreset4},
  {"Preset 5", actionApplyPreset5},
  {"Preset 6", actionApplyPreset6},
  {"Preset 7", actionApplyPreset7},
  {"Preset 8", actionApplyPreset8},
  {"Preset 9", actionApplyPreset9},
  {"Preset 10", actionApplyPreset10},
  {"Next Preset", actionIncPreset},
  {"Prev Preset", actionDecPreset},
  {"Next Palette", actionIncPalette},
  {"Prev Palette", actionDecPalette},

  {"Aqua", actionColorAqua},
  {"Blue", actionColorBlue},
  {"ColdWhite", actionColorColdwhite},
  {"ColdWhite2", actionColorColdwhite2},
  {"Cyan", actionColorCyan},
  {"Deepblue", actionColorDeepblue},
  {"Green", actionColorGreen},
  {"Greenish", actionColorGreenish},
  {"Magenta", actionColorMagenta},
  {"NeutralWhite", actionColorNeutralWhite},
  {"Orange", actionColorOrange},
  {"Pink", actionColorPink},
  {"Purple", actionColorPurple},
  {"Red", actionColorRed},
  {"Reddish", actionColorReddish},
  {"Turquoise", actionColorTurquoise},
  {"WarmWhite", actionColorWarmWhite},
  {"WarmWhite2", actionColorWarmWhite2},
  {"White", actionColorWhite},
  {"Yellow", actionColorYellow},
  {"Yellowish", actionColorYellowish},
  {"Rotate Colors", actionColorRotate},
};

// =============================================================================
// =========================== IR Remote Definitions ===========================
// =============================================================================

// Object to map an IR code to an ActionType
struct KeyMap {
  uint32_t irCode;
  ActionType actionType;
};

// NOTE: Each KeyMap array that defines a Remote's actions, must end in a null
// entry.

const KeyMap IR24_Actions[] = {
  {IR24_BRIGHTER, Action_Bright_Up},
  {IR24_DARKER, Action_Bright_Down},
  {IR24_OFF, Action_PowerOff},
  {IR24_ON, Action_PowerOn},
  {IR24_RED, Action_Color_Red},
  {IR24_REDDISH, Action_Color_Reddish},
  {IR24_ORANGE, Action_Color_Orange},
  {IR24_YELLOWISH, Action_Color_Yellowish},
  {IR24_YELLOW, Action_Color_Yellow},
  {IR24_GREEN, Action_Color_Green},
  {IR24_GREENISH, Action_Color_Greenish},
  {IR24_TURQUOISE, Action_Color_Turquoise},
  {IR24_CYAN, Action_Color_Cyan},
  {IR24_AQUA, Action_Color_Aqua},
  {IR24_BLUE, Action_Color_Blue},
  {IR24_DEEPBLUE, Action_Color_Deepblue},
  {IR24_PURPLE, Action_Color_Purple},
  {IR24_MAGENTA, Action_Color_Magenta},
  {IR24_PINK, Action_Color_Pink},
  {IR24_WHITE, Action_Color_White},
  {IR24_FLASH, Action_Preset_1},
  {IR24_STROBE, Action_Preset_2},
  {IR24_FADE, Action_Preset_3},
  {IR24_SMOOTH, Action_Preset_4},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR24_Old_Actions[] = {
  {IR24_OLD_BRIGHTER, Action_Bright_Up},
  {IR24_OLD_DARKER, Action_Bright_Down},
  {IR24_OLD_OFF, Action_PowerOff},
  {IR24_OLD_ON, Action_PowerOn},

  {IR24_OLD_RED, Action_Color_Red},
  {IR24_OLD_REDDISH, Action_Color_Reddish},
  {IR24_OLD_ORANGE, Action_Color_Orange},
  {IR24_OLD_YELLOWISH, Action_Color_Yellowish},
  {IR24_OLD_YELLOW, Action_Color_Yellow},
  {IR24_OLD_GREEN, Action_Color_Green},
  {IR24_OLD_GREENISH, Action_Color_Greenish},
  {IR24_OLD_TURQUOISE, Action_Color_Turquoise},
  {IR24_OLD_CYAN, Action_Color_Cyan},
  {IR24_OLD_AQUA, Action_Color_Aqua},
  {IR24_OLD_BLUE, Action_Color_Blue},
  {IR24_OLD_DEEPBLUE, Action_Color_Deepblue},
  {IR24_OLD_PURPLE, Action_Color_Purple},
  {IR24_OLD_MAGENTA, Action_Color_Magenta},
  {IR24_OLD_PINK, Action_Color_Pink},
  {IR24_OLD_WHITE, Action_Color_White},

  {IR24_OLD_FLASH, Action_Preset_1},
  {IR24_OLD_STROBE, Action_Preset_2},
  {IR24_OLD_FADE, Action_Preset_3},
  {IR24_OLD_SMOOTH, Action_Preset_4},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR24CT_Actions[] = {
  {IR24_CT_BRIGHTER, Action_Bright_Up},
  {IR24_CT_DARKER, Action_Bright_Down},
  {IR24_CT_OFF, Action_PowerOff},
  {IR24_CT_ON, Action_PowerOn},

  {IR24_CT_RED, Action_Color_Red},
  {IR24_CT_REDDISH, Action_Color_Reddish},
  {IR24_CT_ORANGE, Action_Color_Orange},
  {IR24_CT_YELLOWISH, Action_Color_Yellowish},
  {IR24_CT_YELLOW, Action_Color_Yellow},
  {IR24_CT_GREEN, Action_Color_Green},
  {IR24_CT_GREENISH, Action_Color_Greenish},
  {IR24_CT_TURQUOISE, Action_Color_Turquoise},
  {IR24_CT_CYAN, Action_Color_Cyan},
  {IR24_CT_AQUA, Action_Color_Aqua},
  {IR24_CT_BLUE, Action_Color_Blue},
  {IR24_CT_DEEPBLUE, Action_Color_Deepblue},
  {IR24_CT_PURPLE, Action_Color_Purple},
  {IR24_CT_MAGENTA, Action_Color_Magenta},
  {IR24_CT_PINK, Action_Color_Pink},
  {IR24_CT_COLDWHITE, Action_Color_ColdWhite},
  {IR24_CT_WARMWHITE, Action_Color_WarmWhite},
  {IR24_CT_CTPLUS, Action_Color_ColdWhite2},
  {IR24_CT_CTMINUS, Action_Color_WarmWhite2},

  {IR24_CT_MEMORY, Action_Color_NeutralWhite},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR40_Actions[] = {
  {IR40_BPLUS, Action_Bright_Up},
  {IR40_BMINUS, Action_Bright_Down},
  {IR40_OFF, Action_PowerOff},
  {IR40_ON, Action_PowerOn},

  {IR40_RED, Action_Color_Red},
  {IR40_REDDISH, Action_Color_Reddish},
  {IR40_ORANGE, Action_Color_Orange},
  {IR40_YELLOWISH, Action_Color_Yellowish},
  {IR40_YELLOW, Action_Color_Yellow},
  {IR40_GREEN, Action_Color_Green},
  {IR40_GREENISH, Action_Color_Greenish},
  {IR40_TURQUOISE, Action_Color_Turquoise},
  {IR40_CYAN, Action_Color_Cyan},
  {IR40_AQUA, Action_Color_Aqua},
  {IR40_BLUE, Action_Color_Blue},
  {IR40_DEEPBLUE, Action_Color_Deepblue},
  {IR40_PURPLE, Action_Color_Purple},
  {IR40_MAGENTA, Action_Color_Magenta},
  {IR40_PINK, Action_Color_Pink},
  {IR40_WARMWHITE2, Action_Color_WarmWhite2},
  {IR40_WARMWHITE, Action_Color_WarmWhite},
  {IR40_WHITE, Action_Color_White},
  {IR40_COLDWHITE, Action_Color_ColdWhite},
  {IR40_COLDWHITE2, Action_Color_ColdWhite2},

  {IR40_WOFF, Action_PowerOffWhite},
  {IR40_WON, Action_PowerOnWhite},
  {IR40_WPLUS, Action_White_Bright_Up},
  {IR40_WMINUS, Action_White_Bright_Down},

  {IR40_W25, Action_Bright_25},
  {IR40_W50, Action_Bright_50},
  {IR40_W75, Action_Bright_75},
  {IR40_W100, Action_Bright_100},

  {IR40_QUICK, Action_Speed_Up},
  {IR40_SLOW, Action_Speed_Down},
  {IR40_JUMP7, Action_Intensity_Up},
  {IR40_AUTO, Action_Intensity_Down},

  {IR40_JUMP3, Action_Preset_1},
  {IR40_FADE3, Action_Preset_2},
  {IR40_FADE7, Action_Preset_3},
  {IR40_FLASH, Action_Preset_4},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR44_Actions[] = {
  {IR44_BPLUS, Action_Bright_Up},
  {IR44_BMINUS, Action_Bright_Down},
  {IR44_OFF, Action_PowerOn},
  {IR44_ON, Action_PowerOff},

  {IR44_RED, Action_Color_Red},
  {IR44_REDDISH, Action_Color_Reddish},
  {IR44_ORANGE, Action_Color_Orange},
  {IR44_YELLOWISH, Action_Color_Yellowish},
  {IR44_YELLOW, Action_Color_Yellow},
  {IR44_GREEN, Action_Color_Green},
  {IR44_GREENISH, Action_Color_Greenish},
  {IR44_TURQUOISE, Action_Color_Turquoise},
  {IR44_CYAN, Action_Color_Cyan},
  {IR44_AQUA, Action_Color_Aqua},
  {IR44_BLUE, Action_Color_Blue},
  {IR44_DEEPBLUE, Action_Color_Deepblue},
  {IR44_PURPLE, Action_Color_Purple},
  {IR44_MAGENTA, Action_Color_Magenta},
  {IR44_PINK, Action_Color_Pink},
  {IR44_WHITE, Action_Color_White},
  {IR44_WARMWHITE2, Action_Color_WarmWhite2},
  {IR44_WARMWHITE, Action_Color_WarmWhite},
  {IR44_COLDWHITE, Action_Color_ColdWhite},
  {IR44_COLDWHITE2, Action_Color_ColdWhite2},

  {IR44_REDPLUS, Action_Preset_Next},
  {IR44_REDMINUS, Action_Preset_Prev},
  {IR44_GREENPLUS, Action_Palette_Next},
  {IR44_GREENMINUS, Action_Palette_Prev},
  {IR44_BLUEPLUS, Action_Intensity_Up},
  {IR44_BLUEMINUS, Action_Intensity_Down},
  {IR44_QUICK, Action_Speed_Up},
  {IR44_SLOW, Action_Speed_Down},
  {IR44_DIY1, Action_Preset_1},
  {IR44_DIY2, Action_Preset_2},
  {IR44_DIY3, Action_Preset_3},
  {IR44_DIY4, Action_Preset_4},
  {IR44_DIY5, Action_Preset_5},
  {IR44_DIY6, Action_Preset_6},
  {IR44_AUTO, Action_Preset_7},
  {IR44_FLASH, Action_Preset_8},
  {IR44_JUMP3, Action_Bright_25},
  {IR44_JUMP7, Action_Bright_50},
  {IR44_FADE3, Action_Bright_75},
  {IR44_FADE7, Action_Bright_100},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR21_Actions[] = {
  {IR21_BRIGHTER, Action_Bright_Up},
  {IR21_DARKER, Action_Bright_Down},
  {IR21_OFF, Action_PowerOff},
  {IR21_ON, Action_PowerOn},
  {IR21_RED, Action_Color_Red},
  {IR21_REDDISH, Action_Color_Reddish},
  {IR21_ORANGE, Action_Color_Orange},
  {IR21_YELLOWISH, Action_Color_Yellowish},
  {IR21_GREEN, Action_Color_Green},
  {IR21_GREENISH, Action_Color_Greenish},
  {IR21_TURQUOISE, Action_Color_Turquoise},
  {IR21_CYAN, Action_Color_Cyan},
  {IR21_BLUE, Action_Color_Blue},
  {IR21_DEEPBLUE, Action_Color_Deepblue},
  {IR21_PURPLE, Action_Color_Purple},
  {IR21_PINK, Action_Color_Pink},
  {IR21_WHITE, Action_Color_White},
  {IR21_FLASH, Action_Preset_1},
  {IR21_STROBE, Action_Preset_2},
  {IR21_FADE, Action_Preset_3},
  {IR21_SMOOTH, Action_Preset_4},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR6_Actions[] = {
  {IR6_POWER, Action_PowerToggle},
  {IR6_CHANNEL_UP, Action_Bright_Up},
  {IR6_CHANNEL_DOWN, Action_Bright_Down},
  {IR6_VOLUME_UP, Action_Preset_Next},
  {IR6_VOLUME_DOWN, Action_Color_Rotate},
  {IR6_MUTE, Action_Color_White},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap IR9_Actions[] = {
  {IR9_POWER, Action_PowerToggle},
  {IR9_A, Action_Preset_1},
  {IR9_B, Action_Preset_2},
  {IR9_C, Action_Preset_3},
  {IR9_UP, Action_Preset_Next},
  {IR9_DOWN, Action_Bright_Down},
  {IR9_LEFT, Action_Speed_Up},
  {IR9_RIGHT, Action_Speed_Down},
  {IR9_SELECT, Action_Preset_Next},
  {},                           // IMPORTANT: must end with null entry
};

// To define a custom remote, determine IR codes and #define them in ir_codes.h,
// then make a mapping here in the same style of the above mappings, from IR
// code to `ActionType`.
const KeyMap Custom_Actions[] = {
    {},
};


const KeyMap Squeezebox_Actions[] = {
  {IR_SQUEEZEBOX_NOW_PLAYING, Action_Bright_Down},
  {IR_SQUEEZEBOX_SIZE, Action_PowerToggle},
  {IR_SQUEEZEBOX_BRIGHTNESS, Action_Bright_Up},

  {IR_SQUEEZEBOX_1, Action_Preset_1},
  {IR_SQUEEZEBOX_2, Action_Preset_2},
  {IR_SQUEEZEBOX_3, Action_Preset_3},
  {IR_SQUEEZEBOX_4, Action_Preset_4},
  {IR_SQUEEZEBOX_5, Action_Preset_5},
  {IR_SQUEEZEBOX_6, Action_Preset_6},
  {IR_SQUEEZEBOX_7, Action_Preset_7},
  {IR_SQUEEZEBOX_8, Action_Preset_8},
  {IR_SQUEEZEBOX_9, Action_Preset_9},
  {IR_SQUEEZEBOX_0, Action_Preset_10},

  {IR_SQUEEZEBOX_ARROW_DOWN, Action_Speed_Down},
  {IR_SQUEEZEBOX_ARROW_UP, Action_Speed_Up},
  {IR_SQUEEZEBOX_ARROW_LEFT, Action_Intensity_Down},
  {IR_SQUEEZEBOX_ARROW_RIGHT, Action_Intensity_Up},

  {IR_SQUEEZEBOX_BROWSE, Action_Bright_25},
  {IR_SQUEEZEBOX_SHUFFLE, Action_Bright_50},
  {IR_SQUEEZEBOX_REPEAT, Action_Bright_75},
  {},                           // IMPORTANT: must end with null entry
};

const KeyMap Roku_Express_Actions[] = {
  {IR_ROKU_BACK, Action_PowerOff},
  {IR_ROKU_HOME, Action_PowerOn},
  {IR_ROKU_UP, Action_Bright_Up},
  {IR_ROKU_DOWN, Action_Bright_Down},
  {IR_ROKU_RIGHT, Action_Speed_Up},
  {IR_ROKU_LEFT, Action_Speed_Down},
  {IR_ROKU_REDO, Action_Intensity_Down},
  {IR_ROKU_STAR, Action_Intensity_Up},
  {IR_ROKU_REWIND, Action_Preset_1},
  {IR_ROKU_PLAY, Action_Preset_2},
  {IR_ROKU_FFD, Action_Preset_3},
  {IR_ROKU_NETFLIX, Action_Preset_4},
  {IR_ROKU_ESPN, Action_Preset_5},
  {IR_ROKU_HULU, Action_Preset_6},
  {IR_ROKU_SLING, Action_Preset_7},
  {},                           // IMPORTANT: must end with null entry
};

// =============================================================================
// ============================== IR code Handling =============================
// =============================================================================

// Enumeration of the remote types, as selected in the UI in settings_sync.htm
// and stored in the `irEnabled` variable.
enum RemoteType {
  Remote_IR_Disabled,
  Remote_IR24_Old,
  Remote_IR24_CT,
  Remote_IR40,
  Remote_IR44,
  Remote_IR21,
  Remote_IR6,
  Remote_IR9,
  Remote_IR24,
  Remote_Custom,                // special case user-defined
  Remote_Squeezebox,
  Remote_Roku_Express,

  // All remotes configurable from the UI must be above this item:
  ConfigurableRemoteCount,
};

// Array to the IR_code->ActionType mappings indexed by `irEnabled` /
// `RemoteType`
const KeyMap* buttonActions[ConfigurableRemoteCount] = {
  0,                            // disabled - no mapping...
  IR24_Old_Actions,
  IR24CT_Actions,
  IR40_Actions,
  IR44_Actions,
  IR21_Actions,
  IR6_Actions,
  IR9_Actions,
  IR24_Actions,
  Custom_Actions,
  Squeezebox_Actions,
  Roku_Express_Actions,
};


// return the number of preset buttons configured for the current remote.
byte presetButtonsConfigured() {
  // cache the result for the remote.
  static byte results[ConfigurableRemoteCount] = {};

  // if it's zero, assume it's uninitialized
  if (!results[irEnabled]) {
    // count the preset actions in the KeyMap array for the selected remote
    const KeyMap* remoteMapping = buttonActions[irEnabled];
    for (const KeyMap* keyMap = remoteMapping; keyMap->irCode; ++keyMap)
      if (keyMap->actionType >= Action_Preset_10 &&
          keyMap->actionType <= Action_Preset_10)
        results[irEnabled]++;
  }
  return results[irEnabled];
}


// execute the action that `irCode` maps to in the enabled remote mapping
void handleIRCode(uint32_t irCode) {
  if (irCode == 0xFFFFFFFF) {
    // It's the repeat code; call last action and exit
    Action::repeatLast();
    return;
  }

  // look for `irCode` in the KeyMap array for the selected remote.  Performs an
  // iterative search for the IR Code, which is OK because the array is small.
  // Implementing this with std::map grew the build by 3k.
  const KeyMap* remoteMapping = buttonActions[irEnabled];
  for (const KeyMap* keyMap = remoteMapping; keyMap->irCode; ++keyMap) {
    if (keyMap->irCode == irCode) {
      // found the code!  Perform the action and quit
      actions[keyMap->actionType]();
      return;
    }
  }

  // no action was found for the code.  so we should disable repeats of our last
  // action and exit
  Action::clearLastRepeatableAction();
}


// Main handler for IR code reception - manages the IRrecv object and uses
// incoming IR codes to dispatch `Action`s with reference to the `RemoteType`
// configured in the `irEnabled` variable.
void handleIR() {
  static IRrecv *irrecv = 0;
  static unsigned long irCheckedTime = 0;
  static decode_results results;

  // if IR not enabled; ensure irrecv is null and exit
  if (irEnabled == 0 || irEnabled >= ConfigurableRemoteCount) {
    if (irrecv) {
      irrecv->disableIRIn();
      delete irrecv;
      irrecv = 0;
    }
    return;
  }

  // IR is enabled;  If `irrecv` not initialized; do so and exit
  if (!irrecv) {
    irrecv = new IRrecv(irPin);
    irrecv->enableIRIn();
    return;
  }

  // Only check for a code every 120ms
  if (millis() - irCheckedTime < 120)
    return;
  irCheckedTime = millis();

  // check for IR code
  if (!irrecv->decode(&results)) // received nothing -> exit
    return;
  uint32_t irCode = static_cast<uint32_t>(results.value);
  irrecv->resume();
  if (!irCode)                  // received null -> exit
    return;

  // handle IR code
  Serial.printf("IR recv: 0x%08x\r\n", irCode);
  handleIRCode(irCode);
}

#endif
