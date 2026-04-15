cmake_minimum_required(VERSION 3.16)
project(SpatialMind VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── GDAL（OSGeo4W 手动导入，不用 find_package）──
add_library(GDAL::GDAL UNKNOWN IMPORTED)
set_target_properties(GDAL::GDAL PROPERTIES
    IMPORTED_LOCATION "C:/OSGeo4W/apps/gdal-dev/lib/gdal_i.lib"
    INTERFACE_INCLUDE_DIRECTORIES "C:/OSGeo4W/apps/gdal-dev/include"
)

# ── Qt 模块（不再需要 libpqxx，Qt::Sql 负责数据库）──
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS
    Widgets Network WebEngineWidgets WebChannel Sql Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Widgets Network WebEngineWidgets WebChannel Sql Concurrent)

# ── 源文件 ──
set(PROJECT_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MainWindow.ui
    src/MapBridge.h    src/MapBridge.cpp
    src/LLMClient.h   src/LLMClient.cpp
    src/GISEngine.h   src/GISEngine.cpp
    src/QueryHistory.h src/QueryHistory.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(SpatialMind
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(SpatialMind ${PROJECT_SOURCES}
        resources.qrc
        src/ConversationManager.h src/ConversationManager.cpp
        src/SQLCache.h src/SQLCache.cpp
        src/SettingsDialog.h src/SettingsDialog.cpp)
endif()

# ── 链接库（完全移除 libpqxx 相关）──
target_link_libraries(SpatialMind PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel
    Qt${QT_VERSION_MAJOR}::Sql
    Qt${QT_VERSION_MAJOR}::Concurrent
    GDAL::GDAL
)

# ── 运行时让程序找到 libpq.dll（OSGeo4W 里的）──
if(WIN32)
    target_compile_definitions(SpatialMind PRIVATE
        LIBPQ_BIN_DIR="C:/OSGeo4W/bin"
    )
endif()

if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.SpatialMind)
endif()
set_target_properties(SpatialMind PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS SpatialMind
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 强制 MSVC 以 UTF-8 解析源文件和输出
if(MSVC)
    target_compile_options(SpatialMind PRIVATE /utf-8)
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(SpatialMind)
endif()
