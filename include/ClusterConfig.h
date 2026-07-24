#pragma once

#include <glm/glm.hpp>

// defining the cluster grid dimensions
constexpr int CLUSTER_GRID_X = 16;
constexpr int CLUSTER_GRID_Y = 9;
constexpr int CLUSTER_GRID_Z = 24;
constexpr int NUM_CLUSTERS = CLUSTER_GRID_X * CLUSTER_GRID_Y * CLUSTER_GRID_Z; // 3456

constexpr int MAX_LIGHTS_PER_CLUSTER = 128; // upper bound — one cluster could theoretically overlap all lights
constexpr int MAX_LIGHT_INDICES = NUM_CLUSTERS * 32; // generous shared pool — most clusters will only match a handful


struct Cluster
{
    glm::vec4 minBounds; // xyz = view-space min corner, w unused (padding)
    glm::vec4 maxBounds; // xyz = view-space max corner, w unused (padding)
};

struct ClusterLightInfo
{
    uint32_t offset; // where in the global light index list this cluster's lights start
    uint32_t count;  // how many lights matched this cluster
};