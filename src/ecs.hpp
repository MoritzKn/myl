#pragma once

#include <bitset>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include <boost/container/flat_map.hpp>
#include <boost/dynamic_bitset.hpp>

#include "struct.hpp"

using EntityId = uint32_t;
constexpr auto maxEntityId = std::numeric_limits<EntityId>::max();
constexpr auto maxComponents = 64;

class ComponentPool {
public:
    ComponentPool(size_t componentSize, size_t pageSize = 0);

    bool has(EntityId entityId) const;
    void* add(EntityId entityId);
    // No const overload, because you probably never have a const ComponentPool anyways
    void* get(EntityId entityId);
    void remove(EntityId entityId);

private:
    struct Page {
        void* data = nullptr;
        boost::dynamic_bitset<> occupied;

        Page(size_t pageSize);
    };

    std::pair<size_t, size_t> getIndices(EntityId entityId) const;
    void* getPointer(size_t page, size_t index);

    size_t componentSize_;
    size_t pageSize_;
    std::vector<Page> pages_;
};

class Component {
public:
    using Id = size_t;

    Component(const std::string& name, Struct&& s);

    Id getId() const;
    const std::string& getName() const;
    const Struct& getStruct() const;

private:
    static Id getComponentId();

    Id id_;
    std::string name_;
    Struct struct_;
};

class ComponentMask {
public:
    ComponentMask() = default;
    ComponentMask(Component::Id id);

    bool includes(Component::Id id) const;
    bool includes(const ComponentMask& other) const;

    // Returns true if none of the components in other are included in *this
    bool includesNot(const ComponentMask& other) const;

    void include(Component::Id id);

    ComponentMask operator+(Component::Id id) const;
    ComponentMask operator+(const ComponentMask& other) const;

    void clear();

    auto getMask() const;

private:
    ComponentMask(const std::bitset<maxComponents>& mask);

    std::bitset<maxComponents> mask_;
};

class World {
public:
    struct System {
        std::string name;
        std::function<void(float)> function;
        double lastDuration = 0.0;
        bool enabled = true;

        template <typename Func>
        System(const std::string& name, Func&& func)
            : name(name)
            , function(std::forward<Func>(func))
        {
        }
    };

    World() = default;

    bool entityExists(EntityId id) const;
    EntityId newEntity();
    void destroyEntity(EntityId id);

    template <typename... Args>
    void registerComponent(const std::string& name, Args&&... args)
    {
        // Component names must be unique
        assert(std::all_of(components_.begin(), components_.end(),
            [&name](const Component& c) { return name != c.getName(); }));
        components_.emplace_back(name, std::forward<Args>(args)...);

        const Component& component = components_.back();
        // TODO: Page size has to be configurable at some point.
        componentPools_.emplace_back(component.getStruct().getSize());
        componentNames_.emplace(component.getName(), component.getId());
    }

    const std::vector<Component>& getComponents();

    bool hasComponent(EntityId id, Component::Id compId);

    template <typename T = void>
    T* addComponent(EntityId id, Component::Id compId)
    {
        assert(!hasComponent(id, compId));
        entities_[id].components.include(compId);
        return reinterpret_cast<T*>(componentPools_[compId].add(id));
    }

    template <typename T = void>
    T* getComponent(EntityId id, Component::Id compId)
    {
        return reinterpret_cast<T*>(componentPools_[compId].get(id));
    }

    void removeComponent(EntityId id, Component::Id compId);

    Component::Id getComponentId(const std::string& name) const;

    template <typename Func>
    void registerSystem(const std::string& name, Func&& func)
    {
        // This is kind of a hack to sort the internal systems vector of World,
        // but for some things I want it to be sorted.
        auto it = systems_.begin();
        while (it != systems_.end() && it->name < name)
            it++;
        systems_.emplace(it, name, std::forward<Func>(func));

        systemNames_.clear();
        for (size_t i = 0; i < systems_.size(); ++i)
            systemNames_.emplace(systems_[i].name, i);
    }

    std::vector<System>& getSystems()
    {
        return systems_;
    }

    // Implement foreachEntity in the future that returns a custom iterator.
    std::vector<EntityId> getEntities(const ComponentMask& mask = ComponentMask()) const;
    void invokeSystem(const std::string& name, float dt);

private:
    struct Entity {
        bool exists;
        ComponentMask components;
    };

    std::vector<Component> components_;
    boost::container::flat_map<std::string, Component::Id> componentNames_;
    std::vector<ComponentPool> componentPools_;

    std::vector<Entity> entities_;
    std::priority_queue<EntityId, std::vector<EntityId>, std::greater<>> entityIdFreeList_;

    std::vector<System> systems_;
    boost::container::flat_map<std::string, size_t> systemNames_;
};

template <typename T>
class SystemData {
public:
    SystemData(World& world, Component::Id componentId)
        : world_(world)
        , boundComponent_(componentId)
        , data_(sizeof(T))
    {
    }

    bool has(EntityId id) const
    {
        return data_.has(id);
    }

    template <typename... Args>
    T& add(EntityId id, Args&&... args)
    {
        T* p = reinterpret_cast<T*>(data_.add(id));
        return *new (p) T(std::forward<Args>(args)...);
    }

    template <bool addDefault = false>
    T& get(EntityId id)
    {
        if constexpr (addDefault) {
            if (!has(id))
                return add(id);
        }
        return *reinterpret_cast<T*>(data_.get(id));
    }

    void remove(EntityId id)
    {
        T* ptr = reinterpret_cast<T*>(data_.get(id));
        ptr->~T();
        data_.remove(id);
    }

    void remove()
    {
        for (auto entityId : world_.getEntities()) {
            if (has(entityId) && !world_.hasComponent(entityId, boundComponent_))
                remove(entityId);
        }
    }

private:
    World& world_;
    Component::Id boundComponent_;
    ComponentPool data_;
};