add_executable(unit_tests
    unit_tests/cpu_test.cpp
    unit_tests/main.cpp
    unit_tests/ppu_crt_scan_test.cpp
    unit_tests/ppu_name_table_test.cpp
    unit_tests/ppu_palette_table_test.cpp
    unit_tests/ppu_test.cpp
    unit_tests/mmc1_test.cpp
    unit_tests/ppu_oam_test.cpp
    unit_tests/bus_test.cpp
    unit_tests/ppu_registers_test.cpp
    unit_tests/ppu_scroll_test.cpp
)

target_link_libraries(unit_tests
    libnes
    Catch2::Catch2WithMain
)

catch_discover_tests(unit_tests)

add_executable(integration_tests
    integration_tests/nestest.cpp
    integration_tests/ppu_vbl_nmi.cpp
)

target_link_libraries(integration_tests
    libnes
    Catch2::Catch2WithMain
)

add_custom_command(
    TARGET integration_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/rom"
    "${CMAKE_CURRENT_BINARY_DIR}/rom"
)

catch_discover_tests(integration_tests)