#include "test_helpers.h"
#include "../src/core/distance_categorizer.h"

using namespace firmware;

void test_far_distance_returns_zero() {
    ASSERT_EQ(0, categorizeDistance(150.0f), "150cm should be level 0");
    ASSERT_EQ(0, categorizeDistance(100.0f), "100cm should be level 0");
    ASSERT_EQ(0, categorizeDistance(200.0f), "200cm should be level 0");
}

void test_close_distance_returns_max() {
    ASSERT_EQ(7, categorizeDistance(5.0f),  "5cm should be level 7");
    ASSERT_EQ(7, categorizeDistance(1.0f),  "1cm should be level 7");
    ASSERT_EQ(7, categorizeDistance(9.9f),  "9.9cm should be level 7");
}

void test_boundary_values() {
    ASSERT_EQ(1, categorizeDistance(85.0f),  "85cm should be level 1");
    ASSERT_EQ(2, categorizeDistance(84.9f),  "84.9cm should be level 2");
    ASSERT_EQ(2, categorizeDistance(70.0f),  "70cm should be level 2");
    ASSERT_EQ(3, categorizeDistance(69.9f),  "69.9cm should be level 3");
    ASSERT_EQ(6, categorizeDistance(10.0f),  "10cm should be level 6");
    ASSERT_EQ(7, categorizeDistance(9.9f),   "9.9cm should be level 7");
}

void test_mid_range_values() {
    ASSERT_EQ(2, categorizeDistance(72.0f), "72cm should be level 2");
    ASSERT_EQ(3, categorizeDistance(55.0f), "55cm should be level 3");
    ASSERT_EQ(4, categorizeDistance(42.0f), "42cm should be level 4");
    ASSERT_EQ(5, categorizeDistance(25.0f), "25cm should be level 5");
}

void test_invalid_distance_returns_zero() {
    ASSERT_EQ(0, categorizeDistance(0.0f),   "0cm should be level 0");
    ASSERT_EQ(0, categorizeDistance(-10.0f), "-10cm should be level 0");
    ASSERT_EQ(0, categorizeDistance(-1.0f),  "-1cm should be level 0");
}

void test_arm_length_scaling() {
    // With arm length 80cm vs reference 65cm, scale = 80/65 ~= 1.23
    // Threshold[0] = 100 * 1.23 = 123cm. So 110cm should now be level 1.
    uint8_t level = categorizeDistance(110.0f, 80.0f);
    ASSERT_TRUE(level > 0, "110cm with 80cm arm should trigger detection");

    // With default (no arm length), 110cm is level 0
    uint8_t levelDefault = categorizeDistance(110.0f, 0.0f);
    ASSERT_EQ(0, levelDefault, "110cm with default should be level 0");

    // Short arm (50cm), scale = 50/65 ~= 0.77
    // Threshold[0] = 100 * 0.77 = 77cm. So 80cm is still level 0.
    uint8_t levelShort = categorizeDistance(80.0f, 50.0f);
    ASSERT_EQ(0, levelShort, "80cm with 50cm arm should be level 0");
}

void runDistanceCategorizerTests() {
    RUN_TEST(test_far_distance_returns_zero);
    RUN_TEST(test_close_distance_returns_max);
    RUN_TEST(test_boundary_values);
    RUN_TEST(test_mid_range_values);
    RUN_TEST(test_invalid_distance_returns_zero);
    RUN_TEST(test_arm_length_scaling);
}
