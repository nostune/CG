#include "InputManager.h"
#include "../core/DebugManager.h"
#include <iostream>

namespace outer_wilds {

InputManager& InputManager::GetInstance() {
    static InputManager instance;
    return instance;
}

void InputManager::Initialize(HWND hwnd) {
    m_Hwnd = hwnd;
    memset(m_CurrentKeyState, 0, sizeof(m_CurrentKeyState));
    memset(m_PreviousKeyState, 0, sizeof(m_PreviousKeyState));

    // Get initial mouse position
    GetCursorPos(&m_CurrentMousePos);
    ScreenToClient(m_Hwnd, &m_CurrentMousePos);
    m_PreviousMousePos = m_CurrentMousePos;
    m_LastMousePos = m_CurrentMousePos;
    
    // Enable mouse look by default
    m_MouseLookEnabled = true;
    m_PlayerInput.mouseLookEnabled = true;
    SetMouseCapture(true);

    
}

void InputManager::Update() {
    // Update previous states
    memcpy(m_PreviousKeyState, m_CurrentKeyState, sizeof(m_CurrentKeyState));
    m_PreviousMousePos = m_CurrentMousePos;

    // Update current states
    UpdateKeyboardState();
    UpdateMouseState();

    // Update player input
    UpdatePlayerInput();
}

void InputManager::UpdateKeyboardState() {
    GetKeyboardState(m_CurrentKeyState);
}

void InputManager::UpdateMouseState() {
    GetCursorPos(&m_CurrentMousePos);
    ScreenToClient(m_Hwnd, &m_CurrentMousePos);
}

void InputManager::UpdatePlayerInput() {
    // Movement input (WASD)
    m_PlayerInput.moveInput.x = 0.0f;
    m_PlayerInput.moveInput.y = 0.0f;

    if (IsKeyHeld(KEY_W)) m_PlayerInput.moveInput.y += 1.0f;
    if (IsKeyHeld(KEY_S)) m_PlayerInput.moveInput.y -= 1.0f;
    if (IsKeyHeld(KEY_A)) m_PlayerInput.moveInput.x -= 1.0f;
    if (IsKeyHeld(KEY_D)) m_PlayerInput.moveInput.x += 1.0f;

    // Normalize movement input
    if (m_PlayerInput.moveInput.x != 0.0f || m_PlayerInput.moveInput.y != 0.0f) {
        DirectX::XMVECTOR moveVec = DirectX::XMLoadFloat2(&m_PlayerInput.moveInput);
        moveVec = DirectX::XMVector2Normalize(moveVec);
        DirectX::XMStoreFloat2(&m_PlayerInput.moveInput, moveVec);
    }

    // Look input (mouse movement) - calculate delta from window center for infinite rotation
    if (m_MouseLookEnabled) {
        RECT clientRect;
        GetClientRect(m_Hwnd, &clientRect);
        int centerX = (clientRect.left + clientRect.right) / 2;
        int centerY = (clientRect.top + clientRect.bottom) / 2;

        // Calculate delta from center
        float mouseDeltaX = (float)(m_CurrentMousePos.x - centerX);
        float mouseDeltaY = (float)(m_CurrentMousePos.y - centerY);

        m_PlayerInput.lookInput.x = mouseDeltaX;
        m_PlayerInput.lookInput.y = mouseDeltaY;

        // Reset mouse to center for next frame (allows infinite rotation)
        POINT centerPosScreen = { centerX, centerY };
        ClientToScreen(m_Hwnd, &centerPosScreen);
        SetCursorPos(centerPosScreen.x, centerPosScreen.y);
    } else {
        m_PlayerInput.lookInput.x = 0.0f;
        m_PlayerInput.lookInput.y = 0.0f;
    }
    
    // Update mouse look enabled state in player input
    m_PlayerInput.mouseLookEnabled = m_MouseLookEnabled;

    // Action buttons
    m_PlayerInput.jumpPressed = IsKeyPressed(KEY_SPACE);
    m_PlayerInput.jumpHeld = IsKeyHeld(KEY_SPACE);
    m_PlayerInput.sprintHeld = IsKeyHeld(KEY_SHIFT);
    m_PlayerInput.crouchHeld = IsKeyHeld(VK_CONTROL);

    // ESC to toggle mouse look
    m_PlayerInput.toggleMouseLookPressed = IsKeyPressed(KEY_ESCAPE);
}

void InputManager::SetMouseLookEnabled(bool enabled) {
    m_MouseLookEnabled = enabled;
    SetMouseCapture(enabled);
}

void InputManager::SetMouseCapture(bool capture) {
    if (capture && !m_MouseCaptured) {
        // Capture mouse and hide cursor
        SetCapture(m_Hwnd);
        ShowCursor(FALSE);
        
        // Center mouse initially
        RECT clientRect;
        GetClientRect(m_Hwnd, &clientRect);
        int centerX = (clientRect.left + clientRect.right) / 2;
        int centerY = (clientRect.top + clientRect.bottom) / 2;
        
        POINT centerPosScreen = { centerX, centerY };
        ClientToScreen(m_Hwnd, &centerPosScreen);
        SetCursorPos(centerPosScreen.x, centerPosScreen.y);
        
        m_MouseCaptured = true;
    } else if (!capture && m_MouseCaptured) {
        // Release mouse and show cursor
        ReleaseCapture();
        ShowCursor(TRUE);
        m_MouseCaptured = false;
    }
}

bool InputManager::IsKeyPressed(int key) const {
    return (m_CurrentKeyState[key] & 0x80) && !(m_PreviousKeyState[key] & 0x80);
}

bool InputManager::IsKeyHeld(int key) const {
    return (m_CurrentKeyState[key] & 0x80);
}

bool InputManager::IsKeyReleased(int key) const {
    return !(m_CurrentKeyState[key] & 0x80) && (m_PreviousKeyState[key] & 0x80);
}

} // namespace outer_wilds