// Android's input on port 0 beside the controllers: the touch controls, and a keyboard and mouse when one is
// plugged in. It is src/common/mouse_keyboard.h, the game's own keyboard and mouse flow as
// src/common/mouse_keyboard.cpp has it, over the touch screen: the touch buttons press the keys their actions
// are bound to, the stick is the left stick and a drag on the right turns the view as the mouse does. In a menu
// a touch is the mouse cursor: a tap clicks, a drag on a slider slides it. SDL's keys arrive as the virtual
// keys of common/keys.h.
#include "common/mouse_keyboard.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/aspect.h"
#include "common/keys.h"
#include "common/menu.h"
#include "common/options.h"
#include "common/translations.h"
#include "core/log.h"
#include "game/hud.h"
#include "host.h"

namespace input {

namespace {

// what an action presses on the pad, as the game's own keyboard and mouse flow has it
enum AnalogButton {
    kA,
    kB,
    kX,
    kY,
    kBlack,
    kWhite,
    kLeftTrigger,
    kRightTrigger,
};
enum PadButton {
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kStart,
    kBack,
    kLeftThumb,
    kRightThumb,
};
enum Move {
    kForward,
    kBackward,
    kLeft,
    kRight,
    kMoveCount,
};
enum Look {
    kLookUp,
    kLookDown,
    kLookLeft,
    kLookRight,
    kLookCount,
};
// Game: presses nothing on the pad, the game's action queries read it (game/actions.cpp)
enum class Kind { Analog, Button, Move, Look, Host, Walk, Game };

// a Game action's index is the pad button its prompts show, in ButtonKey's numbering, -1 for none
struct Entry {
    Action action;
    Kind kind;
    int index;
    Section section = Section::Foot;
};

constexpr int kFirstPadButton = 8;  // ButtonKey's numbering: the analog buttons, then the pad buttons

// the game's controller layout, then the keys of the vehicles, which the game reads as actions
constexpr Entry kEntries[] = {
    {{"move_forward", "MOVE FORWARD", "W"}, Kind::Move, kForward},
    {{"move_back", "MOVE BACK", "S"}, Kind::Move, kBackward},
    {{"strafe_left", "STRAFE LEFT", "A"}, Kind::Move, kLeft},
    {{"strafe_right", "STRAFE RIGHT", "D"}, Kind::Move, kRight},
    {{"walk", "WALK", "Alt"}, Kind::Walk, 0},
    {{"interact", "INTERACT / CLIMB", "F"}, Kind::Analog, kA},
    {{"reload", "RELOAD", "R"}, Kind::Game, -1},
    {{"switch_ammo", "SWITCH AMMO", "T"}, Kind::Game, -1},
    {{"dive_roll", "DIVE ROLL", "Shift"}, Kind::Analog, kB},
    {{"focus", "FOCUS", "E"}, Kind::Analog, kX},
    {{"wall_cover", "WALL COVER", "Ctrl"}, Kind::Analog, kY},
    {{"crouch", "CROUCH", "C"}, Kind::Analog, kBlack},
    {{"inventory", "INVENTORY", "Q"}, Kind::Game, kWhite},
    {{"lock_on", "LOCK ON", "RMB"}, Kind::Analog, kLeftTrigger},
    {{"target_left", "TARGET LEFT", "Mouse4"}, Kind::Game, -1},
    {{"target_right", "TARGET RIGHT", "Mouse5"}, Kind::Game, -1},
    {{"fire", "FIRE", "LMB"}, Kind::Analog, kRightTrigger},
    {{"zoom_in", "SCOPE ZOOM IN", "Z,Wheel Up"}, Kind::Button, kDpadUp},
    {{"zoom_out", "SCOPE ZOOM OUT", "X,Wheel Down"}, Kind::Button, kDpadDown},
    {{"reset_camera", "RESET CAMERA", "V"}, Kind::Button, kRightThumb},
    {{"objectives", "OBJECTIVES", "Tab"}, Kind::Button, kBack},
    {{"pause", "PAUSE MENU", "Escape"}, Kind::Button, kStart},
    {{"hide_hud", "TOGGLE HUD", ""}, Kind::Host, 0},
    {{"toggle_textures", "TOGGLE TEXTURE REPLACEMENT", ""}, Kind::Host, 0},
    {{"settings_panel", "SETTINGS PANEL", "F10"}, Kind::Host, 0},
    {{"look_up", "LOOK UP", "Up"}, Kind::Look, kLookUp},
    {{"look_down", "LOOK DOWN", "Down"}, Kind::Look, kLookDown},
    {{"look_left", "LOOK LEFT", "Left"}, Kind::Look, kLookLeft},
    {{"look_right", "LOOK RIGHT", "Right"}, Kind::Look, kLookRight},
    {{"inventory_up", "INVENTORY UP", "Wheel Up,W"}, Kind::Game, -1, Section::Inventory},
    {{"inventory_down", "INVENTORY DOWN", "Wheel Down,S"}, Kind::Game, -1, Section::Inventory},
    {{"inventory_left", "INVENTORY LEFT", "A"}, Kind::Game, -1, Section::Inventory},
    {{"inventory_right", "INVENTORY RIGHT", "D"}, Kind::Game, -1, Section::Inventory},
    {{"inventory_select", "SELECT ITEM", "LMB,Q"}, Kind::Game, kA, Section::Inventory},
    {{"inventory_cancel", "CANCEL", "RMB"}, Kind::Game, kB, Section::Inventory},
    // the console's A both accelerates and answers the boxes that pause the game waiting for it, so the pedal presses
    // the pad's A itself: a keyboard only action would leave a driving tutorial box with no button to close it
    {{"car_accelerate", "ACCELERATE", "W"}, Kind::Analog, kA, Section::Car},
    {{"car_brake", "BRAKE / REVERSE", "S"}, Kind::Game, kX, Section::Car},
    {{"car_steer_left", "STEER LEFT", "A"}, Kind::Move, kLeft, Section::Car},
    {{"car_steer_right", "STEER RIGHT", "D"}, Kind::Move, kRight, Section::Car},
    {{"car_handbrake", "HANDBRAKE", "Space"}, Kind::Game, kB, Section::Car},
    {{"car_fire", "FIRE WEAPON", "LMB"}, Kind::Game, kRightTrigger, Section::Car},
    {{"car_target_lock", "TARGET LOCK", "RMB"}, Kind::Game, kLeftTrigger, Section::Car},
    {{"car_cycle_weapon", "CYCLE WEAPON", "Q,Wheel Up"}, Kind::Game, kWhite, Section::Car},
    {{"car_tire_slasher", "TIRE SLASHER", "E"}, Kind::Game, -1, Section::Car},
    {{"car_exit", "EXIT VEHICLE", "F"}, Kind::Game, kY, Section::Car},
    {{"fly_forward", "FLY FORWARD", "W"}, Kind::Move, kForward, Section::Jetpack},
    {{"fly_back", "FLY BACK", "S"}, Kind::Move, kBackward, Section::Jetpack},
    {{"fly_left", "FLY LEFT", "A"}, Kind::Move, kLeft, Section::Jetpack},
    {{"fly_right", "FLY RIGHT", "D"}, Kind::Move, kRight, Section::Jetpack},
    {{"fly_up", "FLY UP", "Space"}, Kind::Game, kA, Section::Jetpack},
    {{"fly_down", "FLY DOWN", "Ctrl"}, Kind::Game, kX, Section::Jetpack},
    {{"fly_exit", "LAND / EXIT", "F"}, Kind::Game, kY, Section::Jetpack},
};
constexpr size_t kCount = std::size(kEntries);
static_assert(kCount <= 64, "HeldActions keeps a bit per action");

constexpr size_t ActionIndex(std::string_view id) {
    size_t i = 0;
    while (kEntries[i].action.id != id) i++;
    return i;
}
constexpr size_t kHudAction = ActionIndex("hide_hud");
constexpr size_t kTexturesAction = ActionIndex("toggle_textures");
constexpr size_t kPanelAction = ActionIndex("settings_panel");

constexpr size_t PauseActionIndex() {
    size_t i = 0;
    while (kEntries[i].kind != Kind::Button || kEntries[i].index != kStart) i++;
    return i;
}
constexpr size_t kPauseAction = PauseActionIndex();

constexpr uint8_t kFullPress = 0xFF;
constexpr int16_t kFullStick = 32767;
constexpr float kWalkStick = 0.4f;  // share of a full stick the walk key moves at
constexpr uint64_t kCaptureGuardMs = 250;  // the panel key waits this long after a key capture poll
constexpr size_t kMaxKeysPerAction = 2;
constexpr uint64_t kPlayMs = 250;  // play is the look hook asking for the mouse this recently
constexpr uint64_t kTapMs = 80;    // a wheel notch holds what it presses this long
constexpr int kPadStickPress = 8000;  // a controller stick past this counts as a press
constexpr uint8_t kPadAnalogPress = 30;  // a resting trigger may read a little above 0
constexpr int kPadDigitalButtons = 8;   // Pad::buttons bits in use
constexpr int kPorts = 4;
// a wheel notch in a menu steps along its list: up and down, or left and right in a row
constexpr uint16_t kWheelUp = 1 << kDpadUp;
constexpr uint16_t kWheelDown = 1 << kDpadDown;
constexpr uint16_t kWheelBack = 1 << kDpadLeft;
constexpr uint16_t kWheelNext = 1 << kDpadRight;
// the wheel binds as two keys, codes no key has
constexpr int kWheelUpKey = 0x0E;
constexpr int kWheelDownKey = 0x0F;
constexpr int kFirstVk = 1;
constexpr int kEndVk = 255;
constexpr uint32_t kAnalogShift = 16;    // a tap's analog buttons sit above its pad buttons

struct Name {
    int vk;
    const char* name;
};
// letters, digits and F keys are named by their own character; F3 stays with the host
constexpr Name kNames[] = {
    {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"}, {VK_MBUTTON, "MMB"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {kWheelUpKey, "Wheel Up"}, {kWheelDownKey, "Wheel Down"},
    {VK_SPACE, "Space"}, {VK_ESCAPE, "Escape"}, {VK_TAB, "Tab"}, {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"},
    {VK_MENU, "Alt"}, {VK_CAPITAL, "CapsLock"}, {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"},
    {VK_END, "End"}, {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"}, {VK_PRIOR, "PageUp"},
    {VK_NEXT, "PageDown"},
    {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"}, {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"},
    {VK_NUMPAD4, "Numpad4"}, {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
    {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"}, {VK_ADD, "NumpadPlus"}, {VK_SUBTRACT, "NumpadMinus"},
    {VK_MULTIPLY, "NumpadStar"}, {VK_DIVIDE, "NumpadSlash"}, {VK_DECIMAL, "NumpadPeriod"},
    {VK_OEM_3, "Backtick"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"},
    {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"},
};

constexpr int kFirstFKey = 1;
constexpr int kLastFKey = 12;
constexpr int kHostFKey = 3;

using Binds = std::array<std::vector<int>, kCount>;

struct StickKeys {
    std::array<bool, kMoveCount> move{};
    std::array<bool, kLookCount> look{};
    bool walk = false;  // the move keys push the stick by the walk speed
};

std::mutex g_bind_lock;
Binds g_binds;
std::atomic<bool> g_settle{false};
std::atomic<int> g_dx{0};
std::atomic<int> g_dy{0};
std::atomic<uint64_t> g_take_ms{0};  // TakeMouse, asked every tick in play
std::array<std::atomic<uint32_t>, kPorts> g_pad_held{};  // by port, each polled on its own
std::atomic<uint64_t> g_wheel_ms[2] = {};  // the last notch up, down
std::atomic<uint64_t> g_capture_ms{0};
std::atomic<bool> g_menu{false};
std::atomic<bool> g_keyboard{true};
std::atomic<bool> g_touch{true};  // the touch controls used last, not a controller or the keys
std::atomic<int> g_wheel{0};  // notches in the menus not yet pressed
std::atomic<uint32_t> g_press{0};
std::atomic<uint64_t> g_press_until{0};
std::atomic<Section> g_section{Section::Foot};
std::atomic<bool> g_wheel_across{false};
std::atomic<bool> g_walk_diagonal{false};

// a wheel notch in the menus, pressed on the dpad with a release before the next; pad thread only
struct WheelStep {
    uint16_t button = 0;
    uint64_t until = 0;
};
WheelStep g_step;

// the menu page's accept and cancel as tap bits, zero outside a page
std::atomic<uint32_t> g_page_accept{0};
std::atomic<uint32_t> g_page_cancel{0};

// pad thread: the page keys in use, and Enter, Space or a pause key held since they changed
struct PageState {
    uint32_t accept = 0;
    uint32_t cancel = 0;
    bool held = false;
};
PageState g_page;

// the keys and mouse buttons held, by virtual key
std::array<std::atomic<bool>, 256> g_down{};
// the keys the touch buttons hold down and the touch stick: they join the pad in Keyboard below, past the game's own
// hook reading it — nothing touch goes on the pad before that, or a walk would read as a controller's input and take
// the look from the touch screen
std::array<std::atomic<bool>, 256> g_button_keys{};
std::atomic<float> g_stick_x{0}, g_stick_y{0};  // the stick, -1 to 1, y up

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

bool IsLetterOrDigit(int vk) { return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'); }

bool IsBindableFKey(int vk) {
    int number = vk - VK_F1 + 1;
    return number >= kFirstFKey && number <= kLastFKey && number != kHostFKey;
}

// empty for a key that cannot be bound
std::string NameOf(int vk) {
    if (IsLetterOrDigit(vk)) return std::string(1, char(vk));
    if (IsBindableFKey(vk)) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const Name& n : kNames) {
        if (n.vk == vk) return n.name;
    }
    return {};
}

int Vk(const std::string& name) {
    if (name.size() == 1 && std::isalnum(uint8_t(name[0]))) return std::toupper(uint8_t(name[0]));
    for (int vk = VK_F1; vk <= VK_F12; vk++) {
        if (IsBindableFKey(vk) && strcasecmp(NameOf(vk).c_str(), name.c_str()) == 0) return vk;
    }
    for (const Name& n : kNames) {
        if (strcasecmp(n.name, name.c_str()) == 0) return n.vk;
    }
    return 0;
}

std::vector<int> Parse(const std::string& list) {
    std::vector<int> keys;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (int vk = Vk(list.substr(start, comma - start))) keys.push_back(vk);
        start = comma + 1;
    }
    return keys;
}

// the saved names, as the game's own flow writes them; the player's layout is the US one here
std::string Join(const std::vector<int>& keys) {
    std::string text;
    for (int vk : keys) {
        if (!text.empty()) text += ',';
        text += NameOf(vk);
    }
    return text;
}

bool IsWheel(int vk) { return vk == kWheelUpKey || vk == kWheelDownKey; }

// a wheel key is held for one tap after its notch; the keys the touch buttons hold count as held too
bool Down(int vk) {
    if (IsWheel(vk)) return NowMs() - g_wheel_ms[vk == kWheelDownKey] < kTapMs;
    return vk > 0 && vk < 256 && (g_down[size_t(vk)] || g_button_keys[size_t(vk)]);
}

bool IsMouseButton(int vk) {
    bool button = vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    return button || IsWheel(vk);
}

bool Play() { return NowMs() - g_take_ms < kPlayMs; }

// the app is in front and the panel is closed
bool Active() { return host::Foreground() && !app::MenuOpen(); }

Binds CopyBinds() {
    std::lock_guard lock(g_bind_lock);
    return g_binds;
}

// the touch controls press the keys their actions are bound to, and the car's stick its gas and brake
void HoldActionKeys(size_t action, bool down) {
    if (action >= kCount) return;
    std::lock_guard lock(g_bind_lock);
    for (int vk : g_binds[action]) {
        if (vk > 0 && vk < 256) g_button_keys[size_t(vk)] = down;
    }
}

bool AnyDown(const std::vector<int>& keys) { return std::any_of(keys.begin(), keys.end(), Down); }

// keys held as the inventory closes wait for release: the click that picked an item does not then fire it
std::mutex g_held_back_lock;
std::vector<int> g_held_back;

void HoldBackInventoryKeys() {
    std::vector<int> held;
    {
        std::lock_guard lock(g_bind_lock);
        for (size_t i = 0; i < kCount; i++) {
            if (kEntries[i].section != Section::Inventory) continue;
            for (int vk : g_binds[i]) {
                if (Down(vk)) held.push_back(vk);
            }
        }
    }
    std::lock_guard lock(g_held_back_lock);
    g_held_back = held;
}

// a released key is let go of
bool HeldBack(int vk) {
    std::lock_guard lock(g_held_back_lock);
    auto found = std::find(g_held_back.begin(), g_held_back.end(), vk);
    if (found == g_held_back.end()) return false;
    if (Down(vk)) return true;
    g_held_back.erase(found);
    return false;
}

// the held back list is checked first, so a key let go leaves it
bool PlayDown(int vk) { return !HeldBack(vk) && Down(vk); }

const char* SectionHeading(Section section) {
    switch (section) {
        case Section::Inventory:
            return "INVENTORY";
        case Section::Car:
            return "CAR";
        case Section::Jetpack:
            return "FLYING";
        default:
            return nullptr;
    }
}

// the jetpack and the Q copter share their keys
Section KeySection(Section now) { return now == Section::Copter ? Section::Jetpack : now; }

// the on foot keys act everywhere but their moves, which give way to a vehicle's; a section's keys act only there
bool Applies(const Entry& entry, Section now) {
    if (entry.section != Section::Foot) return entry.section == KeySection(now);
    if (entry.kind == Kind::Move) return now == Section::Foot;
    // the car's right stick is its gas and brake
    if (entry.kind == Kind::Look) return now != Section::Car;
    return true;
}

// the pad button its prompts show, ButtonKey's numbering, -1 for none
int PromptButton(const Entry& entry) {
    switch (entry.kind) {
        case Kind::Analog:
        case Kind::Game:
            return entry.index;
        case Kind::Button:
            return entry.index + kFirstPadButton;
        default:
            return -1;
    }
}

constexpr int kArrows[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};

// Space accepts in the menus only where it is bound to A, Enter always; in play they do what they are bound to
bool SpaceIsA() {
    std::lock_guard lock(g_bind_lock);
    for (size_t i = 0; i < kCount; i++) {
        bool a = kEntries[i].kind == Kind::Analog && kEntries[i].index == kA;
        const std::vector<int>& keys = g_binds[i];
        if (a && std::find(keys.begin(), keys.end(), VK_SPACE) != keys.end()) return true;
    }
    return false;
}

bool AcceptDown() { return Down(VK_RETURN) || (Down(VK_SPACE) && SpaceIsA()); }

// keys held when the window got the input wait for release
bool AnyPressingKeyDown(const Binds& binds) {
    bool menu_key = AcceptDown() || Down(VK_BACK) || std::any_of(std::begin(kArrows), std::end(kArrows), Down);
    if (menu_key) return true;
    return std::any_of(binds.begin(), binds.end(), [](const std::vector<int>& keys) { return AnyDown(keys); });
}

void Press(const Entry& entry, Pad& pad, StickKeys& sticks) {
    switch (entry.kind) {
        case Kind::Analog:
            pad.analog[entry.index] = kFullPress;
            break;
        case Kind::Button:
            pad.buttons |= uint16_t(1u << entry.index);
            break;
        case Kind::Move:
            sticks.move[size_t(entry.index)] = true;
            break;
        case Kind::Look:
            sticks.look[size_t(entry.index)] = true;
            break;
        case Kind::Walk:
            sticks.walk = true;
            break;
        case Kind::Host:
        case Kind::Game:
            break;
    }
}

uint32_t TapBits(Tap tap) {
    switch (tap) {
        case Tap::Start:
            return 1u << kStart;
        case Tap::Back:
            return 1u << kBack;
        case Tap::Up:
            return 1u << kDpadUp;
        case Tap::Down:
            return 1u << kDpadDown;
        case Tap::Left:
            return 1u << kDpadLeft;
        case Tap::Right:
            return 1u << kDpadRight;
        case Tap::A:
            return 1u << (kAnalogShift + kA);
        case Tap::B:
            return 1u << (kAnalogShift + kB);
    }
    return 0;
}

void ApplyBits(Pad& pad, uint32_t bits) {
    pad.buttons |= uint16_t(bits);
    for (int i = kA; i <= kRightTrigger; i++) {
        if (bits >> (kAnalogShift + i) & 1) pad.analog[i] = kFullPress;
    }
}

// Delete clears a binding on the options pages: bound to a move key it would move the selection off the row first.
// Out of play the wheel scrolls, and with the cursor up the mouse buttons are the cursor's.
bool GameDown(int vk, bool play) {
    if (g_menu && vk == VK_DELETE) return false;
    if (!play && IsWheel(vk)) return false;
    if (!play && g_menu && IsMouseButton(vk)) return false;
    return PlayDown(vk);
}

bool AnyGameDown(const std::vector<int>& keys, bool play) {
    return std::any_of(keys.begin(), keys.end(), [play](int vk) { return GameDown(vk, play); });
}

// the look keys act in play only, where the arrows are not the dpad; in a menu the mouse buttons are the cursor's
// and the pause keys the page's cancel
void PressBoundKeys(const Binds& binds, bool play, Pad& pad, StickKeys& sticks) {
    bool page = g_page.cancel || g_page.held;
    Section now = g_section;
    for (size_t i = 0; i < kCount; i++) {
        bool look_blocked = kEntries[i].kind == Kind::Look && !play;
        bool blocked = look_blocked || (i == kPauseAction && page) || !Applies(kEntries[i], now);
        bool down = AnyGameDown(binds[i], play);
        if (!blocked && down) Press(kEntries[i], pad, sticks);
    }
}

// a change of page keys waits for Enter, Space and the pause keys to be let go, so the press that opened or closed
// a page does not act on the next
void LatchPageKeys(const Binds& binds) {
    uint32_t accept = g_page_accept;
    uint32_t cancel = g_page_cancel;
    if (accept != g_page.accept || cancel != g_page.cancel) g_page = {accept, cancel, true};
    if (!AcceptDown() && !AnyDown(binds[kPauseAction])) g_page.held = false;
}

// only out of play: the arrows are the dpad, Enter and Space accept, Backspace backs out; on a menu page Enter,
// Space and the pause keys are the page's own accept and cancel
void PressMenuKeys(const Binds& binds, Pad& pad) {
    if (AcceptDown() && !g_page.held) ApplyBits(pad, g_page.accept ? g_page.accept : TapBits(Tap::A));
    if (g_page.cancel && !g_page.held && AnyDown(binds[kPauseAction])) ApplyBits(pad, g_page.cancel);
    if (Down(VK_BACK)) pad.analog[kB] = kFullPress;
    for (size_t i = 0; i < std::size(kArrows); i++) {
        int bit = kDpadUp + int(i);
        if (Down(kArrows[i])) pad.buttons |= uint16_t(1u << bit);
    }
}

// the key goes in its box and out of the other box; vk 0 empties the box
void SetSlot(std::vector<int>& keys, int vk, int slot) {
    std::array<int, kMaxKeysPerAction> slots{};
    for (size_t i = 0; i < keys.size() && i < slots.size(); i++) slots[i] = keys[i];
    size_t other = 1 - size_t(slot);
    if (vk && slots[other] == vk) slots[other] = 0;
    slots[size_t(slot)] = vk;
    keys.clear();
    for (int key : slots) {
        if (key) keys.push_back(key);
    }
}

int Axis(bool positive, bool negative) { return int(positive) - int(negative); }

void SetSticks(Pad& pad, const StickKeys& sticks) {
    int move_x = Axis(sticks.move[kRight], sticks.move[kLeft]);
    int move_y = Axis(sticks.move[kForward], sticks.move[kBackward]);
    float move = sticks.walk ? kWalkStick : 1.0f;
    g_walk_diagonal = sticks.walk && move_x && move_y;
    if (move_x) pad.lx = int16_t(float(move_x) * move * kFullStick);
    if (move_y) pad.ly = int16_t(float(move_y) * move * kFullStick);
    if (int x = Axis(sticks.look[kLookRight], sticks.look[kLookLeft])) pad.rx = int16_t(x * kFullStick);
    if (int y = Axis(sticks.look[kLookUp], sticks.look[kLookDown])) pad.ry = int16_t(y * kFullStick);
}

void SetKeyboard(bool on) { g_keyboard = on; }

void PressTap(Pad& pad) {
    if (NowMs() < g_press_until) ApplyBits(pad, g_press);
}

void PressWheel(Pad& pad) {
    uint64_t now = NowMs();
    if (now < g_step.until) {
        pad.buttons |= g_step.button;
        return;
    }
    bool released = now >= g_step.until + kTapMs;
    int notches = g_wheel;
    if (!released || notches == 0) return;
    int step = notches > 0 ? 1 : -1;
    g_wheel -= step;
    uint16_t back = g_wheel_across ? kWheelBack : kWheelUp;
    uint16_t next = g_wheel_across ? kWheelNext : kWheelDown;
    g_step = {step > 0 ? back : next, now + kTapMs};
    pad.buttons |= g_step.button;
}

// SDL's scancodes as the virtual keys (common/keys.h) the bindings name, by position as on a US keyboard
int VkOf(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return VK_F1 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return VK_NUMPAD1 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
        case SDL_SCANCODE_0: return '0';
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
        case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return VK_BACK;
        case SDL_SCANCODE_TAB: return VK_TAB;
        case SDL_SCANCODE_SPACE: return VK_SPACE;
        case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return VK_SHIFT;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return VK_CONTROL;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return VK_MENU;
        case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
        case SDL_SCANCODE_UP: return VK_UP;
        case SDL_SCANCODE_DOWN: return VK_DOWN;
        case SDL_SCANCODE_LEFT: return VK_LEFT;
        case SDL_SCANCODE_RIGHT: return VK_RIGHT;
        case SDL_SCANCODE_INSERT: return VK_INSERT;
        case SDL_SCANCODE_DELETE: return VK_DELETE;
        case SDL_SCANCODE_HOME: return VK_HOME;
        case SDL_SCANCODE_END: return VK_END;
        case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
        case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
        case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
        case SDL_SCANCODE_KP_PLUS: return VK_ADD;
        case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
        case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
        case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
        case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
        case SDL_SCANCODE_GRAVE: return VK_OEM_3;
        case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
        case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
        case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
        case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
        case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
        case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
        case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
        case SDL_SCANCODE_SLASH: return VK_OEM_2;
        default: return 0;
    }
}

int VkOfButton(uint8_t b) {
    switch (b) {
        case SDL_BUTTON_LEFT: return VK_LBUTTON;
        case SDL_BUTTON_RIGHT: return VK_RBUTTON;
        case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
        case SDL_BUTTON_X1: return VK_XBUTTON1;
        case SDL_BUTTON_X2: return VK_XBUTTON2;
        default: return 0;
    }
}

}  // namespace

// the raw input the window thread fed on Windows is SDL's, in OnEvent below

void StartMouse(void*) {}
void OnRawInput(void*) {}

void ReleaseCursor() { g_settle = true; }

std::optional<MouseMove> TakeMouse() {
    uint64_t now = NowMs();
    bool was_idle = now - g_take_ms.exchange(now) >= kPlayMs;
    int x = g_dx.exchange(0);
    int y = g_dy.exchange(0);
    if ((x == 0 && y == 0) || was_idle || !Active()) return std::nullopt;
    const game::Options& options = game::Opt();
    float sign_x = options.invert_mouse_x ? -1.0f : 1.0f;
    float sign_y = options.invert_mouse ? -1.0f : 1.0f;
    return MouseMove{float(x) * sign_x, float(y) * sign_y};
}

void Keyboard(Pad& pad) {
    g_walk_diagonal = false;
    if (!Active()) {
        g_settle = true;
        return;
    }
    // a bind started in the game's options menu takes the next key, and the key that bound waits for release
    if (app::Capturing()) {
        g_settle = true;
        return;
    }
    Binds binds = CopyBinds();
    bool pressing = AnyPressingKeyDown(binds);
    if (g_settle && pressing) return;
    g_settle = false;
    if (pressing) SetKeyboard(true);
    bool play = Play();
    if (!g_menu) g_wheel = 0;
    LatchPageKeys(binds);
    StickKeys sticks;
    PressBoundKeys(binds, play, pad, sticks);
    if (!play) PressMenuKeys(binds, pad);
    if (!play) PressTap(pad);
    if (!play && g_menu) PressWheel(pad);
    SetSticks(pad, sticks);
    // the touch stick straight onto the axes, as its own input: it is not the keys', so past their walk and diagonal
    if (float sx = g_stick_x, sy = g_stick_y; sx != 0.0f || sy != 0.0f) {
        pad.lx = int16_t(std::clamp(sx, -1.0f, 1.0f) * float(kFullStick));
        pad.ly = int16_t(std::clamp(sy, -1.0f, 1.0f) * float(kFullStick));
    }
}

int KeyboardPort() {
    const auto& players = game::Opt().players;
    for (int port = 0; port < game::kPlayers; port++) {
        if (players[size_t(port)].keyboard) return port;
    }
    return -1;
}

// the bits a pad's report holds, in g_pad_held's numbering: the buttons, then each analog past its press, then each
// stick past its push
uint32_t HeldBits(const Pad& pad) {
    uint32_t held = pad.buttons;
    int bit = kPadDigitalButtons;
    for (uint8_t analog : pad.analog) {
        bool pressed = analog > kPadAnalogPress;
        held |= uint32_t(pressed) << bit;
        bit++;
    }
    for (int16_t axis : {pad.lx, pad.ly, pad.rx, pad.ry}) {
        bool pressed = std::abs(int(axis)) > kPadStickPress;
        held |= uint32_t(pressed) << bit;
        bit++;
    }
    return held;
}

// only new presses count, so a drifting stick never takes over from the touch controls and the keys
void SeenPad(int port, const Pad& pad) {
    if (port < 0 || port >= kPorts) return;
    uint32_t held = HeldBits(pad);
    if (held & ~g_pad_held[size_t(port)].exchange(held)) SetKeyboard(false);
}

// the touch controls' own pad bits, held as the pad's: a control that writes them is not a controller taking over, so
// the touch prompts and the keys stay the player's
void TouchHeld(int port, const Pad& pad) {
    if (port < 0 || port >= kPorts) return;
    g_pad_held[size_t(port)] |= HeldBits(pad);
}

void SetMenuWheelAcross(bool across) { g_wheel_across = across; }

void SetMenu(bool menu) { g_menu = menu; }

bool InMenu() { return g_menu; }

bool KeyboardActive() { return g_keyboard; }

bool TouchActive() { return g_touch; }

int TakeWeaponArrow() { return touch::TakeWeaponArrow(); }

bool InPlay() { return Play(); }

bool WalkDiagonal() { return g_walk_diagonal; }

void Press(Tap tap) {
    g_press = TapBits(tap);
    g_press_until = NowMs() + kTapMs;
}

void SetPageKeys(std::optional<PageKeys> keys) {
    g_page_accept = keys ? TapBits(keys->accept) : 0;
    g_page_cancel = keys ? TapBits(keys->cancel) : 0;
}

void SetSection(Section section) {
    Section before = g_section.exchange(section);
    if (before == Section::Inventory && section != Section::Inventory) HoldBackInventoryKeys();
}

std::span<const Action> Actions() {
    static const std::array<Action, kCount> list = [] {
        std::array<Action, kCount> actions{};
        for (size_t i = 0; i < kCount; i++) {
            actions[i] = kEntries[i].action;
            actions[i].section = SectionHeading(kEntries[i].section);
        }
        return actions;
    }();
    return list;
}

std::string Binding(size_t action) {
    std::lock_guard lock(g_bind_lock);
    return action < kCount ? Join(g_binds[action]) : std::string();
}

void Bind(size_t action, int vk, int slot) {
    bool known = vk == 0 || !NameOf(vk).empty();
    if (action >= kCount || slot < 0 || slot >= int(kMaxKeysPerAction) || !known) return;
    // the host keys act on the host's key events, which mouse buttons do not send
    if (kEntries[action].kind == Kind::Host && IsMouseButton(vk)) return;
    {
        std::lock_guard lock(g_bind_lock);
        std::vector<int> keys = g_binds[action];
        SetSlot(keys, vk, slot);
        // the settings panel with no key would leave a keyboard player no way back into it
        if (keys.empty() && action == kPanelAction) return;
        g_binds[action] = keys;
        game::Opt().keys[kEntries[action].action.id] = Join(keys);
    }
    app::Save();
}

void Unbind(size_t action) {
    if (action >= kCount || action == kPanelAction) return;
    {
        std::lock_guard lock(g_bind_lock);
        g_binds[action].clear();
        game::Opt().keys[kEntries[action].action.id].clear();
    }
    app::Save();
}

bool TakeDeletePress() {
    static bool held = false;
    bool down = Down(VK_DELETE);
    bool fresh = down && !held;
    held = down;
    return fresh && Active();
}

void ResetBindings() {
    game::Opt().keys.clear();
    LoadBindings();
    app::Save();
}

void LoadBindings() {
    std::lock_guard lock(g_bind_lock);
    const auto& saved = game::Opt().keys;
    for (size_t i = 0; i < kCount; i++) {
        auto found = saved.find(kEntries[i].action.id);
        std::string names = found != saved.end() ? found->second : kEntries[i].action.defaults;
        g_binds[i] = Parse(names);
        // an action may be left with no key, but the settings panel must keep one
        if (g_binds[i].empty() && i == kPanelAction) g_binds[i] = Parse(kEntries[i].action.defaults);
    }
}

int PollCapturedKey() {
    g_capture_ms = NowMs();
    // the keys the touch buttons hold are not the player's: a capture must not take them
    for (int vk = kFirstVk; vk < kEndVk; vk++) {
        if (vk > 0 && vk < 256 && g_down[size_t(vk)] && !NameOf(vk).empty()) return vk;
    }
    return 0;
}

bool CaptureRecent() { return NowMs() - g_capture_ms <= kCaptureGuardMs; }

uint64_t HeldActions() {
    if (!Active() || g_settle || app::Capturing()) return 0;
    Binds binds = CopyBinds();
    uint64_t held = 0;
    for (size_t i = 0; i < kCount; i++) {
        if (std::any_of(binds[i].begin(), binds[i].end(), PlayDown)) held |= uint64_t(1) << i;
    }
    return held;
}

int ActionIndex(std::string_view id) {
    for (size_t i = 0; i < kCount; i++) {
        if (kEntries[i].action.id == id) return int(i);
    }
    return -1;
}

// the action is bound to this key and no key capture wants it
bool HostKeyBound(size_t action, int vk) {
    if (CaptureRecent()) return false;
    std::lock_guard lock(g_bind_lock);
    const std::vector<int>& keys = g_binds[action];
    return std::find(keys.begin(), keys.end(), vk) != keys.end();
}

bool PanelKey(int vk) { return HostKeyBound(kPanelAction, vk); }

bool HudKey(int vk) { return HostKeyBound(kHudAction, vk); }

bool TexturesKey(int vk) { return HostKeyBound(kTexturesAction, vk); }

}  // namespace input

// the touch controls
namespace touch {

namespace {

using input::NowMs;

// what a finger on the screen does
enum class Role : uint8_t { None, Stick, Look, Button, Cursor, Drag, Pinch };

// a control of the touch layout: what it shows and what it presses: the bound keys of its action in the game's
// own keyboard flow, or a fixed menu key, or the host's. Where it sits is the layout's, which the player edits and
// picks presets for
enum : int { kPanel = -1, kByAction = -3, kPad = -4 };

// an icon in white line art inside the button
enum class Icon : uint8_t { Bullet, Scope, Action, Focus, Cover, Roll, Reload, WeaponLeft, WeaponRight, Crouch, Case,
                            Start, Gear, Back, Accel, Brake, Handbrake, Slasher, FlyUp, FlyDown, Land };
// Play while the player has the controls, OnFoot walking, Car driving, Jetpack flying the jetpack or the copter, Game
// outside the menus (cutscenes too), Menus in the menus and options, Always in both
enum class When : uint8_t { Play, OnFoot, Car, Jetpack, Menus, Game, Always };

constexpr size_t Act(std::string_view id) {
    size_t i = 0;
    while (input::kEntries[i].action.id != id) i++;
    return i;
}

struct Button {
    const char* id;     // its key in layout.toml
    const char* label;  // in the layout editor, and the name the prompts give it
    const char* badge;  // the console control it stands in for, on the button
    Icon icon;
    int press;      // kPanel, kPad, a fixed menu key, or kByAction: the action's bound keys
    size_t action;  // kByAction: the entry in the game's action table
    When when;
    int pad = -1;       // kPad: a pad control in ButtonKey's numbering, 0-7 analog, 8-15 the buttons
    bool pill = false;  // drawn as a trigger, wide
    bool look = false;  // a drag from it turns the view as well, as a fire button does
    bool keep = false;  // never hidden: the way back out of the menus
    bool inv = false;   // also shown in the inventory, where it picks and confirms as the console's dpad, A and B
};
// the game's own controller, on the screen: the triggers for the gun and the lock on, the face buttons for what Bond
// does, the dpad for the weapons and the left stick pressed for the crouch. Driving has its own buttons where the
// console has them, so no button does two things
constexpr Button kButtons[] = {
    {"fire", "Fire", "R", Icon::Bullet, kByAction, Act("fire"), When::Play, -1, true, true},
    {"lock_on", "Lock on", "L", Icon::Scope, kByAction, Act("lock_on"), When::Play, -1, true},
    {"interact", "Action", "A", Icon::Action, kByAction, Act("interact"), When::OnFoot, -1, false, false, false, true},
    {"focus", "Focus", "X", Icon::Focus, kByAction, Act("focus"), When::OnFoot},
    {"cover", "Wall cover", "Y", Icon::Cover, kByAction, Act("wall_cover"), When::OnFoot},
    {"roll", "Dive roll", "B", Icon::Roll, kByAction, Act("dive_roll"), When::OnFoot, -1, false, false, false, true},
    {"reload", "Reload", "", Icon::Reload, kByAction, Act("reload"), When::OnFoot},
    {"weapon_left", "Weapon left", "<", Icon::WeaponLeft, kPad, 0, When::OnFoot, 8 + input::kDpadLeft, false, false,
     false, true},
    {"weapon_right", "Weapon right", ">", Icon::WeaponRight, kPad, 0, When::OnFoot, 8 + input::kDpadRight,
     false, false, false, true},
    {"crouch", "Crouch", "L3", Icon::Crouch, kByAction, Act("crouch"), When::OnFoot},
    {"inventory", "Inventory", "BACK", Icon::Case, kPad, 0, When::OnFoot, 8 + input::kBack},
    {"pause", "Pause", "START", Icon::Start, kByAction, Act("pause"), When::Game},
    {"back", "Back", "BACK", Icon::Back, VK_BACK, 0, When::Menus, -1, false, false, true},
    // the settings panel over the game, where its cheats give any weapon; a tap again closes it
    {"panel", "Panel", "", Icon::Gear, kPanel, 0, When::Always},
    // driving: the pedals on A and X, the handbrake on B, the slasher and the way out on Y as the console has them
    {"car_accel", "Accelerate", "A", Icon::Accel, kByAction, Act("car_accelerate"), When::Car},
    {"car_brake", "Brake", "X", Icon::Brake, kByAction, Act("car_brake"), When::Car},
    {"car_handbrake", "Handbrake", "B", Icon::Handbrake, kByAction, Act("car_handbrake"), When::Car},
    {"car_slasher", "Slasher", "Y", Icon::Slasher, kPad, 0, When::Car, input::kY},
    // the jetpack and the copter: the thrust on A and X, the way down on Y as the console has them
    {"fly_up", "Fly up", "A", Icon::FlyUp, kByAction, Act("fly_up"), When::Jetpack},
    {"fly_down", "Fly down", "X", Icon::FlyDown, kByAction, Act("fly_down"), When::Jetpack},
    {"fly_exit", "Land / exit", "Y", Icon::Land, kByAction, Act("fly_exit"), When::Jetpack},
};
constexpr size_t kFireTouch = Act("fire");
constexpr size_t kCarAccel = Act("car_accelerate");
constexpr size_t kCarBrake = Act("car_brake");
constexpr size_t kButtonCount = std::size(kButtons);
constexpr size_t kStick = kButtonCount;  // the stick's place, after the buttons'

// a control's place in the buttons, by its id
constexpr size_t ButtonAt(std::string_view id) {
    size_t i = 0;
    while (std::string_view(kButtons[i].id) != id) i++;
    return i;
}
// the weapon arrows, for the switch a tap of one starts
constexpr size_t kWeaponLeftButton = ButtonAt("weapon_left");
constexpr size_t kWeaponRightButton = ButtonAt("weapon_right");

// where a control sits: its centre as shares of the screen's width and height, its radius in screen heights
struct Place {
    float x, y, r;
    bool hidden = false;
};
using Layout = std::array<Place, kButtonCount + 1>;
// fire, lock on, action, focus, cover, roll, reload, weapon left, weapon right, crouch, inventory, pause, back, panel,
// accelerate, brake, handbrake, slasher, fly up, fly down, land, stick
// the thumbs alone: the triggers under the index fingers, the face buttons under the right thumb
constexpr Layout kTwoFinger = {{
    {0.905f, 0.20f, 0.075f}, {0.095f, 0.20f, 0.075f}, {0.80f, 0.80f, 0.058f}, {0.70f, 0.70f, 0.05f},
    {0.80f, 0.60f, 0.052f},  {0.90f, 0.70f, 0.05f},   {0.895f, 0.50f, 0.045f}, {0.71f, 0.44f, 0.038f},
    {0.80f, 0.44f, 0.038f},  {0.255f, 0.60f, 0.045f}, {0.055f, 0.34f, 0.042f}, {0.945f, 0.34f, 0.042f},
    {0.055f, 0.10f, 0.042f}, {0.50f, 0.06f, 0.032f},  {0.86f, 0.82f, 0.065f},  {0.86f, 0.64f, 0.055f},
    {0.72f, 0.82f, 0.055f},  {0.72f, 0.64f, 0.05f},   {0.60f, 0.58f, 0.055f},  {0.60f, 0.78f, 0.055f},
    {0.46f, 0.68f, 0.05f},   {0.13f, 0.72f, 0.12f},
}};
// claw with the left index finger: the triggers at the top corners, the right thumb turns and aims
constexpr Layout kThreeFinger = {{
    {0.90f, 0.14f, 0.085f},  {0.10f, 0.14f, 0.085f}, {0.78f, 0.85f, 0.06f}, {0.67f, 0.72f, 0.05f},
    {0.78f, 0.58f, 0.055f},  {0.89f, 0.72f, 0.05f},  {0.89f, 0.50f, 0.045f}, {0.68f, 0.44f, 0.038f},
    {0.78f, 0.44f, 0.038f},  {0.255f, 0.62f, 0.045f}, {0.055f, 0.34f, 0.042f}, {0.945f, 0.30f, 0.042f},
    {0.055f, 0.10f, 0.042f}, {0.50f, 0.06f, 0.032f},  {0.87f, 0.84f, 0.065f},  {0.87f, 0.66f, 0.055f},
    {0.72f, 0.84f, 0.055f},  {0.72f, 0.66f, 0.05f},   {0.58f, 0.58f, 0.055f},  {0.58f, 0.80f, 0.055f},
    {0.44f, 0.69f, 0.05f},   {0.15f, 0.74f, 0.13f},
}};
// both index fingers on top: the thumbs low, the face buttons where the right thumb rests
constexpr Layout kFourFinger = {{
    {0.91f, 0.13f, 0.08f},   {0.09f, 0.13f, 0.08f},  {0.82f, 0.87f, 0.06f}, {0.71f, 0.75f, 0.05f},
    {0.82f, 0.62f, 0.055f},  {0.93f, 0.75f, 0.05f},  {0.91f, 0.50f, 0.045f}, {0.70f, 0.48f, 0.038f},
    {0.80f, 0.48f, 0.038f},  {0.27f, 0.62f, 0.045f}, {0.06f, 0.36f, 0.042f}, {0.94f, 0.32f, 0.042f},
    {0.06f, 0.10f, 0.042f},  {0.50f, 0.06f, 0.032f},  {0.88f, 0.82f, 0.065f}, {0.88f, 0.65f, 0.055f},
    {0.73f, 0.82f, 0.055f},  {0.73f, 0.65f, 0.05f},   {0.60f, 0.58f, 0.055f}, {0.60f, 0.82f, 0.055f},
    {0.46f, 0.70f, 0.05f},   {0.17f, 0.74f, 0.13f},
}};
constexpr bool AllPlaced(const Layout& layout) {
    for (const Place& p : layout)
        if (p.r < 0.02f) return false;
    return true;
}
static_assert(AllPlaced(kTwoFinger) && AllPlaced(kThreeFinger) && AllPlaced(kFourFinger), "every control placed");
struct Preset {
    const char* id;
    const char* label;
    const Layout* places;
};
constexpr Preset kPresets[] = {
    {"default", "DEFAULT", &kTwoFinger}, {"three", "3 FINGER", &kThreeFinger}, {"four", "4 FINGER", &kFourFinger}};

constexpr float kStickZone = 0.42f;    // share of the width from the left a touch takes the stick in
constexpr float kLookCounts = 4500.0f;  // mouse counts a swipe across the screen's width turns: 180 degrees, at
                                        // sensitivity 1; the settings' HORIZONTAL SENSITIVITY lowers it from there
constexpr uint64_t kShownMs = 8000;     // the controls hide this long after the last touch while a pad is plugged in

// a touch in the menus is a tap, a swipe up or down that scrolls, or the cursor held down (a slider dragged): which,
// once it moved or stayed long enough to tell
enum class MenuTouch : uint8_t { Undecided, Cursor, Scroll };
constexpr float kMenuMove = 0.03f;     // screen heights a finger moves before it is a swipe or a drag
constexpr float kScrollNotch = 0.05f;  // screen heights of a swipe a row scrolls, about a row of the menus
constexpr uint64_t kHoldMs = 180;      // a finger still this long is the cursor held down
constexpr int kMaxNotches = 8;         // notches a swipe runs ahead of the menu

struct Finger {
    SDL_FingerID id = 0;
    Role role = Role::None;
    size_t button = 0;     // or kStick, dragged in the editor
    float x0 = 0, y0 = 0;  // where it went down, pixels; in the editor, its offset from the control's centre
    float x = 0, y = 0;    // where it is
    MenuTouch menu = MenuTouch::Undecided;
    uint64_t down_ms = 0;
    float notch_y = 0;  // a scroll's place at its last notch
};

std::mutex g_lock;
std::vector<Finger> g_fingers;
bool g_pressed[kButtonCount] = {};
std::atomic<int> g_weapon_arrow{0};  // a fresh tap of an arrow, for the weapon switch it starts
uint64_t g_pad_until[kButtonCount] = {};  // a tapped pad control stays down until then
float g_cursor_x = 0, g_cursor_y = 0;  // the menu cursor, pixels
bool g_cursor_down = false;
bool g_cursor_touch = false;  // the cursor follows a finger, not the mouse
uint64_t g_cursor_tap_until = 0;  // a tap holds the cursor down this long, so the menu sees a click
std::atomic<uint64_t> g_touch_ms{0};
std::atomic<uint64_t> g_back_until{0};  // the back key's tap of Start or B lasts until then
constexpr uint64_t kBackTapMs = 120;
// a tap on a pad control stays down this long: Android hands a tap over as a down and an up in the same
// batch, so the game's own slower polling of the pad would never see the button pressed at all
constexpr uint64_t kPadTapMs = 150;

// the layout in use, under g_lock
Layout g_layout = kTwoFinger;
size_t g_preset = 0;
float g_opacity = 0.85f;
float g_scale = 1.0f;  // every control's size, on top of its own
bool g_fixed_stick = false;  // the stick stays in its place, else it comes down under the thumb
bool g_loaded = false;
// the editor: the layout before it opened (for Cancel), the control picked
std::atomic<bool> g_editing{false};
Layout g_before = kTwoFinger;
float g_before_opacity = 0.85f;
size_t g_before_preset = 0;
int g_selected = -1;

float Width() {
    int w, h;
    host::WindowPixels(w, h);
    return float(std::max(w, 1));
}
float Height() {
    int w, h;
    host::WindowPixels(w, h);
    return float(std::max(h, 1));
}

std::filesystem::path LayoutFile() { return app::ExeDir() / "layout.toml"; }

void LoadLayout() {
    g_loaded = true;
    try {
        toml::table t = toml::parse_file(LayoutFile().string());
        std::string preset = t["preset"].value_or(std::string("default"));
        for (size_t i = 0; i < std::size(kPresets); i++)
            if (preset == kPresets[i].id) g_preset = i;
        g_layout = *kPresets[g_preset].places;
        g_opacity = std::clamp(float(t["opacity"].value_or(double(g_opacity))), 0.2f, 1.0f);
        g_scale = std::clamp(float(t["scale"].value_or(double(g_scale))), 0.5f, 1.5f);
        g_fixed_stick = t["stick"].value_or(std::string("floating")) == "fixed";
        for (size_t i = 0; i <= kButtonCount; i++) {
            // the menus' controls are not in the editor: always the preset's
            if (i != kStick && kButtons[i].when == When::Menus) continue;
            const char* id = i == kStick ? "stick" : kButtons[i].id;
            const toml::table* p = t["controls"][id].as_table();
            if (!p) continue;
            Place& pl = g_layout[i];
            pl.x = std::clamp(float((*p)["x"].value_or(double(pl.x))), 0.0f, 1.0f);
            pl.y = std::clamp(float((*p)["y"].value_or(double(pl.y))), 0.0f, 1.0f);
            pl.r = std::clamp(float((*p)["r"].value_or(double(pl.r))), 0.02f, 0.25f);
            pl.hidden = (*p)["hidden"].value_or(false) && i != kStick && !kButtons[i].keep;
        }
    } catch (...) {
    }
}

void SaveLayout() {
    toml::table controls;
    for (size_t i = 0; i <= kButtonCount; i++) {
        const Place& p = g_layout[i];
        controls.insert(i == kStick ? "stick" : kButtons[i].id,
                        toml::table{{"x", double(p.x)}, {"y", double(p.y)}, {"r", double(p.r)}, {"hidden", p.hidden}});
    }
    toml::table t{{"preset", kPresets[g_preset].id}, {"opacity", double(g_opacity)}, {"scale", double(g_scale)},
                  {"stick", g_fixed_stick ? "fixed" : "floating"}, {"controls", controls}};
    std::ofstream(LayoutFile()) << t << "\n";
}

void EnsureLayout() {
    if (!g_loaded) LoadLayout();
}

void OpenEditor() {
    g_before = g_layout;
    g_before_opacity = g_opacity;
    g_before_preset = g_preset;
    g_selected = -1;
    g_editing = true;
}

bool InMenus() { return input::g_menu; }

bool ButtonShown(size_t i) {
    const Button& b = kButtons[i];
    if (g_editing) return b.when != When::Menus;
    if (g_layout[i].hidden) return false;
    if (b.when == When::Always) return true;
    if (b.when == When::Game) return !InMenus();
    if (b.when == When::Menus) return InMenus();
    // in play only while the player has the controls: none over a menu page or a film
    if (InMenus() || FilmShown()) return false;
    // the inventory pauses the game and takes the dpad and A/B: the controls that pick, confirm and cancel stay on
    // screen, the rest of the play controls do not
    input::Section section = input::g_section;
    if (section == input::Section::Inventory) return b.inv;
    if (b.when == When::OnFoot) return section == input::Section::Foot;
    if (b.when == When::Car) return section == input::Section::Car;
    if (b.when == When::Jetpack) return section == input::Section::Jetpack || section == input::Section::Copter;
    return true;  // When::Play, on foot, in the jetpack, the copter and the car
}

void Centre(size_t i, float& cx, float& cy) {
    cx = g_layout[i].x * Width();
    cy = g_layout[i].y * Height();
}

float Radius(size_t i) { return g_layout[i].r * g_scale * Height(); }

// the editor's bar at the top: Reset, smaller and bigger for the picked control, hide or show it, Cancel, Done, in
// screen shares
constexpr const char* kBarLabels[] = {"RESET", "-", "+", "HIDE", "CANCEL", "DONE"};
enum { kBarReset, kBarSmaller, kBarBigger, kBarHide, kBarCancel, kBarDone, kBarCount };
constexpr float kBarY = 0.17f, kBarH = 0.09f, kBarW = 0.14f, kBarGap = 0.02f;
constexpr float kBarSizeW = 0.07f;                       // the - and + buttons
constexpr float kMinRadius = 0.02f, kMaxRadius = 0.25f;  // a control's own radius, in screen heights
constexpr float kSizeStep = 1.1f;                        // a tap on - or +

float BarWidth(int i) { return i == kBarSmaller || i == kBarBigger ? kBarSizeW : kBarW; }

// the picked control can be hidden: not the stick, nor the way back out of the menus
bool Hideable(int i) { return i >= 0 && size_t(i) < kButtonCount && !kButtons[size_t(i)].keep; }

const char* BarLabel(int i) {
    if (i == kBarHide && Hideable(g_selected) && g_layout[size_t(g_selected)].hidden) return "SHOW";
    return kBarLabels[i];
}

// the bar's button under a point, -1 for none
int BarAt(float x, float y, float rect[4] = nullptr, int which = -1) {
    float w = Width(), h = Height();
    float total = (kBarCount - 1) * kBarGap;
    for (int i = 0; i < kBarCount; i++) total += BarWidth(i);
    float left = 0.5f - total / 2;
    for (int i = 0; i < kBarCount; i++) {
        float x0 = left * w, y0 = kBarY * h;
        float x1 = x0 + BarWidth(i) * w, y1 = y0 + kBarH * h;
        left += BarWidth(i) + kBarGap;
        if (i == which && rect) {
            rect[0] = x0, rect[1] = y0, rect[2] = x1, rect[3] = y1;
            return i;
        }
        if (which < 0 && x >= x0 && x <= x1 && y >= y0 && y <= y1) return i;
    }
    return -1;
}

void ApplyPreset(size_t i) {
    g_preset = i;
    g_layout = *kPresets[i].places;
    g_selected = -1;
}

void CloseEditor(bool keep) {
    if (keep) {
        SaveLayout();
    } else {
        g_layout = g_before;
        g_opacity = g_before_opacity;
        g_preset = g_before_preset;
    }
    g_selected = -1;
    g_editing = false;
}

// the button under a point, kButtonCount for none
size_t HitButton(float x, float y) {
    for (size_t i = 0; i < kButtonCount; i++) {
        if (!ButtonShown(i)) continue;
        float cx, cy;
        Centre(i, cx, cy);
        float r = Radius(i) * 1.25f;  // a little slack around the drawn circle
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) return i;
    }
    return kButtonCount;
}

// on the stick's circle, with the buttons' slack: a fixed stick moved out of the left of the screen still takes it
bool InStick(float x, float y) {
    float cx = g_layout[kStick].x * Width(), cy = g_layout[kStick].y * Height(), r = Radius(kStick) * 1.25f;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

void UpdateStick(const Finger& f) {
    float r = Radius(kStick);
    float dx = (f.x - f.x0) / r, dy = (f.y - f.y0) / r;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1) {
        dx /= len;
        dy /= len;
    }
    input::g_stick_x = dx;
    input::g_stick_y = -dy;
    // the car reads its gas and brake as the keys its actions are bound to, its steering off the stick's x
    if (input::g_section == input::Section::Car) {
        input::g_stick_y = 0;
        constexpr float kPush = 0.25f;  // the stick pushed this far past its rest presses the pedal
        input::HoldActionKeys(kCarAccel, dy < -kPush);
        input::HoldActionKeys(kCarBrake, dy > kPush);
    } else {
        input::HoldActionKeys(kCarAccel, false);
        input::HoldActionKeys(kCarBrake, false);
    }
}

// a dpad arrow held in the menus: a step as it comes down, then steps at a calm pace once held a while; the game
// steps on every poll the arrow is down, too quick to land on a row
constexpr uint64_t kStepMs = 120, kRepeatAfterMs = 450, kRepeatMs = 180;

bool DpadStep(uint64_t held) {
    if (held < kStepMs) return true;
    return held >= kRepeatAfterMs && (held - kRepeatAfterMs) % kRepeatMs < kStepMs / 2;
}

// the menus' dpad buttons, whose keys step at that pace
bool Dpad(size_t i) {
    if (kButtons[i].when != When::Menus) return false;
    int press = kButtons[i].press;
    return press == VK_UP || press == VK_DOWN || press == VK_LEFT || press == VK_RIGHT;
}

// the held buttons and the stick recounted from the fingers on the screen
void Recount() {
    std::fill(std::begin(g_pressed), std::end(g_pressed), false);
    // a pad control tapped stays down a moment: the game reads the pad slower than the touch screen
    uint64_t now = NowMs();
    for (size_t i = 0; i < kButtonCount; i++)
        if (kButtons[i].press == kPad && now < g_pad_until[i]) g_pressed[i] = true;
    for (size_t i = 0; i < kButtonCount; i++)
        if (Dpad(i)) input::g_button_keys[size_t(kButtons[i].press)] = false;
    bool stick = false;
    for (const Finger& f : g_fingers) {
        if (f.role == Role::Button) {
            g_pressed[f.button] = !Dpad(f.button) || DpadStep(NowMs() - f.down_ms);
            if (Dpad(f.button)) input::g_button_keys[size_t(kButtons[f.button].press)] = g_pressed[f.button];
        }
        if (f.role == Role::Stick) stick = true;
    }
    if (!stick) {
        input::g_stick_x = 0;
        input::g_stick_y = 0;
        input::HoldActionKeys(kCarAccel, false);
        input::HoldActionKeys(kCarBrake, false);
    }
}

void Resize(size_t i, float r) { g_layout[i].r = std::clamp(r, kMinRadius, kMaxRadius); }

// the editor: a touch on the bar resets, resizes the picked control, cancels or saves; a second finger while one
// drags a control pinches it bigger or smaller; on a control picks it up to move it, anywhere else lets go of the pick
void EditDown(Finger& f, float x, float y) {
    int bar = BarAt(x, y);
    switch (bar) {
        case kBarReset:
            ApplyPreset(g_preset);
            return;
        case kBarSmaller:
        case kBarBigger:
            if (g_selected >= 0) {
                float r = g_layout[size_t(g_selected)].r;
                Resize(size_t(g_selected), bar == kBarBigger ? r * kSizeStep : r / kSizeStep);
            }
            return;
        case kBarHide:
            if (Hideable(g_selected)) g_layout[size_t(g_selected)].hidden ^= true;
            return;
        case kBarCancel:
            CloseEditor(false);
            return;
        case kBarDone:
            CloseEditor(true);
            return;
        default:
            break;
    }
    for (const Finger& d : g_fingers) {
        if (d.role != Role::Drag) continue;
        // the span between the fingers and the control's size as the pinch began
        f.role = Role::Pinch;
        f.button = d.button;
        f.x0 = std::max(std::hypot(x - d.x, y - d.y), 1.0f);
        f.y0 = g_layout[d.button].r;
        return;
    }
    size_t b = HitButton(x, y);
    float sx = g_layout[kStick].x * Width(), sy = g_layout[kStick].y * Height(), sr = Radius(kStick);
    if (b == kButtonCount && (x - sx) * (x - sx) + (y - sy) * (y - sy) <= sr * sr) b = kStick;
    if (b > kStick) {
        g_selected = -1;
        return;
    }
    g_selected = int(b);
    f.role = Role::Drag;
    f.button = b;
    f.x0 = x - g_layout[b].x * Width();
    f.y0 = y - g_layout[b].y * Height();
}

// a touch button holds the keys of the action it presses, or its fixed menu key: the game's own keyboard flow then
// presses the action onto the pad, as the key itself would. A button that presses a pad control of its own sets it
// in Apply, straight on the pad
void PressButton(size_t b, bool down) {
    int press = kButtons[b].press;
    if (press == kPad) return;
    if (press == kByAction) input::HoldActionKeys(kButtons[b].action, down);
    else if (press >= 0) input::g_button_keys[size_t(press)] = down;
}

void FingerDown(SDL_FingerID id, float x, float y) {
    Finger f;
    f.id = id;
    f.x0 = f.x = x;
    f.y0 = f.y = y;
    if (g_editing) {
        EditDown(f, x, y);
        g_fingers.push_back(f);
        return;
    }
    // the touch controls are the keyboard player's: their prompts name their buttons, and the look keeps working
    input::g_keyboard = true;
    input::g_touch = true;
    size_t b = HitButton(x, y);
    if (b < kButtonCount) {
        f.role = Role::Button;
        f.button = b;
        f.down_ms = NowMs();
        if (kButtons[b].press == kPad) g_pad_until[b] = 0;
        if (kButtons[b].press == kPanel) app::ToggleMenu();
        PressButton(b, true);
    } else if (InMenus()) {
        // not the cursor yet: a tap clicks where it lifts, a swipe scrolls without moving the cursor
        f.role = Role::Cursor;
        f.menu = MenuTouch::Undecided;
        f.down_ms = NowMs();
        g_cursor_touch = true;
    } else if (x < Width() * kStickZone || (g_fixed_stick && InStick(x, y))) {
        f.role = Role::Stick;
        // fixed: pushed from its own centre, at once wherever the thumb came down
        if (g_fixed_stick) {
            f.x0 = g_layout[kStick].x * Width();
            f.y0 = g_layout[kStick].y * Height();
            UpdateStick(f);
        }
    } else {
        f.role = Role::Look;
    }
    g_fingers.push_back(f);
    Recount();
}

// the finger becomes the cursor held down where it is
void MenuHold(Finger& f) {
    f.menu = MenuTouch::Cursor;
    g_cursor_x = f.x;
    g_cursor_y = f.y;
    g_cursor_down = true;
}

// a finger in the menus moved: up or down past kMenuMove scrolls, sideways is the cursor dragging; a scroll gives a
// wheel notch a kScrollNotch, a finger going up shows the rows below as on a phone's lists
void MenuMove(Finger& f) {
    float h = Height();
    float dx = f.x - f.x0, dy = f.y - f.y0;
    if (f.menu == MenuTouch::Undecided) {
        if (std::fabs(dy) > kMenuMove * h && std::fabs(dy) >= std::fabs(dx)) {
            f.menu = MenuTouch::Scroll;
            f.notch_y = f.y0;
        } else if (std::fabs(dx) > kMenuMove * h) {
            MenuHold(f);
        }
    }
    if (f.menu == MenuTouch::Cursor) {
        g_cursor_x = f.x;
        g_cursor_y = f.y;
    }
    if (f.menu != MenuTouch::Scroll) return;
    // a key capture keeps the taps for itself: its menu does not scroll under it
    bool capture = input::CaptureRecent();
    float notch = kScrollNotch * h;
    while (f.y <= f.notch_y - notch) {
        f.notch_y -= notch;
        if (!capture && input::g_wheel > -kMaxNotches) input::g_wheel--;
    }
    while (f.y >= f.notch_y + notch) {
        f.notch_y += notch;
        if (!capture && input::g_wheel < kMaxNotches) input::g_wheel++;
    }
}

// a finger in the menus still since it came down: held long enough, it is the cursor held down (a slider dragged)
void MenuHolds() {
    uint64_t now = NowMs();
    for (Finger& f : g_fingers)
        if (f.role == Role::Cursor && f.menu == MenuTouch::Undecided && now - f.down_ms >= kHoldMs) MenuHold(f);
}

void FingerMove(SDL_FingerID id, float x, float y) {
    for (Finger& f : g_fingers) {
        if (f.id != id) continue;
        float dx = x - f.x, dy = y - f.y;
        f.x = x;
        f.y = y;
        if (f.role == Role::Drag) {
            Place& p = g_layout[f.button];
            p.x = std::clamp((x - f.x0) / Width(), 0.0f, 1.0f);
            p.y = std::clamp((y - f.y0) / Height(), 0.0f, 1.0f);
        }
        // a pinch sizes the control by the span between its two fingers, whichever moved
        if (f.role == Role::Drag || f.role == Role::Pinch) {
            for (const Finger& o : g_fingers) {
                const Finger& pinch = f.role == Role::Pinch ? f : o;
                const Finger& drag = f.role == Role::Pinch ? o : f;
                if (&o == &f || pinch.role != Role::Pinch || drag.role != Role::Drag || pinch.button != drag.button) continue;
                Resize(f.button, pinch.y0 * std::hypot(pinch.x - drag.x, pinch.y - drag.y) / pinch.x0);
            }
        }
        if (f.role == Role::Stick) UpdateStick(f);
        if (f.role == Role::Look || (f.role == Role::Button && kButtons[f.button].look)) {
            // a drag turns the view as the mouse does, in mouse counts
            float k = kLookCounts / Width();
            AddLook(dx * k, dy * k);
        }
        if (f.role == Role::Cursor) MenuMove(f);
    }
}

void FingerUp(SDL_FingerID id) {
    for (const Finger& f : g_fingers) {
        if (f.id != id) continue;
        if (f.role == Role::Button) {
            PressButton(f.button, false);
            // a tap too short for the game's pad poll still reads: hold its bit a moment
            if (kButtons[f.button].press == kPad)
                g_pad_until[f.button] = std::max(g_pad_until[f.button], f.down_ms + kPadTapMs);
        }
        if (f.role != Role::Cursor) continue;
        if (f.menu == MenuTouch::Undecided) {
            // a tap: the cursor goes there and clicks
            g_cursor_x = f.x;
            g_cursor_y = f.y;
            g_cursor_tap_until = NowMs() + input::kTapMs;
        }
        if (f.menu != MenuTouch::Scroll) g_cursor_down = false;
    }
    std::erase_if(g_fingers, [id](const Finger& f) { return f.id == id; });
    Recount();
}

bool PadPlugged() {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) SDL_free(ids);
    return n > 0;
}

void KeyEvent(const SDL_KeyboardEvent& k) {
    // Android's back key: out of the layout editor or the settings panel, else the game's pause menu (its options
    // are the settings), in the game's menus back out of them
    if (k.scancode == SDL_SCANCODE_AC_BACK) {
        if (!k.down || k.repeat) return;
        std::lock_guard lock(g_lock);
        if (g_editing) {
            CloseEditor(false);
        } else if (app::MenuOpen()) {
            app::ToggleMenu();
        } else {
            // the game's pause keys held a moment: pause in play, a page's cancel in the menus
            g_back_until = NowMs() + kBackTapMs;
            input::HoldActionKeys(input::kPauseAction, true);
        }
        return;
    }
    int vk = input::VkOf(k.scancode);
    if (!vk) return;
    input::g_down[size_t(vk)] = k.down;
    if (!k.down) return;
    input::g_keyboard = true;
    input::g_touch = false;
    if (!k.repeat && input::PanelKey(vk)) app::ToggleMenu();
    else if (!k.repeat && !app::MenuOpen() && input::HudKey(vk)) game::g_hide_hud = !game::g_hide_hud;
    else if (!k.repeat && !app::MenuOpen() && input::TexturesKey(vk)) app::ToggleTextureReplacement();
    else if (vk == VK_ESCAPE && app::MenuOpen() && !app::Capturing()) app::ToggleMenu();
}

}  // namespace

bool OnEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            KeyEvent(e.key);
            return app::MenuOpen() ? false : true;
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) return true;
            input::g_keyboard = true;
            input::g_touch = false;
            if (input::Active() && input::Play()) {
                // the fractions carried over: a held mouse sends small moves, rounded each they were lost
                AddLook(float(e.motion.xrel), float(e.motion.yrel));
            } else {
                std::lock_guard lock(g_lock);
                g_cursor_x = float(e.motion.x);
                g_cursor_y = float(e.motion.y);
                g_cursor_touch = false;
            }
            return !app::MenuOpen();
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.which == SDL_TOUCH_MOUSEID) return true;
            if (int vk = input::VkOfButton(e.button.button)) input::g_down[size_t(vk)] = e.button.down;
            input::g_keyboard = true;
            input::g_touch = false;
            return !app::MenuOpen();
        case SDL_EVENT_MOUSE_WHEEL:
            if (e.wheel.y != 0) {
                int notch = e.wheel.y > 0 ? 1 : -1;
                input::g_wheel_ms[notch < 0] = NowMs();
                if (input::Active() && !input::Play() && !input::CaptureRecent()) input::g_wheel += notch;
            }
            return !app::MenuOpen();
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            g_touch_ms = NowMs();
            float x = e.tfinger.x * Width(), y = e.tfinger.y * Height();
            std::lock_guard lock(g_lock);
            EnsureLayout();
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                // the settings panel takes the touches, but for the button that closes it
                if (app::MenuOpen() && !g_editing) {
                    size_t b = HitButton(x, y);
                    if (b == kButtonCount || kButtons[b].press != kPanel) return false;
                }
                FingerDown(e.tfinger.fingerID, x, y);
                return true;
            }
            bool ours = std::any_of(g_fingers.begin(), g_fingers.end(),
                                    [&](const Finger& f) { return f.id == e.tfinger.fingerID; });
            if (!ours) return false;
            if (e.type == SDL_EVENT_FINGER_MOTION) FingerMove(e.tfinger.fingerID, x, y);
            else FingerUp(e.tfinger.fingerID);
            return true;
        }
        default:
            return false;
    }
}

