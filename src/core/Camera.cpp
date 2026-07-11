#include "Camera.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 up, float fovDegrees, float aspectRatio, float nearZ, float farZ)
    : m_position(position), m_up(up)
{
    m_forward = glm::normalize(target - position);
    m_projection = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearZ, farZ);
    // GLM's clip space Y is flipped relative to Vulkan's — this is the standard fix
    m_projection[1][1] *= -1;
}

glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(m_position, m_position + m_forward, m_up);
}

void Camera::Update(float deltaTime)
{
    float velocity = m_moveSpeed * deltaTime;
    glm::vec3 right = glm::normalize(glm::cross(m_forward, m_up));

    if (m_moveForward)  m_position += m_forward * velocity;
    if (m_moveBackward) m_position -= m_forward * velocity;
    if (m_moveLeft)     m_position -= right * velocity;
    if (m_moveRight)    m_position += right * velocity;
}

void Camera::OnKey(int key, int action)
{
    bool pressed = (action != GLFW_RELEASE);

    switch (key) {
    case GLFW_KEY_W: m_moveForward = pressed; break;
    case GLFW_KEY_S: m_moveBackward = pressed; break;
    case GLFW_KEY_A: m_moveLeft = pressed; break;
    case GLFW_KEY_D: m_moveRight = pressed; break;
    }
}

void Camera::OnMouseButton(int button, int action)
{
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        m_rightMouseDown = (action == GLFW_PRESS);
        m_firstMouse = true; // avoid a jump when re-engaging look
    }
}

void Camera::OnMouseMove(double x, double y)
{
    if (!m_rightMouseDown) return;

    if (m_firstMouse) {
        m_lastMouseX = static_cast<float>(x);
        m_lastMouseY = static_cast<float>(y);
        m_firstMouse = false;
    }

    float xOffset = (static_cast<float>(x) - m_lastMouseX) * m_mouseSensitivity;
    float yOffset = (m_lastMouseY - static_cast<float>(y)) * m_mouseSensitivity; // reversed: Y grows downward on screen

    m_lastMouseX = static_cast<float>(x);
    m_lastMouseY = static_cast<float>(y);

    m_yaw += xOffset;
    m_pitch += yOffset;
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f); // avoid gimbal flip at the poles

    glm::vec3 direction;
    direction.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    direction.y = sin(glm::radians(m_pitch));
    direction.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_forward = glm::normalize(direction);
}