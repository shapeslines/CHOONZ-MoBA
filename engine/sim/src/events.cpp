#include "sim/events.h"

#include <cstring>

static bool layout_advance(const Arena* arena, size_t* offset, size_t size, size_t alignment) {
    if (!arena || !arena->base || !offset || alignment == 0u ||
        (alignment & (alignment - 1u)) != 0u || *offset > arena->reserved) return false;

    uintptr_t base = reinterpret_cast<uintptr_t>(arena->base);
    if (*offset > static_cast<size_t>(UINTPTR_MAX - base)) return false;
    uintptr_t current = base + *offset;
    size_t padding = static_cast<size_t>(
        (alignment - (current & static_cast<uintptr_t>(alignment - 1u))) & (alignment - 1u));
    if (padding > SIZE_MAX - *offset) return false;
    size_t start = *offset + padding;
    if (size > SIZE_MAX - start) return false;
    size_t end = start + size;
    if (end > arena->reserved) return false;
    *offset = end;
    return true;
}

size_t damage_event_queue_memory_required(uint32_t capacity) {
    if (capacity == 0u || static_cast<size_t>(capacity) > SIZE_MAX / sizeof(DamageEvent))
        return 0u;
    size_t bytes = sizeof(DamageEvent) * static_cast<size_t>(capacity);
    size_t allocation = bytes + (alignof(DamageEvent) - 1u);
    if (allocation < bytes || allocation > SIZE_MAX / 2u) return 0u;
    return allocation * 2u;
}

bool damage_event_queue_init(DamageEventQueue* queue, Arena* arena, uint32_t capacity) {
    size_t required = damage_event_queue_memory_required(capacity);
    if (!queue || !arena || !arena->base || arena->offset > arena->reserved ||
        required == 0u || required > arena->reserved - arena->offset) return false;

    size_t bytes = sizeof(DamageEvent) * static_cast<size_t>(capacity);
    size_t end = arena->offset;
    if (!layout_advance(arena, &end, bytes, alignof(DamageEvent)) ||
        !layout_advance(arena, &end, bytes, alignof(DamageEvent))) return false;

    DamageEventQueue staged{};
    staged.buffers[0] = static_cast<DamageEvent*>(
        arena_push_zero(arena, bytes, alignof(DamageEvent)));
    staged.buffers[1] = static_cast<DamageEvent*>(
        arena_push_zero(arena, bytes, alignof(DamageEvent)));
    staged.capacity = capacity;
    staged.read_index = 0u;
    staged.write_index = 1u;
    *queue = staged;
    return true;
}

bool damage_event_queue_is_valid(const DamageEventQueue* queue) {
    return queue && queue->buffers[0] && queue->buffers[1] && queue->capacity > 0u &&
           queue->read_index < 2u && queue->write_index < 2u &&
           queue->read_index != queue->write_index &&
           queue->counts[0] <= queue->capacity && queue->counts[1] <= queue->capacity;
}

bool damage_event_is_canonical(const DamageEvent* event) {
    if (!event || handle_gen(event->target.h) == 0u || event->amount < 0) return false;
    return event->source.h == HANDLE_NULL || handle_gen(event->source.h) != 0u;
}

bool damage_event_queue_append(DamageEventQueue* queue, const DamageEvent* event) {
    if (!damage_event_queue_is_valid(queue) || !damage_event_is_canonical(event)) return false;
    uint32_t write = queue->write_index;
    uint32_t count = queue->counts[write];
    if (count >= queue->capacity) return false;
    queue->buffers[write][count] = *event;
    queue->counts[write] = count + 1u;
    return true;
}

bool damage_event_queue_publish(DamageEventQueue* queue) {
    if (!damage_event_queue_is_valid(queue) || queue->counts[queue->read_index] != 0u)
        return false;
    uint8_t old_read = queue->read_index;
    queue->read_index = queue->write_index;
    queue->write_index = old_read;
    queue->counts[queue->write_index] = 0u;
    return true;
}

bool damage_event_queue_read(const DamageEventQueue* queue, DamageEventView* view) {
    if (!damage_event_queue_is_valid(queue) || !view) return false;
    DamageEventView staged{queue->buffers[queue->read_index], queue->counts[queue->read_index]};
    *view = staged;
    return true;
}

bool damage_event_queue_consume(DamageEventQueue* queue) {
    if (!damage_event_queue_is_valid(queue)) return false;
    uint32_t read = queue->read_index;
    for (uint32_t i = 0u; i < queue->counts[read]; ++i)
        queue->buffers[read][i] = DamageEvent{};
    queue->counts[read] = 0u;
    return true;
}