// The touch buttons that hold keys go on the pad in the game's own keyboard flow, applied in input::Keyboard. A button
// that presses a pad control of its own, the weapon pick and the car's Y, sets it here straight on the pad. What else
// runs here is the touch state that goes by the clock: the back key's tap of the pause keys, each time the game reads
// port 0
void Apply(int port, input::Pad& pad) {
    if (port != 0 || g_editing) return;
    std::lock_guard lock(g_lock);
    Recount();
    // the weapon arrows: the game has no on foot action for the dpad, so a fresh tap of an arrow is taken here, from
    // the button itself (the pad's bits are no guide: the menu cursor presses those too) and the game runs the item
    // select HUD for it
    static bool was_left = false, was_right = false;
    bool left = g_pressed[kWeaponLeftButton] && ButtonShown(kWeaponLeftButton);
    bool right = g_pressed[kWeaponRightButton] && ButtonShown(kWeaponRightButton);
    if (left && !was_left) g_weapon_arrow = -1;
    if (right && !was_right) g_weapon_arrow = 1;
    was_left = left;
    was_right = right;
    input::Pad touched;  // the bits the touch controls write, held as the pad's so they are no new press
    for (size_t i = 0; i < kButtonCount; i++) {
        const Button& b = kButtons[i];
        if (b.press != kPad || !g_pressed[i] || !ButtonShown(i)) continue;
        if (b.pad < input::kFirstPadButton) {
            pad.analog[b.pad] = input::kFullPress;
            touched.analog[b.pad] = input::kFullPress;
        } else {
            uint16_t bit = uint16_t(1u << (b.pad - input::kFirstPadButton));
            pad.buttons |= bit;
            touched.buttons |= bit;
        }
    }
    if (touched.buttons || std::any_of(std::begin(touched.analog), std::end(touched.analog),
                                       [](uint8_t a) { return a != 0; }))
        input::TouchHeld(port, touched);
    if (g_back_until && NowMs() >= g_back_until) {
        input::HoldActionKeys(input::kPauseAction, false);
        g_back_until = 0;
    }
}

