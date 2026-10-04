# 本工程是官方 deepin-music 的「在线歌词增强版」，但以**独立应用**身份发布，
# 避免与系统自带 deepin-music 抢包名/文件路径：二进制 / 桌面 / 图标 / 翻译目录 /
# dconfig schema 全部使用 deepin-music-online-lyric 命名空间，与官方零文件冲突。
# 显示名：Deepin音乐-V2。project(deepin-music) 与 deepin-music_*.ts/qrc 收集名保持不变
# （翻译 .qm 基名仍为 deepin-music_，打包时复制为 deepin-music-online-lyric_*.qm 供 DTK 查找）。
set(PLUGIN_ID "deepin-music-online-lyric")

set(APP_BIN_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/bin/)
set(BIN_NAME ${CMAKE_PROJECT_NAME})
list(APPEND RESOURCES ${CMAKE_PROJECT_NAME}.qrc)
list(APPEND RESOURCES dci.qrc)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find the Qt6Quick library
find_package(Dtk6Declarative REQUIRED)
find_package(Qt6 COMPONENTS Core Widgets Quick LinguistTools DBus REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(DTK REQUIRED IMPORTED_TARGET dtk6core)
pkg_check_modules(DTK REQUIRED IMPORTED_TARGET dtk6gui)

find_package(Qt6 COMPONENTS Svg REQUIRED)

qt6_add_resources(RCC_SOURCES ${RESOURCES})

# Windows resource file for application icon
if(WIN32)
    set(WIN_RESOURCES appicon.rc)
endif()

# 保证 src 目录下头文件全局可见
include_directories(src)
include_directories(../libdmusic)
include_directories(../util)

aux_source_directory(./ ALLSOURCE)
aux_source_directory(./effect ALLSOURCE)
aux_source_directory(./util ALLSOURCE)

LINK_DIRECTORIES(${CMAKE_BINARY_DIR}/src/libdmusic)

file(GLOB TS LIST_DIRECTORIES false ${PROJECT_SOURCE_DIR}/translations/${BIN_NAME}*.ts)
set_source_files_properties(${TS}
    PROPERTIES OUTPUT_LOCATION ${PROJECT_SOURCE_DIR}/translations)
set(translate ${PROJECT_SOURCE_DIR}/translations)

if (NOT (${CMAKE_BUILD_TYPE} MATCHES "Debug"))
    qt6_add_translation(QM_FILES ${TS})
else()
    qt6_create_translation(QM_FILES ${CMAKE_CURRENT_SOURCE_DIR} ${TS} OPTIONS -source-language en_US -no-obsolete)
endif ()

# Add EXE
if(WIN32)
    add_executable(${BIN_NAME} WIN32 ${RCC_SOURCES} ${ALLSOURCE} ${QM_FILES} ${WIN_RESOURCES})
else()
    add_executable(${BIN_NAME} ${RCC_SOURCES} ${ALLSOURCE} ${QM_FILES})
endif()

file(GLOB_RECURSE APP_QML_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qml)
# AlbumSublist.qml was intentionally absent from the legacy QRC and is not loaded at runtime.
list(REMOVE_ITEM APP_QML_FILES musicsublist/AlbumSublist.qml)
if (Qt6_VERSION VERSION_GREATER_EQUAL "6.8.0")
    qt_policy(SET QTP0004 NEW)
endif()
qt_add_qml_module(${BIN_NAME}
    URI org.deepin.music
    VERSION 1.0
    RESOURCE_PREFIX /
    NO_RESOURCE_TARGET_PATH
    QML_FILES ${APP_QML_FILES})

# Use target_link_libraries instead of qt6_use_modules
target_link_libraries(${BIN_NAME} PRIVATE Qt6::Core)

set(TARGET_LIBS Qt6::Quick PkgConfig::DTK dmusic ${Dtk6Declarative_LIBRARIES} Dtk6::Gui)

target_link_libraries(${BIN_NAME} PRIVATE ${TARGET_LIBS})

# install dconfig meta files
# APPID 必须与 QML 侧 SettingsConfig.qml 的 `name:` 一致，否则 dconf 读写会落空
dtk_add_config_meta_files(APPID org.deepin.music.online.lyric FILES data/org.deepin.music.online.lyric.json)

# Add install rule
# OUTPUT_NAME 而非 install(TARGETS ... RENAME)：后者在本机 CMake 3.31.4 +
# qt_add_qml_module 生成的 install 规则组合下会被忽略（实测产物仍叫 deepin-music）。
# OUTPUT_NAME 在 configure 阶段就改掉产物名，install 规则随之生效。
# target 名 ${BIN_NAME} 不变，故 qmlcache / qmldir / 资源前缀等仍按原名匹配。
set_target_properties(${BIN_NAME} PROPERTIES OUTPUT_NAME ${PLUGIN_ID})
install(TARGETS ${BIN_NAME} DESTINATION ${APP_BIN_INSTALL_DIR})
# install Translation files
install(DIRECTORY ${PROJECT_SOURCE_DIR}/translations DESTINATION share/${PLUGIN_ID} FILES_MATCHING PATTERN "*.qm")
# install Desktop icon
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/${PLUGIN_ID}.desktop DESTINATION share/applications)
# install Manual App icon
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data/${PLUGIN_ID}.svg DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/assets/deepin-music-online-lyric DESTINATION ${CMAKE_INSTALL_PREFIX}/share/deepin-manual/manual-assets/application/)
