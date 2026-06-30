#pragma once

#include "engine/scene/Component.h"
#include "engine/scene/Entity.h"

#include <array>
#include <string>
#include <vector>

namespace aine {

struct PhysicsVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct PhysicsVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PhysicsRayHit2D {
    bool hit = false;
    int entityId = 0;
    std::string entityName;
    std::string colliderType;
    PhysicsVec2 point;
    PhysicsVec2 normal;
    float distance = 0.0f;
};

struct PhysicsRayHit3D {
    bool hit = false;
    int entityId = 0;
    std::string entityName;
    std::string colliderType;
    PhysicsVec3 point;
    PhysicsVec3 normal;
    float distance = 0.0f;
};

struct PhysicsEvent {
    std::string dimension;
    std::string type;
    int entityAId = 0;
    int entityBId = 0;
    std::string entityAName;
    std::string entityBName;
    PhysicsVec3 point;
    PhysicsVec3 normal;
    float penetration = 0.0f;
    bool trigger = false;
};

struct PhysicsStepResult {
    int fixedSteps = 0;
    int body2DCount = 0;
    int collider2DCount = 0;
    int body3DCount = 0;
    int collider3DCount = 0;
    std::vector<int> changedEntityIds;
    std::vector<PhysicsEvent> events;
};

class PhysicsWorld {
public:
    ~PhysicsWorld();

    void Reset();
    PhysicsStepResult AccumulateAndStep(std::vector<Entity>& entities, float deltaSeconds);
    PhysicsStepResult StepFixed(std::vector<Entity>& entities, float fixedDeltaSeconds);

    bool Raycast2D(const std::vector<Entity>& entities, PhysicsVec2 origin, PhysicsVec2 direction,
                   float maxDistance, PhysicsRayHit2D* outHit = nullptr) const;
    bool Raycast3D(const std::vector<Entity>& entities, PhysicsVec3 origin, PhysicsVec3 direction,
                   float maxDistance, PhysicsRayHit3D* outHit = nullptr) const;
    bool SphereCast3D(const std::vector<Entity>& entities, PhysicsVec3 origin, PhysicsVec3 direction,
                      float maxDistance, float radius, PhysicsRayHit3D* outHit = nullptr) const;
    std::vector<int> OverlapCircle2D(const std::vector<Entity>& entities, PhysicsVec2 center, float radius) const;
    std::vector<int> OverlapSphere3D(const std::vector<Entity>& entities, PhysicsVec3 center, float radius) const;

    const std::vector<PhysicsEvent>& LastEvents() const { return lastEvents_; }
    int LastFixedStepCount() const { return lastFixedStepCount_; }
    int LastBody2DCount() const { return lastBody2DCount_; }
    int LastCollider2DCount() const { return lastCollider2DCount_; }
    int LastBody3DCount() const { return lastBody3DCount_; }
    int LastCollider3DCount() const { return lastCollider3DCount_; }

private:
    float accumulatorSeconds_ = 0.0f;
    std::vector<PhysicsEvent> lastEvents_;
    int lastFixedStepCount_ = 0;
    int lastBody2DCount_ = 0;
    int lastCollider2DCount_ = 0;
    int lastBody3DCount_ = 0;
    int lastCollider3DCount_ = 0;
};

float Dot(PhysicsVec2 left, PhysicsVec2 right);
float Dot(PhysicsVec3 left, PhysicsVec3 right);
float Length(PhysicsVec2 value);
float Length(PhysicsVec3 value);
PhysicsVec2 Normalize(PhysicsVec2 value, PhysicsVec2 fallback = {1.0f, 0.0f});
PhysicsVec3 Normalize(PhysicsVec3 value, PhysicsVec3 fallback = {1.0f, 0.0f, 0.0f});

Component MakePhysicsSettingsComponent();
Component MakeRigidbody2DComponent(const std::string& bodyType = "Dynamic");
Component MakeRigidbody3DComponent(const std::string& bodyType = "Dynamic");
Component MakeCollider2DComponent(const std::string& shape = "Box", bool trigger = false);
Component MakeCollider3DComponent(const std::string& shape = "Box", bool trigger = false);

bool RunPhysicsSelfTests(std::vector<std::string>* diagnostics);

} // namespace aine
