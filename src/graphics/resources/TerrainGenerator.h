#pragma once
#include "Mesh.h"
#include <DirectXMath.h>

namespace outer_wilds {
namespace resources {

class TerrainGenerator {
public:
    // Create a simple grid-based ground plane
    static void CreateGroundPlane(Mesh& mesh, float size = 100.0f, int divisions = 10);

    // Create a simple horizon sphere (distant sky)
    static void CreateHorizonSphere(Mesh& mesh, float radius = 500.0f, int stacks = 8, int slices = 16);
};

} // namespace resources
} // namespace outer_wilds