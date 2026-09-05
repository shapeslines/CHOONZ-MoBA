#pragma once

#include <stddef.h>
#include <stdint.h>

#include "core/handle.h"
#include "core/mem.h"
#include "math/fix.h"

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

// ------------------------------------------------------------------ M5.2 envelope
// docs/slate-moba-proto-design.md section 3.4 specifies one SimEvent envelope. M5.2
// adds it alongside the damage wire rather than replacing it: DamageEvent stays the
// minimum damage payload the replay seam depends on, and every other typed gameplay
// event (heal, status applied/expired, death) rides this queue. One envelope keeps
// the canonical-hash, diff, and SimStateField surface to a single block no matter
// how many kinds M5.3 adds.
//
// Storage, phases, and failure policy mirror DamageEventQueue one for one, so an
// overflow fails the source operation (ADR-0014) and nothing is ever dropped.

static const uint16_t SIM_EVENT_PAYLOAD_MAX = 16;

typedef enum SimEventKind : uint16_t {
    SIM_EVENT_NONE = 0,
    SIM_EVENT_HEAL = 1,
    SIM_EVENT_STATUS_APPLIED = 2,
    SIM_EVENT_STATUS_EXPIRED = 3,
    SIM_EVENT_DEATH = 4,
    // M5.3 lane objectives. No queue change: SIM_EVENT_PAYLOAD_MAX stays 16 and
    // sizeof(SimEvent) stays 32, so the phases, the canonicality predicate, and the
    // reject-at-source policy are untouched.
    SIM_EVENT_OBJECTIVE_DAMAGED = 5,     // payload_size 16
    SIM_EVENT_OBJECTIVE_DESTROYED = 6,   // payload_size 12
    SIM_EVENT_ECONOMY = 7,               // payload_size 16
    SIM_EVENT_MATCH_OVER = 8,            // payload_size 12
} SimEventKind;

// Reason codes for SIM_EVENT_MATCH_OVER. v1 produces only CORE_DESTROYED; the field
// exists so a later timeout or surrender rule is a data change, not a payload change.
static const uint8_t SIM_MATCH_OVER_CORE_DESTROYED = 1;

typedef struct SimEvent {
    uint64_t tick;
    uint16_t event_kind;
    uint16_t payload_size;          // exactly the fixed size for event_kind
    uint32_t append_ordinal;        // position in the write phase; assigned by append
    uint8_t payload[SIM_EVENT_PAYLOAD_MAX];
} SimEvent;

static_assert(sizeof(SimEvent) == 32u, "SimEvent stays a bounded 32-byte POD envelope");

typedef struct SimEventView {
    const SimEvent* events;
    uint32_t count;
} SimEventView;

typedef struct SimEventQueue {
    SimEvent* buffers[2];
    uint32_t counts[2];
    uint32_t capacity;
    uint8_t read_index;
    uint8_t write_index;
} SimEventQueue;

// Fixed payload size for a kind; 0 for an unknown kind or SIM_EVENT_NONE.
uint16_t sim_event_payload_size(uint16_t event_kind);

size_t sim_event_queue_memory_required(uint32_t capacity);
bool sim_event_queue_init(SimEventQueue* queue, Arena* arena, uint32_t capacity);
bool sim_event_queue_is_valid(const SimEventQueue* queue);

// Known kind, payload_size exactly the kind's fixed size, at least the four bytes the
// standing offset-0 rule reserves, and every byte past payload_size zero.
// append_ordinal is not checked here: the queue assigns it, and queue validity proves
// it equals the position in the phase.
//
// The standing rule, made explicit by M5.3: payload offset 0 is ALWAYS a u32 EntityId
// slot, HANDLE_NULL when the kind names no entity. sys_effects_resolve already reads
// sim_event_payload_u32(&event, 0u) for every kind before branching, so preserving it
// is a correctness requirement rather than a style choice.
bool sim_event_is_canonical(const SimEvent* event);

// Appends in literal call order and stamps append_ordinal with the write position.
bool sim_event_queue_append(SimEventQueue* queue, const SimEvent* event);
bool sim_event_queue_publish(SimEventQueue* queue);
bool sim_event_queue_read(const SimEventQueue* queue, SimEventView* view);
bool sim_event_queue_consume(SimEventQueue* queue);

// Free slots in the write phase; producers pre-flight so an action is atomic.
uint32_t sim_event_queue_write_room(const SimEventQueue* queue);
uint32_t damage_event_queue_write_room(const DamageEventQueue* queue);

// Payload builders and readers. Every field is explicit little-endian, so the hash
// reads bytes, never a struct image.
SimEvent sim_event_make_heal(uint64_t tick, EntityId target, int32_t amount);
SimEvent sim_event_make_status_applied(uint64_t tick, EntityId target, uint8_t effect_type,
                                       uint16_t duration_ticks, int32_t magnitude,
                                       mm::fix scalar);
SimEvent sim_event_make_status_expired(uint64_t tick, EntityId target, uint8_t effect_type);
SimEvent sim_event_make_death(uint64_t tick, EntityId target, EntityId source);

// M5.3 payload layouts, explicit little-endian byte offsets:
//   OBJECTIVE_DAMAGED   (16): target 0..3, source 4..7, amount 8..11, remaining 12..15
//   OBJECTIVE_DESTROYED (12): target 0..3, killer 4..7, owner_team 8, kind 9, rsvd 10..11
//   ECONOMY             (16): subject 0..3, team 4, award_kind 5, source_kind 6,
//                             reserved 7, amount 8..11, ledger_total 12..15
//   MATCH_OVER          (12): HANDLE_NULL 0..3, winner 4, reason 5, rsvd 6..7,
//                             end_tick 8..11
SimEvent sim_event_make_objective_damaged(uint64_t tick, EntityId target, EntityId source,
                                          int32_t amount, int32_t remaining_health);
SimEvent sim_event_make_objective_destroyed(uint64_t tick, EntityId target, EntityId killer,
                                            uint8_t owner_team, uint8_t kind);
SimEvent sim_event_make_economy(uint64_t tick, EntityId subject, uint8_t team,
                                uint8_t award_kind, uint8_t source_kind,
                                uint32_t amount, int32_t ledger_total);
SimEvent sim_event_make_match_over(uint64_t tick, uint8_t winner, uint8_t reason,
                                   uint32_t end_tick);

uint32_t sim_event_payload_u32(const SimEvent* event, uint16_t offset);
uint16_t sim_event_payload_u16(const SimEvent* event, uint16_t offset);
uint8_t sim_event_payload_u8(const SimEvent* event, uint16_t offset);
int32_t sim_event_payload_i32(const SimEvent* event, uint16_t offset);
