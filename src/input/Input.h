#pragma once
// BrickDrop Input — SDL gamepad + keyboard, logic port từ RomCloud InputManager
// (Nintendo layout: Right=A, Bottom=B; chin buttons MENU-SELECT-START).
#include <SDL2/SDL.h>
#include <cstdint>

namespace BrickDrop {

enum class Button { UP, DOWN, LEFT, RIGHT, A, B, X, Y, L1, R1, START, SELECT, MENU, COUNT };

class Input {
public:
  static Input &instance();
  bool init();
  void shutdown();
  // Gọi mỗi frame: poll SDL events, trả false khi user thoát (MENU giữ / QUIT).
  bool poll();
  bool justPressed(Button b);
  bool held(Button b) const;

private:
  Input() = default;
  SDL_GameController *m_ctrl = nullptr;
  SDL_Joystick *m_joy = nullptr;
  bool m_cur[(int)Button::COUNT] = {false};
  bool m_just[(int)Button::COUNT] = {false};
  bool m_dpad[4] = {false}; // U D L R
  bool m_key[4] = {false};
  bool m_quit = false;
  void set(Button b, bool down);
  void mapKey(SDL_Keycode k, bool down);
};

} // namespace BrickDrop
