# CMake build file for Tart/test-unit

include_directories(${TART_SOURCE_DIR}/third-party/gtest-1.3.0/include)
include_directories(${TART_SOURCE_DIR}/third-party/gmock-1.1.0/include)

# Unit test executable. We specify the sources explicitly because we want
# the tests to run in that order.
add_executable(unittest
  main.cpp
  FakeSourceFile.h
  SmallEnumSetTest.cpp
  LexerTest.cpp
  ParserTest.cpp
  ScopeTest.cpp
  TypeTest.cpp
  DefnTest.cpp
  ScopeBuilderTest.cpp
  DeclAnalyzerTest.cpp
  ParameterAssignmentsTest.cpp
  TypeInferenceTest.cpp
  UnifyTest.cpp)
target_link_libraries(unittest
    gtest gmock compiler collector
    ${LLVM_BIT_WRITER}
    ${LLVM_ANALYSIS} 
    ${LLVM_TARGET}
    ${LLVM_SUPPORT}
    ${LLVM_SYSTEM}
    ${LLVM_CORE})

add_custom_target(unittest.run DEPENDS unittest COMMAND unittest)
add_dependencies(check unittest.run)