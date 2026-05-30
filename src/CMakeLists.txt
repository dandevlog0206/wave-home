set(WAVE_RUNTIME_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Directory for built wave-server binaries")
set(WAVE_RUNTIME_OUTPUT_NAME "wave-server" CACHE STRING "Runtime filename for the wave-server executable")

add_executable(wave-server
    main.cpp
    core/appliance_config_store.cpp
    core/locale.cpp
    core/server_state_store.cpp
    util/arg_parser.cpp
    util/cpu_sampler.cpp
    device/network.cpp
    device/inference_engine.cpp
    device/retina.cpp
    device/sensor_pipeline.cpp
    device/gesture_set_catalog.cpp
    device/gesture_probability_gate.cpp
    app/app_state.cpp
    app/gesture_repository.cpp
    app/api_controller.cpp
    app/ws_controller.cpp
    appliance/appliance.cpp
    appliance/mqtt_transport.cpp
    appliance/appliance_manager.cpp
    appliance/tizen_appliance.cpp
    appliance/tizen_transport.cpp
    appliance/tuya_appliance.cpp
    appliance/tuya_protocol.cpp
    appliance/tuya_transport.cpp
)

set_target_properties(wave-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${WAVE_RUNTIME_OUTPUT_DIR}"
    OUTPUT_NAME "${WAVE_RUNTIME_OUTPUT_NAME}"
)

target_compile_definitions(wave-server PRIVATE
    WAVE_SITE_ROOT="${CMAKE_SOURCE_DIR}/site"
    WAVE_GESTURE_SET_ROOT="${CMAKE_SOURCE_DIR}/gesture_set"
)

target_include_directories(wave-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/thirdparty/json/include"
)

target_link_libraries(wave-server PRIVATE Drogon::Drogon)
target_link_libraries(wave-server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(wave-server PRIVATE wave_asio)

find_package(OpenSSL REQUIRED)
target_link_libraries(wave-server PRIVATE OpenSSL::SSL OpenSSL::Crypto)

if(WAVE_BUILD_NCNN AND TARGET ncnn)
    target_link_libraries(wave-server PRIVATE ncnn)
endif()