bool Shown() { return !PadPlugged() || NowMs() - g_touch_ms < kShownMs; }

int TakeWeaponArrow() { return g_weapon_arrow.exchange(0); }

namespace {

constexpr float kPi = 3.14159265f;

// an icon in white line art inside a button of radius r: points in units of r, from its centre, y down
struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float r;
    ImU32 col;
    float th;  // line thickness
    ImVec2 P(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
    // a point turned by angle a about the centre
    ImVec2 R(float x, float y, float a) const {
        float s = std::sin(a), k = std::cos(a);
        return P(x * k - y * s, x * s + y * k);
    }
    void Line(float x0, float y0, float x1, float y1) const { dl->AddLine(P(x0, y0), P(x1, y1), col, th); }
    void Ring(float x, float y, float rad) const { dl->AddCircle(P(x, y), rad * r, col, 32, th); }
    void Dot(float x, float y, float rad) const { dl->AddCircleFilled(P(x, y), rad * r, col, 24); }
    void Box(float x0, float y0, float x1, float y1, float round = 0) const {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), col, round * r);
    }
    // a cartridge standing up, its tip at top
    void Cartridge(float x, float y, float w, float len) const {
        float tip = w * 0.5f;
        dl->PathArcTo(P(x, y + tip), tip * r, kPi, 2 * kPi, 12);
        dl->PathLineTo(P(x + tip, y + len));
        dl->PathLineTo(P(x - tip, y + len));
        dl->PathFillConvex(col);
        dl->AddLine(P(x - tip, y + len * 0.42f), P(x + tip, y + len * 0.42f), IM_COL32(0, 0, 0, 120), th * 0.6f);
    }
    // an arrow along a line, its head at the end
    void Arrow(float x0, float y0, float x1, float y1, float head) const {
        Line(x0, y0, x1, y1);
        float a = std::atan2(y1 - y0, x1 - x0);
        for (float side : {-1.0f, 1.0f}) {
            float b = a + kPi + side * 0.55f;
            Line(x1, y1, x1 + std::cos(b) * head, y1 + std::sin(b) * head);
        }
    }
};

