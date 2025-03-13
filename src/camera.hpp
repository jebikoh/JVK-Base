#pragma once

#include <SDL_events.h>
#include <material.hpp>

struct Camera {
    glm::vec3 velocity;
    glm::vec3 position;

    float pitch = 0.0f;
    float yaw = 0.0f;
    float speed = 1.0f;

    glm::mat4 getViewMatrix() const;
    glm::mat4 getRotationMatrix() const;

    void processSDLEvent(SDL_Event &event);
    void update(float deltaTime = 0.0f);
};