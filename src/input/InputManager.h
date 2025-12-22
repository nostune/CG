#pragma once
#include <Windows.h>
#include <DirectXMath.h>
#include "../gameplay/components/PlayerInputComponent.h"

namespace outer_wilds {

class InputManager {
public:
    static InputManager& GetInstance();

    void Initialize(HWND hwnd);
    void Update();

    // Input state
    PlayerInputComponent& GetPlayerInput() { return m_PlayerInput; }

    // Mouse control
    void SetMouseCapture(bool capture);
    bool IsMouseCaptured() const { return m_MouseCaptured; }
    void SetMouseLookEnabled(bool enabled);
    bool IsMouseLookEnabled() const { return m_MouseLookEnabled; }

    // Key state queries
    bool IsKeyPressed(int key) const;
    bool IsKeyHeld(int key) const;
    bool IsKeyReleased(int key) const;
    
    // Mouse delta for spacecraft/free camera control
    void GetMouseDelta(int& deltaX, int& deltaY) const {
        deltaX = m_CurrentMousePos.x - m_PreviousMousePos.x;
        deltaY = m_CurrentMousePos.y - m_PreviousMousePos.y;
    }

private:
    InputManager() = default;
    ~InputManager() = default;

    void UpdateKeyboardState();
    void UpdateMouseState();
    void UpdatePlayerInput();

    // Window handle
    HWND m_Hwnd = nullptr;

    // Input state
    PlayerInputComponent m_PlayerInput;

    // Keyboard state
    BYTE m_CurrentKeyState[256];
    BYTE m_PreviousKeyState[256];

    // Mouse state
    POINT m_CurrentMousePos;
    POINT m_PreviousMousePos;
    POINT m_LastMousePos;
    bool m_MouseCaptured = false;
    bool m_MouseLookEnabled = false;
    bool m_FirstMouseCapture = true;

    // Movement keys
    static constexpr int KEY_W = 'W';
    static constexpr int KEY_A = 'A';
    static constexpr int KEY_S = 'S';
    static constexpr int KEY_D = 'D';
    static constexpr int KEY_SPACE = VK_SPACE;
    static constexpr int KEY_SHIFT = VK_SHIFT;
    static constexpr int KEY_ESCAPE = VK_ESCAPE;
};

} // namespace outer_wilds