void DrawIcon(const Pen& p, Icon icon) {
    switch (icon) {
        case Icon::Bullet:
            p.Cartridge(0, -0.48f, 0.34f, 0.96f);
            break;
        case Icon::Scope:  // the lock on's reticle
            p.Ring(0, 0, 0.42f);
            p.Line(-0.62f, 0, -0.16f, 0);
            p.Line(0.16f, 0, 0.62f, 0);
            p.Line(0, -0.62f, 0, -0.16f);
            p.Line(0, 0.16f, 0, 0.62f);
            p.Dot(0, 0, 0.05f);
            break;
        case Icon::Action:  // a figure on the move: jump, climb, vault
            p.Dot(0.06f, -0.5f, 0.13f);
            p.Line(0.02f, -0.34f, -0.06f, 0.02f);
            p.Line(0.02f, -0.26f, 0.34f, -0.42f);
            p.Line(0.02f, -0.26f, -0.3f, -0.1f);
            p.Line(-0.06f, 0.02f, 0.26f, 0.26f);
            p.Line(-0.06f, 0.02f, -0.24f, 0.3f);
            p.Line(-0.24f, 0.3f, -0.4f, 0.52f);
            break;
        case Icon::Focus:  // the sense's rings closing in on their mark
            p.Ring(0, 0, 0.48f);
            p.Ring(0, 0, 0.28f);
            p.Dot(0, 0, 0.09f);
            break;
        case Icon::Cover:  // a figure held against a wall
            p.Line(0.3f, -0.58f, 0.3f, 0.58f);
            p.Line(0.3f, -0.58f, 0.52f, -0.58f);
            p.Line(0.3f, 0.58f, 0.52f, 0.58f);
            p.Dot(-0.16f, -0.36f, 0.11f);
            p.Line(-0.14f, -0.2f, -0.26f, 0.1f);
            p.Line(-0.26f, 0.1f, 0.0f, 0.2f);
            p.Line(-0.26f, 0.1f, -0.34f, 0.5f);
            p.Line(-0.26f, 0.1f, -0.02f, 0.52f);
            break;
        case Icon::Roll:  // a figure tucked into a roll, over the ground
            p.Dot(0.05f, -0.05f, 0.13f);
            p.dl->PathArcTo(p.c, 0.34f * p.r, 0.75f * kPi, 2.25f * kPi, 24);
            p.dl->PathStroke(p.col, 0, p.th);
            p.Line(-0.5f, 0.52f, 0.5f, 0.52f);
            break;
        case Icon::Reload: {  // the magazine round again: an open ring with its arrowhead
            p.dl->PathArcTo(p.c, 0.42f * p.r, 0.22f * kPi, 1.95f * kPi, 32);
            p.dl->PathStroke(p.col, 0, p.th);
            float a = 1.95f * kPi;
            float tx = std::cos(a) * 0.42f, ty = std::sin(a) * 0.42f;
            p.Line(tx, ty, tx + 0.2f, ty - 0.02f);
            p.Line(tx, ty, tx - 0.02f, ty - 0.2f);
            p.Cartridge(0, -0.1f, 0.18f, 0.36f);
            break;
        }
        case Icon::WeaponLeft:
        case Icon::WeaponRight: {  // the round, and the way the pick goes
            float s = icon == Icon::WeaponLeft ? -1.0f : 1.0f;
            p.Cartridge(0, -0.52f, 0.3f, 0.62f);
            p.Arrow(-0.34f * s, 0.5f, 0.34f * s, 0.5f, 0.2f);
            break;
        }
        case Icon::Crouch:  // the stick pressed down
            p.Ring(0, 0.16f, 0.4f);
            p.Dot(0, 0.16f, 0.15f);
            p.Arrow(0, -0.52f, 0, -0.1f, 0.17f);
            break;
        case Icon::Case:  // the briefcase of the inventory
            p.Box(-0.42f, -0.14f, 0.42f, 0.46f, 0.05f);
            p.dl->PathArcTo(p.P(0.0f, -0.14f), 0.2f * p.r, kPi, 2 * kPi, 12);
            p.dl->PathStroke(p.col, 0, p.th);
            p.dl->AddLine(p.P(-0.42f, 0.06f), p.P(0.42f, 0.06f), IM_COL32(0, 0, 0, 120), p.th * 0.6f);
            break;
        case Icon::Start:  // the pause bars
            p.Box(-0.3f, -0.4f, -0.08f, 0.4f, 0.04f);
            p.Box(0.08f, -0.4f, 0.3f, 0.4f, 0.04f);
            break;
        case Icon::Gear:
            for (int i = 0; i < 8; i++) {
                float a = i * kPi / 4;
                p.dl->AddLine(p.R(0.3f, 0, a), p.R(0.52f, 0, a), p.col, p.th * 2.4f);
            }
            p.Dot(0, 0, 0.36f);
            p.dl->AddCircleFilled(p.c, 0.15f * p.r, IM_COL32(0, 0, 0, 200), 20);
            break;
        case Icon::Back:
            p.Line(0.14f, -0.36f, -0.22f, 0);
            p.Line(-0.22f, 0, 0.14f, 0.36f);
            break;
        case Icon::Accel:  // two chevrons up, the gas
            p.Line(-0.34f, 0.08f, 0, -0.3f);
            p.Line(0, -0.3f, 0.34f, 0.08f);
            p.Line(-0.34f, 0.46f, 0, 0.08f);
            p.Line(0, 0.08f, 0.34f, 0.46f);
            break;
        case Icon::Brake:  // two chevrons down, the brake
            p.Line(-0.34f, -0.08f, 0, 0.3f);
            p.Line(0, 0.3f, 0.34f, -0.08f);
            p.Line(-0.34f, -0.46f, 0, -0.08f);
            p.Line(0, -0.08f, 0.34f, -0.46f);
            break;
        case Icon::Handbrake: {  // P, the parking brake
            const char* text = "P";
            float size = p.r * 1.2f;
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
            p.dl->AddText(ImGui::GetFont(), size, ImVec2(p.c.x - ts.x / 2, p.c.y - ts.y / 2), p.col, text);
            break;
        }
        case Icon::Slasher:  // a tyre and the blade across it
            p.Ring(0, 0, 0.44f);
            p.Ring(0, 0, 0.19f);
            p.Line(-0.42f, 0.42f, 0.42f, -0.42f);
            p.Line(0.18f, -0.5f, 0.5f, -0.18f);
            break;
        case Icon::FlyUp:  // an arrow up on the thrust
            p.Arrow(0, 0.34f, 0, -0.44f, 0.22f);
            p.Line(-0.34f, 0.52f, -0.2f, 0.3f);
            p.Line(0.34f, 0.52f, 0.2f, 0.3f);
            p.Line(0, 0.58f, 0, 0.34f);
            break;
        case Icon::FlyDown:  // an arrow down, the thrust still lifting
            p.Arrow(0, -0.34f, 0, 0.44f, 0.22f);
            p.Line(-0.34f, -0.52f, -0.2f, -0.3f);
            p.Line(0.34f, -0.52f, 0.2f, -0.3f);
            p.Line(0, -0.58f, 0, -0.34f);
            break;
        case Icon::Land:  // an arrow down onto the ground
            p.Arrow(0, -0.5f, 0, 0.26f, 0.22f);
            p.Line(-0.44f, 0.5f, 0.44f, 0.5f);
            p.Line(-0.2f, 0.5f, -0.3f, 0.36f);
            p.Line(0.2f, 0.5f, 0.3f, 0.36f);
            break;
    }
}

