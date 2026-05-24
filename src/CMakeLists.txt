add_executable(wave-server
    main.cpp
    util/arg_parser.cpp
    util/network.cpp
    app/app_state.cpp
    app/gesture_repository.cpp
    app/api_controller.cpp
    app/ws_controller.cpp
)

if(WAVE_BUILD_NCNN)
    target_compile_definitions(wave-server PRIVATE WAVE_BUILD_NCNN=1)
    target_sources(wave-server PRIVATE
        device/inference_engine.cpp
        device/retina.cpp
        device/sensor_pipeline.cpp
        device/gesture_set_catalog.cpp
        device/gesture_output_stabilizer.cpp
    )
endif()

set_target_properties(wave-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
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

if(WAVE_BUILD_NCNN AND TARGET ncnn)
    target_link_libraries(wave-server PRIVATE ncnn)
    target_link_libraries(wave-server PRIVATE wave_asio)
endif()
