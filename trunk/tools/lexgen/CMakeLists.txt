# CMake build file for tartx/lexgen

include(AddTartLibrary)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src) # Source file root
set(TEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/test) # Test source root
set(TARTC_OPTIONS -g -debug-errors)
set(TARTLN_OPTIONS -disable-fp-elim -internalize -O0)

# Module search path
set(TART_MODULE_PATH
    -i ${TART_SOURCE_DIR}/stdlib
    -i ${TART_SOURCE_DIR}/libtesting
    -i ${SRC_ROOT}
    -i ${TEST_ROOT})

# Input files
file(GLOB LIB_LEXGEN_COMMON_SRC RELATIVE ${SRC_ROOT}  ${SRC_ROOT}/tartx/lexgen/common/*.tart)
file(GLOB LIB_LEXGEN_GEN_SRC    RELATIVE ${SRC_ROOT}  ${SRC_ROOT}/tartx/lexgen/gen/*.tart)
file(GLOB LIB_LEXGEN_APP_SRC    RELATIVE ${SRC_ROOT}  ${SRC_ROOT}/tartx/lexgen/app/*.tart)
file(GLOB LIB_LEXGEN_TEST_SRC   RELATIVE ${TEST_ROOT} ${TEST_ROOT}/*.tart)

add_tart_library(LexGenCommon "${SRC_ROOT}" "${LIB_LEXGEN_COMMON_SRC}")
add_tart_library(LexGenGen "${SRC_ROOT}" "${LIB_LEXGEN_GEN_SRC}")
add_tart_library(LexGenApp "${SRC_ROOT}" "${LIB_LEXGEN_APP_SRC}")
add_tart_library(LexGenTest "${TEST_ROOT}" "${LIB_LEXGEN_TEST_SRC}")

function(add_tart_executable Name)
  set(ASM_FILE ${Name}.s)
  set(EXE_FILE ${Name})
  set(LIBCORE "${PROJECT_BINARY_DIR}/stdlib/stdlib.bc")
  set(DEPS)
  foreach(DEP ${ARGN})
    set(DEPS ${DEPS} "${DEP}")
  endforeach(DEP)
  foreach(DEP ${DEPS})
    message(${DEP})
  endforeach(DEP)

  # Link
  add_custom_command(OUTPUT ${ASM_FILE}
      COMMAND tartln -filetype=asm -o ${ASM_FILE} ${TARTLN_OPTIONS} ${DEPS} ${LIBCORE}
      
      DEPENDS ${DEPS} stdlib
      COMMENT "Linking Tart bitcode file ${ASM_FILE}")

  # Assemble
  #add_executable(${EXE_FILE} ${ASM_FILE})
  
  # Assemble
  add_custom_target("${Name}"
      DEPENDS "${ASM_FILE}" runtime)
  add_custom_command(TARGET "${Name}"
      COMMAND gcc -g -fno-omit-framepointer -c -O0 -x assembler ${ASM_FILE} -o ${EXE_FILE}.o
      COMMAND gcc -o ${EXE_FILE} -L${TART_BINARY_DIR}/runtime ${EXE_FILE}.o -lruntime -ldl
      COMMENT "Assembling ${ASM_FILE}")
endfunction(add_tart_executable)

add_tart_executable(lexGenTest
  "${PROJECT_BINARY_DIR}/libtesting/libtesting.bc"
  libLexGenCommon.bc
  libLexGenGen.bc 
  libLexGenTest.bc 
)