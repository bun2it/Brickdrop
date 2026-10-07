#include "Input.h"

namespace BrickDrop {

Input &Input::instance() {
  static Input inst;
  return inst;
}

bool Input::init() {
  int n = SDL_NumJoysticks();
  for (int i = 0; i < n; ++i) {
    if (SDL_IsGameController(i)) {
      m_ctrl = SDL_GameControllerOpen(i);
      if (m_ctrl)
        break;
    }
  }
  if (!m_ctrl && n > 0)
    m_joy = SDL_JoystickOpen(0);
  return true;
}

void Input::shutdown() {
  if (m_ctrl) {
    SDL_GameControllerClose(m_ctrl);
    m_ctrl = nullptr;
  }
  if (m_joy) {
    SDL_JoystickClose(m_joy);
    m_joy = nullptr;
  }
}

void Input::set(Button b, bool down) {
  int i = (int)b;
  if (down && !m_cur[i])
    m_just[i] = true;
  m_cur[i] = down;
}

void Input::mapKey(SDL_Keycode k, bool down) {
  Button b = Button::COUNT;
  switch (k) {
  case SDLK_UP:
    m_key[0] = down;
    return;
  case SDLK_DOWN:
    m_key[1] = down;
    return;
  case SDLK_LEFT:
    m_key[2] = down;
    return;
  case SDLK_RIGHT:
    m_key[3] = down;
    return;
  case SDLK_RETURN:
  case SDLK_SPACE:
    b = Button::A;
    break;
  case SDLK_ESCAPE:
  case SDLK_BACKSPACE:
    b = Button::B;
    break;
  case SDLK_x:
    b = Button::X;
    break;
  case SDLK_y:
    b = Button::Y;
    break;
  case SDLK_TAB:
    b = Button::SELECT;
    break;
  case SDLK_HOME:
    b = Button::MENU;
    break;
  default:
    break;
  }
  if (b != Button::COUNT)
    set(b, down);
}

bool Input::poll() {
  for (int i = 0; i < (int)Button::COUNT; ++i)
    m_just[i] = false;
  m_quit = false;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
    case SDL_QUIT:
      m_quit = true;
      break;
    case SDL_KEYDOWN:
      mapKey(e.key.keysym.sym, true);
      break;
    case SDL_KEYUP:
      mapKey(e.key.keysym.sym, false);
      break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
      bool down = (e.type == SDL_CONTROLLERBUTTONDOWN);
      switch (e.cbutton.button) {
      case SDL_CONTROLLER_BUTTON_DPAD_UP:
        m_dpad[0] = down;
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        m_dpad[1] = down;
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        m_dpad[2] = down;
        break;
      case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        m_dpad[3] = down;
        break;
      case SDL_CONTROLLER_BUTTON_A:
        set(Button::B, down);
        break;
      case SDL_CONTROLLER_BUTTON_B:
        set(Button::A, down);
        break;
      case SDL_CONTROLLER_BUTTON_X:
        set(Button::Y, down);
        break;
      case SDL_CONTROLLER_BUTTON_Y:
        set(Button::X, down);
        break;
      case SDL_CONTROLLER_BUTTON_BACK:
        set(Button::SELECT, down);
        break;
      case SDL_CONTROLLER_BUTTON_GUIDE:
        set(Button::MENU, down);
        break;
      case SDL_CONTROLLER_BUTTON_START:
        set(Button::START, down);
        break;
      default:
        break;
      }
      break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
      if (m_ctrl)
        break;
      bool down = (e.type == SDL_JOYBUTTONDOWN);
      switch (e.jbutton.button) {
      case 0:
        set(Button::B, down);
        break;
      case 1:
        set(Button::A, down);
        break;
      case 2:
        set(Button::Y, down);
        break;
      case 3:
        set(Button::X, down);
        break;
      case 6:
        set(Button::SELECT, down);
        break;
      case 7:
        set(Button::START, down);
        break;
      case 8:
        set(Button::MENU, down);
        break;
      default:
        break;
      }
      break;
    }
    case SDL_JOYHATMOTION:
      m_dpad[0] = (e.jhat.value & SDL_HAT_UP);
      m_dpad[1] = (e.jhat.value & SDL_HAT_DOWN);
      m_dpad[2] = (e.jhat.value & SDL_HAT_LEFT);
      m_dpad[3] = (e.jhat.value & SDL_HAT_RIGHT);
      break;
    default:
      break;
    }
  }
  // Gộp dpad + phím thành UP/DOWN/LEFT/RIGHT edge
  bool u = m_dpad[0] || m_key[0], d = m_dpad[1] || m_key[1];
  bool l = m_dpad[2] || m_key[2], r = m_dpad[3] || m_key[3];
  auto edge = [&](Button b, bool v) {
    int i = (int)b;
    if (v && !m_cur[i])
      m_just[i] = true;
    m_cur[i] = v;
  };
  edge(Button::UP, u);
  edge(Button::DOWN, d);
  edge(Button::LEFT, l);
  edge(Button::RIGHT, r);
  if (m_quit)
    set(Button::MENU, true);
  return !m_quit;
}

bool Input::justPressed(Button b) { return m_just[(int)b]; }
bool Input::held(Button b) const { return m_cur[(int)b]; }

} // namespace BrickDrop
