cmake_minimum_required(VERSION 3.24)

project(MillManager VERSION 23.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Sql PrintSupport)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/uganda_mill_ai_10000.csv"
    "${CMAKE_CURRENT_BINARY_DIR}/uganda_mill_ai_10000.csv"
    COPYONLY
)

set_source_files_properties(assets/mill-manager.png PROPERTIES
    QT_RESOURCE_ALIAS "assets/mill-manager.png"
)

function(add_mill_manager target grain_profile)
    qt_add_executable(${target} WIN32 MACOSX_BUNDLE MillManagerQt.cpp)
    qt_add_resources(${target} "mill_manager_assets"
        PREFIX "/"
        FILES assets/mill-manager.png
    )
    target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::Network Qt6::Sql Qt6::PrintSupport)
    target_compile_definitions(${target} PRIVATE MILL_MANAGER_GRAIN=${grain_profile})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    install(TARGETS ${target} RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    if(WIN32)
        if(COMMAND qt_generate_deploy_app_script)
            qt_generate_deploy_app_script(
                TARGET ${target}
                OUTPUT_SCRIPT deploy_script
                NO_UNSUPPORTED_PLATFORM_ERROR
            )
            install(SCRIPT "${deploy_script}")
        else()
            message(FATAL_ERROR
                "Windows installer builds require Qt 6.3 or newer for runtime deployment.")
        endif()
    endif()
endfunction()

add_mill_manager(MillManager 0)
add_mill_manager(MillManagerF 1)
install(FILES uganda_mill_ai_10000.csv DESTINATION bin)
install(FILES assets/mill-manager.png DESTINATION share/pixmaps)
if(UNIX AND NOT APPLE)
    install(FILES
        packaging/linux/mill-manager-cpp.desktop
        packaging/linux/mill-manager-f-cpp.desktop
        DESTINATION share/applications
    )
endif()

if(BUILD_TESTING)
    add_test(NAME MillManagerCoffeeSelfTest COMMAND MillManager --self-test)
    add_test(NAME MillManagerGrainSelfTest COMMAND MillManagerF --self-test)
    if(UNIX AND NOT APPLE)
        set_tests_properties(
            MillManagerCoffeeSelfTest
            MillManagerGrainSelfTest
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        )
    endif()
endif()

if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_NAME "Mill-Manager-CPP")
    set(CPACK_PACKAGE_VENDOR "JM Group")
    set(CPACK_PACKAGE_CONTACT "mutebiauhad4@gmail.com")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Coffee and grain mill management for Windows")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Mill Manager C++ ${PROJECT_VERSION}")
    set(CPACK_PACKAGE_EXECUTABLES
        "MillManager" "Mill Manager"
        "MillManagerF" "Mill Manager F"
    )
    set(CPACK_NSIS_DISPLAY_NAME "Mill Manager C++ ${PROJECT_VERSION}")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    include(CPack)
endif()
