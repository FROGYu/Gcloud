cmake_minimum_required(VERSION 3.12)

project(Gnet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 项目公共头文件路径，nlohmann/json 是单头文件库，只需要加入 include 路径。
set(GCLOUD_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
)

# 统一给项目内目标挂载公共 include，避免每个 add_executable 后重复写一遍。
function(gcloud_configure_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${GCLOUD_INCLUDE_DIRS}
    )
endfunction()

# 主程序入口，后续云存储服务端会从这里启动。
add_executable(${PROJECT_NAME}
    src/main.cpp
)
gcloud_configure_target(${PROJECT_NAME})

# net_lab 保留网络演进实验代码：阻塞、select、epoll 服务端与客户端。
add_executable(blocking_server
    src/net_lab/blocking_server.cpp
)
gcloud_configure_target(blocking_server)

add_executable(blocking_client
    src/net_lab/blocking_client.cpp
)
gcloud_configure_target(blocking_client)

add_executable(select_server
    src/net_lab/select_server.cpp
)
gcloud_configure_target(select_server)

add_executable(epoll_server
    src/net_lab/epoll_server.cpp
)
gcloud_configure_target(epoll_server)

# 外部动态库配置。jsoncpp 先保留给旧代码，后续持久化切到 nlohmann/json 后可以删除。
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${JSONCPP_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${JSONCPP_LIBRARIES}
    event
)
