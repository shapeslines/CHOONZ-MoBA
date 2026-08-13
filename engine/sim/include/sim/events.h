#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/handle.h"
#include "core/mem.h"

// Minimal typed combat record for the M3.2 schedule. A null source is valid for
// the retained replay command seam; later gameplay producers can supply a live
// source without changing the queue or record shape.
typedef struct DamageEvent {
    EntityId source;
    EntityId target;
    int32_t amount;
} DamageEvent;

static_assert(sizeof(DamageEvent) == 12u, "DamageEvent stays a compact POD record");

typedef struct DamageEventView {
    const DamageEvent* events;
    uint32_t count;
} DamageEventView;

// Explicit read/write phases make delivery policy a schedule decision. M3.2
// appends, publishes, and consumes in one tick. Publishing at the next tick
// boundary later provides next-tick delivery without changing storage.
typedef struct DamageEventQueue {
    DamageEvent* buffers[2];
    uint32_t counts[2];
    uint32_t capacity;
    uint8_t read_index;
    uint8_t write_index;
} DamageEventQueue;

size_t damage_event_queue_memory_required(uint32_t capacity);

// Initialization and every rejected operation are mutation-free.
bool damage_event_queue_init(DamageEventQueue* queue, Arena* arena, uint32_t capacity);
bool damage_event_queue_is_valid(const DamageEventQueue* queue);
bool damage_event_is_canonical(const DamageEvent* event);

// Producers append to the write phase in literal call order. Publish requires
// the current read phase to be empty, then atomically swaps the two buffers.
bool damage_event_queue_append(DamageEventQueue* queue, const DamageEvent* event);
bool damage_event_queue_publish(DamageEventQueue* queue);
bool damage_event_queue_read(const DamageEventQueue* queue, DamageEventView* view);
bool damage_event_queue_consume(DamageEventQueue* queue);
