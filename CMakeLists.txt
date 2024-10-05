cmake_minimum_required(VERSION 3.26.3)

# 项目名称
project(ThreadPoolProject)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# include_directories(${PROJECT_SOURCE_DIR}/include)#添加头文件的搜索路径
# 设置源文件
set(SOURCES
    ${CMAKE_SOURCE_DIR}/ThreadPool.cpp
)

# 添加可执行文件及其相关的头文件目录
add_executable(ThreadPoolTest ${SOURCES})
# target_include_directories(ThreadPoolTest PRIVATE ${CMAKE_SOURCE_DIR})

