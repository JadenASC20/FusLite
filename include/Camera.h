#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct GLFWwindow;
class Camera
{
public:
    Camera(glm::vec3 position, glm::vec3 target, glm::vec3 up, float fovDegrees, float aspectRatio, float nearZ, float farZ);
    void Update(float deltaTime);
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const
    {
        // Sub-pixel jitter lives in the third column, which offsets clip-space
        // xy by a constant NDC amount regardless of depth.
        glm::mat4 jittered = m_projection;
        jittered[2][0] += m_jitterX;
        jittered[2][1] += m_jitterY;
        return jittered;
    }

    glm::mat4 GetProjectionMatrixNoJitter() const { return m_projection; }
    void SetJitter(float x, float y) { m_jitterX = x; m_jitterY = y; }

    void OnKey(int key, int action);
    void OnMouseMove(double x, double y);
    void OnMouseButton(int button, int action);
    glm::vec3 GetPosition() const { return m_position; }
    glm::mat4 GetViewMatrixNoTranslate() const
    {
        return glm::mat4(glm::mat3(GetViewMatrix()));
    }

private:
    glm::vec3 m_position;
    glm::vec3 m_forward;
    glm::vec3 m_up;
    glm::mat4 m_projection;
    float m_yaw = -90.0f;
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
    float m_jitterX = 0.0f;
    float m_jitterY = 0.0f;
};