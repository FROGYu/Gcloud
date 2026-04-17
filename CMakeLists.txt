cmake_minimum_required(VERSION 3.12)

project(Gnet LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 外部动态库配置。nlohmann/json 和 cpp-httplib 是头文件库，zstd 和 libevent 需要链接系统库。
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

# 项目公共头文件路径。nlohmann/json 和 cpp-httplib 都是单头文件库，只需要加入 include 路径。
set(GCLOUD_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp_httplib
)

# 统一给项目内目标挂载公共 include，避免每个 add_executable 后重复写一遍。
function(gcloud_configure_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${GCLOUD_INCLUDE_DIRS}
    )
endfunction()

# 统一给需要线程支持的目标挂载线程库。
function(gcloud_link_thread_runtime target_name)
    target_link_libraries(${target_name} PRIVATE
        Threads::Threads
    )
endfunction()

# 统一给服务端目标挂载 zstd 和 libevent。
function(gcloud_link_server_runtime target_name)
    target_include_directories(${target_name} PRIVATE
        ${ZSTD_INCLUDE_DIRS}
    )
    target_link_libraries(${target_name} PRIVATE
        ${ZSTD_LIBRARIES}
        event
    )
endfunction()

# 服务端主程序入口。
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Net/Server/ServerMain.cpp
    src/Net/Client/ClientMain.cpp
)
gcloud_configure_target(${PROJECT_NAME})
gcloud_link_thread_runtime(${PROJECT_NAME})
gcloud_link_server_runtime(${PROJECT_NAME})

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
