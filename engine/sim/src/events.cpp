#include "sim/events.h"

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

