#include "doctest.h"

TEST_CASE("Testing xyz") {
    SUBCASE("xyz") {
        constexpr float a = 0.000001;
        CHECK(a == doctest::Approx(0.0));
    }
}