// ------------------------------------------------------------------ M5.2 envelope

uint16_t sim_event_payload_size(uint16_t event_kind) {
    switch (event_kind) {
        case SIM_EVENT_HEAL: return 8u;
        case SIM_EVENT_STATUS_APPLIED: return 16u;
        case SIM_EVENT_STATUS_EXPIRED: return 8u;
        case SIM_EVENT_DEATH: return 8u;
        default: return 0u;
    }
}

size_t sim_event_queue_memory_required(uint32_t capacity) {
    if (capacity == 0u || static_cast<size_t>(capacity) > SIZE_MAX / sizeof(SimEvent)) return 0u;
    size_t bytes = sizeof(SimEvent) * static_cast<size_t>(capacity);
    size_t allocation = bytes + (alignof(SimEvent) - 1u);
    if (allocation < bytes || allocation > SIZE_MAX / 2u) return 0u;
    return allocation * 2u;
}

bool sim_event_queue_init(SimEventQueue* queue, Arena* arena, uint32_t capacity) {
    size_t required = sim_event_queue_memory_required(capacity);
    if (!queue || !arena || !arena->base || arena->offset > arena->reserved ||
        required == 0u || required > arena->reserved - arena->offset) return false;

    size_t bytes = sizeof(SimEvent) * static_cast<size_t>(capacity);
    size_t end = arena->offset;
    if (!layout_advance(arena, &end, bytes, alignof(SimEvent)) ||
        !layout_advance(arena, &end, bytes, alignof(SimEvent))) return false;

    SimEventQueue staged{};
    staged.buffers[0] = static_cast<SimEvent*>(arena_push_zero(arena, bytes, alignof(SimEvent)));
    staged.buffers[1] = static_cast<SimEvent*>(arena_push_zero(arena, bytes, alignof(SimEvent)));
    staged.capacity = capacity;
    staged.read_index = 0u;
    staged.write_index = 1u;
    *queue = staged;
    return true;
}

bool sim_event_is_canonical(const SimEvent* event) {
    if (!event) return false;
    uint16_t size = sim_event_payload_size(event->event_kind);
    if (size == 0u || size > SIM_EVENT_PAYLOAD_MAX || event->payload_size != size) return false;
    for (uint16_t i = size; i < SIM_EVENT_PAYLOAD_MAX; ++i) {
        if (event->payload[i] != 0u) return false;
    }
    return true;
}

bool sim_event_queue_is_valid(const SimEventQueue* queue) {
    if (!queue || !queue->buffers[0] || !queue->buffers[1] || queue->capacity == 0u ||
        queue->read_index > 1u || queue->write_index > 1u ||
        queue->read_index == queue->write_index ||
        queue->counts[0] > queue->capacity || queue->counts[1] > queue->capacity) return false;
    for (uint32_t buffer = 0u; buffer < 2u; ++buffer) {
        for (uint32_t ordinal = 0u; ordinal < queue->counts[buffer]; ++ordinal) {
            const SimEvent& event = queue->buffers[buffer][ordinal];
            if (!sim_event_is_canonical(&event) || event.append_ordinal != ordinal) return false;
        }
    }
    return true;
}

bool sim_event_queue_append(SimEventQueue* queue, const SimEvent* event) {
    if (!sim_event_queue_is_valid(queue) || !sim_event_is_canonical(event)) return false;
    uint32_t write = queue->write_index;
    uint32_t count = queue->counts[write];
    if (count >= queue->capacity) return false;
    SimEvent staged = *event;
    staged.append_ordinal = count;
    queue->buffers[write][count] = staged;
    queue->counts[write] = count + 1u;
    return true;
}

bool sim_event_queue_publish(SimEventQueue* queue) {
    if (!sim_event_queue_is_valid(queue) || queue->counts[queue->read_index] != 0u) return false;
    uint8_t old_read = queue->read_index;
    queue->read_index = queue->write_index;
    queue->write_index = old_read;
    queue->counts[queue->write_index] = 0u;
    return true;
}

bool sim_event_queue_read(const SimEventQueue* queue, SimEventView* view) {
    if (!sim_event_queue_is_valid(queue) || !view) return false;
    SimEventView staged{queue->buffers[queue->read_index], queue->counts[queue->read_index]};
    *view = staged;
    return true;
}

