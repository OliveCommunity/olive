find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(oaktask-gtest
        task_test.cpp
)

target_compile_definitions(oaktask-gtest PRIVATE
        OAK_REPO_ROOT="${OAK_REPO_ROOT}")

target_link_libraries(oaktask-gtest PRIVATE
        oaktask
        oaktimeline
        oakplugin
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(oaktask-gtest
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT
        "OCIO=${OAK_REPO_ROOT}/engine/render/ocioconf/config.ocio"
)
