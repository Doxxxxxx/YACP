add_executable(OtaStagingTest
  OtaStagingTest.cpp
  ${REPO_ROOT}/src/network/OtaUpdater.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_include_directories(OtaStagingTest PRIVATE
  stubs ${REPO_ROOT}/src ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Memory ${REPO_ROOT}/lib/AppVersion
)
target_compile_definitions(OtaStagingTest PRIVATE
  CROSSINK_VERSION="1.7.1-yacp" CROSSPOINT_FIRMWARE_VARIANT="tiny"
)
target_link_libraries(OtaStagingTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OtaStagingTest)
