# 在线歌词运行时联网验证工具（默认不参与主构建）
#
# 该目标仅在根 CMakeLists 传入 -DBUILD_LYRIC_RUNTIME_CHECK=ON 时才加入，
# 避免影响正常构建/打包。它链接已构建的 dmusic 库，驱动真实网络请求。

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(PkgConfig REQUIRED)

add_executable(lyric_runtime_check lyric_runtime_check.cpp)

target_link_libraries(lyric_runtime_check PRIVATE
    Qt6::Core
    Qt6::Network
    dmusic
)

# 头文件路径与主库一致
target_include_directories(lyric_runtime_check PRIVATE
    ${CMAKE_SOURCE_DIR}/src/libdmusic
    ${CMAKE_SOURCE_DIR}/src/libdmusic/core
    ${CMAKE_SOURCE_DIR}/src/libdmusic/util
)
