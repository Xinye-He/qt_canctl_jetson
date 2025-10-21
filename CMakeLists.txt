cmake_minimum_required(VERSION 3.5)
project(qt_canctl_2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Core Gui)

set(SOURCES
    main.cpp
    mainwindow.cpp
    settingsdialog.cpp
    canmanager.cpp
)

set(HEADERS
    mainwindow.h
    settingsdialog.h
    canmanager.h
)

set(UI_FILES
    mainwindow.ui
    settingsdialog.ui
)

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)
