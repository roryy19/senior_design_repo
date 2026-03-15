#pragma once
#include <cstdio>
#include <cstdlib>

// Counters are defined in test_main.cpp, shared across all test files
extern int tests_run;
extern int tests_passed;
extern int tests_failed;

#define ASSERT_EQ(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s (expected %d, got %d) at %s:%d\n", \
               msg, (int)(expected), (int)(actual), __FILE__, __LINE__); \
    } \
} while(0)

#define ASSERT_HEX_EQ(expected, actual, msg) do { \
    tests_run++; \
    if ((expected) == (actual)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s (expected 0x%02X, got 0x%02X) at %s:%d\n", \
               msg, (unsigned)(expected), (unsigned)(actual), __FILE__, __LINE__); \
    } \
} while(0)

#define ASSERT_TRUE(condition, msg) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s at %s:%d\n", msg, __FILE__, __LINE__); \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    printf("  %s...\n", #fn); \
    fn(); \
} while(0)

inline void printSummary() {
    printf("\n========================================\n");
    printf("Tests run: %d  Passed: %d  Failed: %d\n", tests_run, tests_passed, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    } else {
        printf("SOME TESTS FAILED\n");
    }
    printf("========================================\n");
}
