#pragma once
// Self-registering test harness (M1.4). No exceptions, no STL containers — just an
// intrusive list of test cases, free-function bodies, and CHECK macros that count
// failures. One process runs many TEST()s; CTest runs one process per suite (via
// --suite) so a red test points at the right module.
//
// Registration: each TEST(suite, name) defines a free function plus a static
// Registrar that links itself into a global intrusive list at dynamic-init time.
// The list head is a function-local static (registry(), construct-on-first-use), so
// the order in which translation units initialize does NOT matter.
//
// LANDMINE: a static object in a TU the linker considers otherwise unreferenced can
// be dropped, taking its registration with it ("my tests silently don't run"). That
// only happens when the test objects live in a *static library*; we compile each
// *_tests.cpp straight into the test executable, so every Registrar is kept. Keep it
// that way (see tests/CMakeLists.txt — no add_library for suite sources).
#include <cstdio>
#include <cstring>
#include <cmath>

namespace test {

// ---- grand totals across every test in the process ----
inline int& checks() { static int n = 0; return n; }
inline int& fails()  { static int n = 0; return n; }

// ---- one registered case; the Registrar *is* the intrusive list node ----
typedef void (*TestFn)();
struct Registrar {
    const char* suite;
    const char* name;
    TestFn      fn;
    Registrar*  next;
    Registrar(const char* s, const char* n, TestFn f);
};

// Construct-on-first-use head — safe regardless of static-init order across TUs.
inline Registrar*& registry() { static Registrar* head = nullptr; return head; }

inline Registrar::Registrar(const char* s, const char* n, TestFn f)
    : suite(s), name(n), fn(f), next(nullptr) {
    // Append at the tail so cases run in registration order within a TU (across TUs
    // the order is unspecified — tests are independent, so that is fine).
    Registrar** pp = &registry();
    while (*pp) pp = &(*pp)->next;
    *pp = this;
}

// ---- the runner: argv selects what to run, return code feeds CTest ----
inline bool arg_eq(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

inline int run_all(int argc, char** argv) {
    const char* suite  = nullptr;   // --suite <s>:      exact suite match
    const char* filter = nullptr;   // --filter <substr>: substring of "suite.name"
    bool list = false;              // --list:           print cases, run nothing
    for (int i = 1; i < argc; ++i) {
        if (arg_eq(argv[i], "--suite") || arg_eq(argv[i], "--filter")) {
            const char* option = argv[i];
            if (i + 1 >= argc || !argv[i + 1] || argv[i + 1][0] == '\0' ||
                argv[i + 1][0] == '-') {
                std::fprintf(stderr, "ERROR: %s requires a selector value\n", option);
                return 2;
            }
            if (arg_eq(option, "--suite")) suite = argv[++i];
            else filter = argv[++i];
        } else if (arg_eq(argv[i], "--list")) {
            list = true;
        } else {
            std::fprintf(stderr, "ERROR: unknown test harness option '%s'\n",
                         argv[i] ? argv[i] : "(null)");
            return 2;
        }
    }

    int n_run = 0, n_failed = 0;
    for (Registrar* c = registry(); c; c = c->next) {
        if (suite && std::strcmp(c->suite, suite) != 0) continue;
        if (filter) {
            char id[256];
            std::snprintf(id, sizeof(id), "%s.%s", c->suite, c->name);
            if (!std::strstr(id, filter)) continue;
        }
        if (list) { std::printf("%s.%s\n", c->suite, c->name); continue; }

        int before = fails();
        c->fn();
        int failed_here = fails() - before;
        ++n_run;
        if (failed_here) { ++n_failed; std::printf("[%s] %-44s FAILED (%d)\n", c->suite, c->name, failed_here); }
        else             std::printf("[%s] %-44s ok\n", c->suite, c->name);
    }
    if (list) return 0;

    // A run that executed ZERO cases is never a pass — it means a --suite/--filter
    // selector drifted from the registered names, or (worse) the linker dropped the
    // registrars and there are no cases at all (the LANDMINE above). Fail loudly with
    // a distinct code so CTest can't report a silent green over no coverage.
    if (n_run == 0) {
        std::printf("\nERROR: 0 tests ran");
        if (suite)  std::printf(" (--suite %s)", suite);
        if (filter) std::printf(" (--filter %s)", filter);
        std::printf(" — selector drift, or no cases registered (dropped registrars?)\n");
        return 2;
    }

    std::printf("\n%d test(s), %d check(s), %d failed", n_run, checks(), fails());
    std::printf(n_failed ? "  -> %d TEST(S) FAILED\n" : "  -> OK\n", n_failed);
    return fails() ? 1 : 0;
}

} // namespace test

// ---- TEST(suite, name) { ... } : body function + static Registrar that links it ----
#define TEST(suite, name)                                                       \
    static void test_##suite##_##name();                                        \
    static ::test::Registrar test_reg_##suite##_##name(                         \
        #suite, #name, &test_##suite##_##name);                                 \
    static void test_##suite##_##name()

// ---- assertions: bump counters, print file:line on failure (semantics unchanged) ----
#define CHECK(cond) do {                                                        \
        ++::test::checks();                                                     \
        if (!(cond)) { std::printf("  FAIL: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); ++::test::fails(); } \
    } while (0)

// Float/near-equality with an absolute epsilon.
#define CHECK_APPROX(a, b, eps) do {                                            \
        ++::test::checks();                                                     \
        double _da = (double)(a), _db = (double)(b), _e = (double)(eps);        \
        if (!(std::fabs(_da - _db) <= _e)) {                                    \
            std::printf("  FAIL: |%s - %s| = %g > %g  (%s:%d)\n",               \
                        #a, #b, std::fabs(_da - _db), _e, __FILE__, __LINE__);  \
            ++::test::fails();                                                  \
        }                                                                       \
    } while (0)
