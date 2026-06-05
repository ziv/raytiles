#pragma once
#include <raylib.h>

inline void rebase_large_world(Camera &camera, Vector3 &world_offset, const float rebase_threshold) {
    // Large-world rebase: keep the user-space camera close to the origin.
    // Whenever |camera.x| or |camera.z| exceeds the threshold, slide BOTH
    // the camera (position + target) AND the world_offset by the same
    // amount, preserving the absolute camera location (user - offset).
    // In a real game you must apply the same shift to every entity that
    // lives in user space (models, lights, particles, ...).
    if (camera.position.x > rebase_threshold) {
        camera.position.x -= rebase_threshold;
        camera.target.x -= rebase_threshold;
        world_offset.x -= rebase_threshold;
    } else if (camera.position.x < -rebase_threshold) {
        camera.position.x += rebase_threshold;
        camera.target.x += rebase_threshold;
        world_offset.x += rebase_threshold;
    }
    if (camera.position.z > rebase_threshold) {
        camera.position.z -= rebase_threshold;
        camera.target.z -= rebase_threshold;
        world_offset.z -= rebase_threshold;
    } else if (camera.position.z < -rebase_threshold) {
        camera.position.z += rebase_threshold;
        camera.target.z += rebase_threshold;
        world_offset.z += rebase_threshold;
    }
}
