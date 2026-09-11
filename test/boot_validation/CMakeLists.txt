add_executable(BootValidationTest
  BootValidationTest.cpp
  ${REPO_ROOT}/lib/hal/HalBootValidation.cpp
)
target_include_directories(BootValidationTest PRIVATE stubs ${REPO_ROOT}/lib/hal)
target_link_libraries(BootValidationTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BootValidationTest)
