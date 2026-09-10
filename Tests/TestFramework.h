/*
    BrownSweep - minimal dependency-free test harness.

    Deliberately tiny: the DSP core has no third-party dependencies, and the
    test suite should not add one either.  `ctest` runs the single executable.
*/

#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace testing
{

struct TestCase
{
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry();

struct Registrar
{
    Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); }
};

extern int failures;
extern int checks;
extern std::string currentTest;

void reportFailure (const char* file, int line, const std::string& message);

inline std::string describe (double v) { char b[64]; std::snprintf (b, sizeof b, "%.9g", v); return b; }

} // namespace testing

#define BS_TEST(name)                                                          \
    static void name();                                                        \
    static ::testing::Registrar bs_reg_##name (#name, name);                   \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++::testing::checks;                                                   \
        if (! (cond)) ::testing::reportFailure (__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (false)

#define CHECK_MSG(cond, msg)                                                   \
    do {                                                                       \
        ++::testing::checks;                                                   \
        if (! (cond))                                                          \
            ::testing::reportFailure (__FILE__, __LINE__,                      \
                std::string ("CHECK failed: " #cond " | ") + (msg));           \
    } while (false)

#define CHECK_NEAR(a, b, tol)                                                  \
    do {                                                                       \
        ++::testing::checks;                                                   \
        const double bs_a = (double) (a), bs_b = (double) (b), bs_t = (double) (tol); \
        if (! (std::fabs (bs_a - bs_b) <= bs_t))                               \
            ::testing::reportFailure (__FILE__, __LINE__,                      \
                std::string ("CHECK_NEAR failed: " #a " = ") + ::testing::describe (bs_a) \
                + ", " #b " = " + ::testing::describe (bs_b)                   \
                + ", tolerance " + ::testing::describe (bs_t));                \
    } while (false)

#define CHECK_LE(a, b)                                                         \
    do {                                                                       \
        ++::testing::checks;                                                   \
        const double bs_a = (double) (a), bs_b = (double) (b);                 \
        if (! (bs_a <= bs_b))                                                  \
            ::testing::reportFailure (__FILE__, __LINE__,                      \
                std::string ("CHECK_LE failed: " #a " = ") + ::testing::describe (bs_a) \
                + " > " #b " = " + ::testing::describe (bs_b));                \
    } while (false)

#define CHECK_GE(a, b)                                                         \
    do {                                                                       \
        ++::testing::checks;                                                   \
        const double bs_a = (double) (a), bs_b = (double) (b);                 \
        if (! (bs_a >= bs_b))                                                  \
            ::testing::reportFailure (__FILE__, __LINE__,                      \
                std::string ("CHECK_GE failed: " #a " = ") + ::testing::describe (bs_a) \
                + " < " #b " = " + ::testing::describe (bs_b));                \
    } while (false)