// a colour at the layout's opacity
ImU32 Fade(int r, int g, int b, int a) { return IM_COL32(r, g, b, int(float(a) * g_opacity)); }

// a control's face: dark glass, a gold ring, white line art and the console control's badge; a trigger drawn wide
void DrawFace(ImDrawList* dl, ImVec2 c, float r, const Button& b, bool on, int alpha, float th) {
    ImU32 glass = Fade(8, 10, 12, on ? 150 : 115);
    ImU32 ring = on ? Fade(245, 205, 110, alpha) : Fade(198, 166, 90, alpha * 3 / 4);
    ImU32 ink = Fade(255, 252, 240, alpha);
    if (b.pill) {
        float w = r * 1.9f, h = r * 1.42f;
        ImVec2 p0(c.x - w / 2, c.y - h / 2), p1(c.x + w / 2, c.y + h / 2);
        dl->AddRectFilled(p0, p1, glass, h * 0.5f);
        dl->AddRect(p0, p1, ring, h * 0.5f, 0, th * (on ? 1.7f : 1.1f));
    } else {
        dl->AddCircleFilled(c, r, glass, 56);
        dl->AddCircle(c, r, ring, 56, th * (on ? 1.7f : 1.1f));
        dl->AddCircle(c, r * 0.87f, Fade(255, 255, 255, alpha / 6), 48, th * 0.5f);
    }
    ImVec2 at = b.pill ? ImVec2(c.x - r * 0.4f, c.y) : ImVec2(c.x, c.y - r * 0.12f);
    float ir = b.pill ? r * 0.5f : r * 0.6f;
    DrawIcon({dl, at, ir, ink, th * 1.1f}, b.icon);
    if (!b.badge[0]) return;
    // the console control it stands in for, in a small gold framed tag
    float size = b.pill ? r * 0.52f : r * 0.44f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, b.badge);
    ImVec2 bc = b.pill ? ImVec2(c.x + r * 0.5f, c.y) : ImVec2(c.x, c.y + r * 0.58f);
    ImVec2 p0(bc.x - ts.x / 2 - size * 0.22f, bc.y - ts.y / 2 - size * 0.14f);
    ImVec2 p1(bc.x + ts.x / 2 + size * 0.22f, bc.y + ts.y / 2 + size * 0.14f);
    dl->AddRectFilled(p0, p1, Fade(0, 0, 0, 190), size * 0.4f);
    dl->AddRect(p0, p1, Fade(214, 178, 98, alpha), size * 0.4f, 0, th * 0.5f);
    dl->AddText(ImGui::GetFont(), size, ImVec2(p0.x + size * 0.22f, p0.y + size * 0.14f), ink, b.badge);
}

