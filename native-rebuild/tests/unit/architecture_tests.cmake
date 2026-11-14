find_program(LUME_ARCHITECTURE_NODE NAMES node node.exe)
if(LUME_ARCHITECTURE_NODE)
  add_test(NAME lume-source-boundaries-test
    COMMAND "${LUME_ARCHITECTURE_NODE}" "${PROJECT_SOURCE_DIR}/tests/scripts/source-boundaries-test.mjs")
  set_tests_properties(lume-source-boundaries-test PROPERTIES TIMEOUT 15 LABELS "isolated;architecture")
endif()
