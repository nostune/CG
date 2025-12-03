#pragma once
#include <entt/entt.hpp>

namespace outer_wilds {

// Use EnTT's entity handle. It's just an integer, but the library
// provides a nice wrapper around it.
using Entity = entt::entity;

// Base Component class for components that need virtual methods
class Component {
public:
    virtual ~Component() = default;
};

// The base System class is simplified. Initialization and Shutdown
// can be handled by the system's constructor/destructor or dedicated
// public methods if needed.
class System {
public:
    virtual ~System() = default;
    virtual void Update(float deltaTime, entt::registry& registry) = 0;
    virtual void Initialize() {}
    virtual void Shutdown() {}
};

} // namespace outer_wilds