bool sim_event_queue_consume(SimEventQueue* queue) {
    if (!sim_event_queue_is_valid(queue)) return false;
    uint32_t read = queue->read_index;
    for (uint32_t i = 0u; i < queue->counts[read]; ++i) queue->buffers[read][i] = SimEvent{};
    queue->counts[read] = 0u;
    return true;
}

uint32_t sim_event_queue_write_room(const SimEventQueue* queue) {
    if (!sim_event_queue_is_valid(queue)) return 0u;
    return queue->capacity - queue->counts[queue->write_index];
}

uint32_t damage_event_queue_write_room(const DamageEventQueue* queue) {
    if (!damage_event_queue_is_valid(queue)) return 0u;
    return queue->capacity - queue->counts[queue->write_index];
}

static void payload_put_u8(SimEvent* event, uint16_t offset, uint8_t value) {
    event->payload[offset] = value;
}

static void payload_put_u16(SimEvent* event, uint16_t offset, uint16_t value) {
    event->payload[offset] = static_cast<uint8_t>(value);
    event->payload[offset + 1u] = static_cast<uint8_t>(value >> 8u);
}

static void payload_put_u32(SimEvent* event, uint16_t offset, uint32_t value) {
    for (uint16_t i = 0u; i < 4u; ++i)
        event->payload[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
}

static uint32_t u32_from_i32(int32_t value) {
    uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

SimEvent sim_event_make_heal(uint64_t tick, EntityId target, int32_t amount) {
    SimEvent event{};
    event.tick = tick;
    event.event_kind = SIM_EVENT_HEAL;
    event.payload_size = sim_event_payload_size(SIM_EVENT_HEAL);
    payload_put_u32(&event, 0u, target.h);
    payload_put_u32(&event, 4u, u32_from_i32(amount));
    return event;
}

SimEvent sim_event_make_status_applied(uint64_t tick, EntityId target, uint8_t effect_type,
                                       uint16_t duration_ticks, int32_t magnitude,
                                       mm::fix scalar) {
    SimEvent event{};
    event.tick = tick;
    event.event_kind = SIM_EVENT_STATUS_APPLIED;
    event.payload_size = sim_event_payload_size(SIM_EVENT_STATUS_APPLIED);
    payload_put_u32(&event, 0u, target.h);
    payload_put_u8(&event, 4u, effect_type);
    payload_put_u8(&event, 5u, 0u);
    payload_put_u16(&event, 6u, duration_ticks);
    payload_put_u32(&event, 8u, u32_from_i32(magnitude));
    payload_put_u32(&event, 12u, u32_from_i32(scalar));
    return event;
}

SimEvent sim_event_make_status_expired(uint64_t tick, EntityId target, uint8_t effect_type) {
    SimEvent event{};
    event.tick = tick;
    event.event_kind = SIM_EVENT_STATUS_EXPIRED;
    event.payload_size = sim_event_payload_size(SIM_EVENT_STATUS_EXPIRED);
    payload_put_u32(&event, 0u, target.h);
    payload_put_u8(&event, 4u, effect_type);
    return event;
}

SimEvent sim_event_make_death(uint64_t tick, EntityId target, EntityId source) {
    SimEvent event{};
    event.tick = tick;
    event.event_kind = SIM_EVENT_DEATH;
    event.payload_size = sim_event_payload_size(SIM_EVENT_DEATH);
    payload_put_u32(&event, 0u, target.h);
    payload_put_u32(&event, 4u, source.h);
    return event;
}

uint8_t sim_event_payload_u8(const SimEvent* event, uint16_t offset) {
    if (!event || offset >= event->payload_size) return 0u;
    return event->payload[offset];
}

uint16_t sim_event_payload_u16(const SimEvent* event, uint16_t offset) {
    if (!event || offset + 1u >= event->payload_size) return 0u;
    return static_cast<uint16_t>(static_cast<uint16_t>(event->payload[offset]) |
                                 (static_cast<uint16_t>(event->payload[offset + 1u]) << 8u));
}

uint32_t sim_event_payload_u32(const SimEvent* event, uint16_t offset) {
    if (!event || offset + 3u >= event->payload_size) return 0u;
    uint32_t value = 0u;
    for (uint16_t i = 0u; i < 4u; ++i)
        value |= static_cast<uint32_t>(event->payload[offset + i]) << (i * 8u);
    return value;
}

int32_t sim_event_payload_i32(const SimEvent* event, uint16_t offset) {
    uint32_t bits = sim_event_payload_u32(event, offset);
    int32_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
