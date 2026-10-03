// Entity identifiers. The top 4 bits encode the entity kind, the low 28 bits a serial number.
// Serial numbers are never reused, so an id stays meaningful in saves, reports and memories
// long after the entity is gone (dead individuals keep their id in the archive).
#pragma once

#include "Noctis/Core/Platform.h"

namespace noctis
{
enum class EntityKind : u8
{
    None = 0,
    Creature = 1,
    Carcass = 2,
    Nest = 3,
    Researcher = 4,
    Vehicle = 5,
    Drone = 6,
    Sensor = 7,
    FossilSite = 8,
    Group = 9,
    Environment = 10 // weather, river, thunder: sources without a body
};

struct EntityId
{
    u32 value = 0;

    constexpr EntityId() = default;
    constexpr explicit EntityId(u32 raw) : value(raw) {}
    static constexpr EntityId make(EntityKind kind, u32 serial)
    {
        return EntityId((static_cast<u32>(kind) << 28) | (serial & 0x0FFFFFFFu));
    }

    constexpr EntityKind kind() const { return static_cast<EntityKind>(value >> 28); }
    constexpr u32 serial() const { return value & 0x0FFFFFFFu; }
    constexpr bool valid() const { return value != 0; }
    constexpr bool operator==(const EntityId& o) const { return value == o.value; }
    constexpr bool operator!=(const EntityId& o) const { return value != o.value; }
    constexpr bool operator<(const EntityId& o) const { return value < o.value; }
};

inline constexpr EntityId kNoEntity{};
} // namespace noctis
