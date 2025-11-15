cmake_minimum_required(VERSION 3.20)
project(VenusPlus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Add subdirectories
add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(test-app)
