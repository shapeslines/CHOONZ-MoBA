# M5.2 no-bypass scan (docs/plans/m5-hero-combat.md, "Pipeline"): every health,
# hero-resource, and hero-cooldown MUTATION in engine/sim must live in the one file
# that owns it. This is the sibling of check_sim_boundary.cmake: a source-level lint,
# not a runtime test, because the invariant is "no other call site exists" and no
# runtime test can prove absence.
#
# Ownership of record:
#   health.current           written only by engine/sim/src/combat.cpp
#                            (sys_combat_resolve commits damage, sys_effects_resolve
#                            commits heals). sim_hash.cpp READS it; reads are fine.
#   hero resource            written only by hero.cpp (payment) and components.cpp
#                            (pool allocation, clear, and swap-remove bookkeeping).
#   hero cooldown assignment written only by hero.cpp (arming) and components.cpp.
#   hero cooldown decrement  written only by systems.cpp (sys_cooldown_tick is the
#                            sole decrementer of every cooldown in the sim).
#   health.last_damage_source written only by combat.cpp, on the damage commit; it
#                            is the sole kill-credit input (M5.3).
#   gold / xp ledgers        written only by objectives.cpp (sys_economy).
#   match verdict fields     written only by objectives.cpp (sys_death; sim_init
#                            copies its initial value from there).
#
# Every pattern matches an assignment or compound assignment only, never a read, so
# adding a read of health.current to a new system is legal and adding a write is not.

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE SIM_SOURCES LIST_DIRECTORIES false
    "${SOURCE_DIR}/engine/sim/*.h"
    "${SOURCE_DIR}/engine/sim/*.hpp"
    "${SOURCE_DIR}/engine/sim/*.c"
    "${SOURCE_DIR}/engine/sim/*.cc"
    "${SOURCE_DIR}/engine/sim/*.cpp")

# An assignment or compound assignment, never "==".
set(ASSIGN "[ \t\r\n]*(=[^=]|\\+=|-=|\\*=|/=)")

set(RULE_NAMES  "health"  "resource"  "cooldown_set"  "cooldown_decrement"
                "ledger"  "match_state")

set(PATTERN_health             "health\\.(current|last_damage_source)${ASSIGN}")
set(OWNERS_health              "combat.cpp")

set(PATTERN_resource           "[.>]resource${ASSIGN}")
set(OWNERS_resource            "hero.cpp;components.cpp")

set(PATTERN_cooldown_set       "(basic_attack_cooldown|action_cooldown)(\\[[^]]*\\])?${ASSIGN}")
set(OWNERS_cooldown_set        "hero.cpp;components.cpp;objectives.cpp")

set(PATTERN_cooldown_decrement "--[ \t]*\\*?[A-Za-z_]*[.>]*(basic_attack_cooldown|action_cooldown)")
set(OWNERS_cooldown_decrement  "systems.cpp")

# M5.3: the gold and XP ledgers and the match verdict have the same "no other call
# site exists" property health.current has, and no runtime test can prove absence,
# so they get the same source-level rule.
set(PATTERN_ledger             "(gold|xp)(\\[[^]]*\\])?${ASSIGN}")
set(OWNERS_ledger              "objectives.cpp")

set(PATTERN_match_state        "[.>](over|winner|end_tick)${ASSIGN}")
set(OWNERS_match_state         "objectives.cpp")

# Positive control: the scan is only worth running if the patterns still match a
# violation. Each rule must fire on a synthetic line before the real scan runs.
set(PROBE_health             "        *health.current -= event.amount;")
set(PROBE_resource           "    *hero.resource = 0;")
set(PROBE_cooldown_set       "    hero.action_cooldown[slot] = action->cooldown_ticks;")
set(PROBE_cooldown_decrement "        --*hero.basic_attack_cooldown;")
set(PROBE_ledger             "    world->ledger.gold[team] = total;")
set(PROBE_match_state        "    world->match_state.over = 1u;")

set(violations "")
foreach(rule IN LISTS RULE_NAMES)
    if(NOT "${PROBE_${rule}}" MATCHES "${PATTERN_${rule}}")
        list(APPEND violations
             "self-test: rule '${rule}' no longer matches its own probe line; the scan is blind")
    endif()
endforeach()

foreach(path IN LISTS SIM_SOURCES)
    get_filename_component(name "${path}" NAME)
    file(READ "${path}" source)
    foreach(rule IN LISTS RULE_NAMES)
        if(NOT source MATCHES "${PATTERN_${rule}}")
            continue()
        endif()
        set(owned FALSE)
        foreach(owner IN LISTS OWNERS_${rule})
            if(name STREQUAL "${owner}")
                set(owned TRUE)
            endif()
        endforeach()
        if(NOT owned)
            list(APPEND violations
                 "${path}: '${CMAKE_MATCH_0}' -- ${rule} may only be written in ${OWNERS_${rule}}")
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " formatted)
    message(FATAL_ERROR
        "unified effect pipeline bypassed; every mutation must go through resolve_effect:\n  ${formatted}")
endif()

list(LENGTH SIM_SOURCES source_count)
message(STATUS
    "sim pipeline ownership clean across ${source_count} files: health writes only in combat.cpp, "
    "resource/cooldown arming only in hero.cpp+components.cpp+objectives.cpp, "
    "cooldown decrement only in systems.cpp, ledger and match verdict only in "
    "objectives.cpp")