void DrawStick(ImDrawList* dl, float th) {
    // at rest in its place, else where the thumb came down (its place too, when fixed)
    float r = Radius(kStick);
    ImVec2 at(g_layout[kStick].x * Width(), g_layout[kStick].y * Height());
    bool held = false;
    for (const Finger& f : g_fingers)
        if (f.role == Role::Stick) {
            at = ImVec2(f.x0, f.y0);
            held = true;
        }
    dl->AddCircleFilled(at, r, Fade(8, 10, 12, held ? 120 : 90), 64);
    dl->AddCircle(at, r, Fade(198, 166, 90, held ? 200 : 150), 64, th * 0.9f);
    for (int i = 0; i < 4; i++) {  // the four directions
        float a = i * kPi / 2;
        ImVec2 tip(at.x + std::cos(a) * r * 0.86f, at.y + std::sin(a) * r * 0.86f);
        ImVec2 l(at.x + std::cos(a + 0.12f) * r * 0.74f, at.y + std::sin(a + 0.12f) * r * 0.74f);
        ImVec2 rr(at.x + std::cos(a - 0.12f) * r * 0.74f, at.y + std::sin(a - 0.12f) * r * 0.74f);
        dl->AddTriangleFilled(tip, l, rr, Fade(214, 178, 98, held ? 150 : 105));
    }
    ImVec2 knob(at.x + input::g_stick_x * r, at.y - input::g_stick_y * r);
    dl->AddCircleFilled(knob, r * 0.42f, Fade(235, 228, 205, held ? 150 : 105), 48);
    dl->AddCircle(knob, r * 0.42f, Fade(255, 252, 240, held ? 210 : 160), 48, th * 0.7f);
}

