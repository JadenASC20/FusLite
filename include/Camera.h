#pragma once
#include <glm/glm.hpp>

struct GLFWwindow;

class Camera
{
public:
    Camera(glm::vec3 position, glm::vec3 target, glm::vec3 up, float fovDegrees, float aspectRatio, float nearZ, float farZ);

    void Update(float deltaTime);

    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const { return m_projection; }

    // Called from GLFW callbacks in main.cpp
    void OnKey(int key, int action);
    void OnMouseMove(double x, double y);
    void OnMouseButton(int button, int action);
    glm::vec3 GetPosition() const { return m_position; }

private:
    glm::vec3 m_position;
    glm::vec3 m_forward;
    glm::vec3 m_up;

    glm::mat4 m_projection;

    float m_yaw = -90.0f;   // facing -Z initially
    float m_pitch = 0.0f;

    float m_lastMouseX = 0.0f;
    float m_lastMouseY = 0.0f;
    bool m_firstMouse = true;
    bool m_rightMouseDown = false;

    float m_moveSpeed = 3.0f;
    float m_mouseSensitivity = 0.1f;

    bool m_moveForward = false;
    bool m_moveBackward = false;
    bool m_moveLeft = false;
    bool m_moveRight = false;
};