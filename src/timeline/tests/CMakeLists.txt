find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(oaktimeline-gtest
        timelinemarker_test.cpp
        timeline_test.cpp
)

target_link_libraries(oaktimeline-gtest PRIVATE
        oaktimeline
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(oaktimeline-gtest
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT
        "OCIO=${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio"
)