// a text centred on a point, its height a share of the screen's
void CentredText(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const char* text) {
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
    dl->AddText(ImGui::GetFont(), size, ImVec2(at.x - ts.x / 2, at.y - ts.y / 2), colour, text);
}

// the editor's bar, drawn as the controls are: Reset, Cancel, Done, and what to do under it
void DrawEditorBar(ImDrawList* dl, float th) {
    float h = Height();
    for (int i = 0; i < kBarCount; i++) {
        float r[4];
        BarAt(0, 0, r, i);
        bool done = i == kBarDone;
        bool size = i == kBarSmaller || i == kBarBigger;
        // - and + wait for a control to be picked, hide for one that can be
        int alpha = (size && g_selected < 0) || (i == kBarHide && !Hideable(g_selected)) ? 90 : 240;
        dl->AddRectFilled(ImVec2(r[0], r[1]), ImVec2(r[2], r[3]),
                          done ? IM_COL32(150, 115, 45, 235) : IM_COL32(20, 18, 14, 225), h * 0.015f);
        dl->AddRect(ImVec2(r[0], r[1]), ImVec2(r[2], r[3]), IM_COL32(220, 180, 100, alpha), h * 0.015f, 0, th * 0.7f);
        CentredText(dl, ImVec2((r[0] + r[2]) / 2, (r[1] + r[3]) / 2), h * (size ? 0.06f : 0.042f),
                    IM_COL32(255, 255, 255, alpha), app::Ui(BarLabel(i)).c_str());
    }
    CentredText(dl, ImVec2(Width() / 2, (kBarY + kBarH + 0.05f) * h), h * 0.036f, IM_COL32(255, 255, 255, 220),
                app::Ui("DRAG TO MOVE, PINCH OR - + TO RESIZE").c_str());
    CentredText(dl, ImVec2(Width() / 2, (kBarY + kBarH + 0.1f) * h), h * 0.036f, IM_COL32(255, 255, 255, 220),
                app::Ui("TAP A CONTROL, THEN HIDE OR SHOW").c_str());
}

}  // namespace

void Draw() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    bool editing = g_editing;
    if (!editing && !Shown()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float h = Height();
    float th = std::max(2.0f, h / 360.0f);
    if (editing) dl->AddRectFilled(ImVec2(0, 0), ImVec2(Width(), h), IM_COL32(0, 0, 0, 90));
    for (size_t i = 0; i < kButtonCount; i++) {
        const Button& b = kButtons[i];
        if (!ButtonShown(i) || (app::MenuOpen() && b.press != kPanel && !editing)) continue;
        float cx, cy;
        Centre(i, cx, cy);
        float r = Radius(i);
        bool hidden = g_layout[i].hidden;
        bool on = !editing && g_pressed[i];
        int alpha = hidden ? 40 : (on ? 255 : 205);  // a hidden control shows faint in the editor
        DrawFace(dl, ImVec2(cx, cy), r, b, on, alpha, th);
        if (editing && int(i) == g_selected) dl->AddCircle(ImVec2(cx, cy), r + th * 2, IM_COL32(255, 200, 60, 255), 48, th * 1.5f);
        if (editing && hidden) {
            float k = r * 0.6f;
            dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), IM_COL32(255, 80, 60, 220), th);
            dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), IM_COL32(255, 80, 60, 220), th);
        }
    }
    if (editing || (!InMenus() && !app::MenuOpen() && input::Play())) DrawStick(dl, th);
    if (editing) {
        if (g_selected == int(kStick)) {
            ImVec2 at(g_layout[kStick].x * Width(), g_layout[kStick].y * h);
            dl->AddCircle(at, Radius(kStick) + th * 2, IM_COL32(255, 200, 60, 255), 64, th * 1.5f);
        }
        DrawEditorBar(dl, th);
    }
}

// ---- the layout's settings, for the game's options (Recomp Settings, TOUCH); game thread, each saved at once

namespace {

// the controls moved, resized or hidden away from the preset's places
bool Custom() {
    const Layout& preset = *kPresets[g_preset].places;
    for (size_t i = 0; i <= kButtonCount; i++) {
        const Place& a = g_layout[i];
        const Place& b = preset[i];
        if (std::fabs(a.x - b.x) > 1e-4f || std::fabs(a.y - b.y) > 1e-4f || std::fabs(a.r - b.r) > 1e-4f ||
            a.hidden != b.hidden)
            return true;
    }
    return false;
}

uint32_t Percent(float f) { return uint32_t(std::lround(f * 100.0f)); }

}  // namespace

const char* LayoutName() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Custom() ? "CUSTOM" : kPresets[g_preset].label;
}

// the next or previous preset; from a custom layout first back to its own preset's places, or the one before
void StepLayout(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    size_t n = std::size(kPresets);
    size_t i = Custom() ? (step > 0 ? g_preset : (g_preset + n - 1) % n) : (g_preset + n + (step > 0 ? 1 : n - 1)) % n;
    ApplyPreset(i);
    SaveLayout();
}

uint32_t ControlsOpacity() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Percent(g_opacity);
}

// 20% to 100% in steps of 5
void StepOpacity(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    g_opacity = float(std::clamp(int(Percent(g_opacity)) + step * 5, 20, 100)) / 100.0f;
    SaveLayout();
}

uint32_t ControlsSize() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Percent(g_scale);
}

// 50% to 150% in steps of 5
void StepSize(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    g_scale = float(std::clamp(int(Percent(g_scale)) + step * 5, 50, 150)) / 100.0f;
    SaveLayout();
}

const char* StickModeName() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return g_fixed_stick ? "FIXED" : "FLOATING";
}

void StepStickMode(int) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    g_fixed_stick ^= true;
    SaveLayout();
}

// the controls over the screen to drag where they go, Done or Cancel in the bar at the top, Android's back cancels
void EditLayout() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    // the touch that picked the row is the menu's, not the editor's
    g_fingers.clear();
    g_cursor_down = false;
    Recount();
    OpenEditor();
}

void AddLook(float dx, float dy) {
    // the fraction of a count carried to the next reading: slow turns add up
    static float rx = 0, ry = 0;
    rx += dx;
    ry += dy;
    int ix = int(rx), iy = int(ry);
    rx -= float(ix);
    ry -= float(iy);
    input::g_dx += ix;
    input::g_dy += iy;
}

void PadUsed() { input::g_touch = false; }

bool LookActive() { return !g_editing && input::Active() && input::Play() && !InMenus(); }

bool Firing() {
    {
        std::lock_guard lock(g_lock);
        for (size_t i = 0; i < kButtonCount; i++)
            if (g_pressed[i] && kButtons[i].press == kByAction && kButtons[i].action == kFireTouch) return true;
    }
    if (input::Down(VK_LBUTTON)) return true;

    constexpr int kTriggerThreshold = 4096;
    int n = 0;
    bool firing = false;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
        for (int i = 0; i < n; i++) {
            SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
            if (pad &&
                SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >
                    kTriggerThreshold) {
                firing = true;
                break;
            }
        }
        SDL_free(ids);
    }
    return firing;
}

// main thread, each turn of the event loop: what the input asks of the window
void Pump() {
    // a mouse in play is captured as on a PC: no pointer over the picture and no screen edge to stop a turn. Let go
    // in the menus, the panel, the background and once the touch screen is used; asked again whenever Android took
    // it away
    SDL_Window* window = host::Window();
    bool capture = input::g_keyboard && !input::g_touch && LookActive();
    if (window && capture != SDL_GetWindowRelativeMouseMode(window)) SDL_SetWindowRelativeMouseMode(window, capture);
}

// the touch control that presses a pad button of its own, its label, uppercase, as the prompts name it
std::string ButtonName(int button) {
    std::lock_guard lock(g_lock);
    for (size_t i = 0; i < kButtonCount; i++) {
        const Button& b = kButtons[i];
        if (b.press != kPad || b.pad != button || !ButtonShown(i)) continue;
        std::string name = b.label;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return {};
}

}  // namespace touch

namespace input {

bool CursorShown() { return g_menu && (g_keyboard || touch::g_cursor_touch); }

std::optional<Pointer> Cursor() {
    if (!Active() || !CursorShown()) return std::nullopt;
    int w, h;
    host::WindowPixels(w, h);
    if (w <= 0 || h <= 0) return std::nullopt;
    float px, py;
    bool touch_down;
    {
        std::lock_guard lock(touch::g_lock);
        touch::MenuHolds();
        px = touch::g_cursor_x;
        py = touch::g_cursor_y;
        touch_down = touch::g_cursor_touch && (touch::g_cursor_down || NowMs() < touch::g_cursor_tap_until);
    }
    // the frame is shown in the middle of the window
    float aspect = game::Aspect();
    float vw = std::min(float(w), float(h) * aspect);
    float vh = vw / aspect;
    float x = (px - (float(w) - vw) / 2) / vw;
    float y = (py - (float(h) - vh) / 2) / vh;
    return Pointer{x, y, touch_down || Down(VK_LBUTTON), Down(VK_RBUTTON)};
}

bool LeftButton() {
    if (!Active() || g_settle) return false;
    if (Down(VK_LBUTTON)) return true;
    // a finger not on a button drags: its touch is the cursor's held left button
    std::lock_guard lock(touch::g_lock);
    return std::any_of(touch::g_fingers.begin(), touch::g_fingers.end(),
                       [](const touch::Finger& f) { return f.role != touch::Role::Button; });
}

// the touch control that presses the action's pad button: its label, uppercase, as the prompts name it
std::string TouchLabel(size_t action) {
    std::lock_guard lock(touch::g_lock);
    for (size_t i = 0; i < touch::kButtonCount; i++) {
        const touch::Button& b = touch::kButtons[i];
        if (b.press != touch::kByAction || b.action != action || !touch::ButtonShown(i)) continue;
        std::string name = b.label;
        if (size_t paren = name.find(" ("); paren != std::string::npos) name.resize(paren);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return {};
}

// the first key bound to a pad button as the panel names it, empty for none: 0 to 7 A B X Y black white and the
// triggers, 8 to 15 dpad up down left right, start, back, the thumbs. The touch controls used last, their control's
// own name, the way the prompts read on the phone
std::string ButtonKey(int button) {
    if (g_touch) {
        if (std::string name = touch::ButtonName(button); !name.empty()) return name;
        for (size_t i = 0; i < kCount; i++) {
            if (PromptButton(kEntries[i]) != button) continue;
            std::string label = TouchLabel(i);
            if (!label.empty()) return label;
        }
    }
    // the section's own key first, then the on foot one
    std::lock_guard lock(g_bind_lock);
    for (Section section : {KeySection(g_section), Section::Foot}) {
        for (size_t i = 0; i < kCount; i++) {
            const Entry& entry = kEntries[i];
            bool shown = entry.section == section && PromptButton(entry) == button;
            if (shown && !g_binds[i].empty()) return NameOf(g_binds[i].front());
        }
    }
    return {};
}

}  // namespace input
