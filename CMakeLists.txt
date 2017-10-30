cmake_minimum_required(VERSION 2.8)
project(ixy)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

add_compile_options(-g -O2 -march=native -fomit-frame-pointer
	-D_DEFAULT_SOURCE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Wno-unused-function
	-Wformat=2
)


include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SOURCE_COMMON src/pci.c src/memory.c src/stats.c src/driver/ixgbe.c src/driver/device.hpp src/driver/ixgbe.cpp src/driver/ixgbe.hpp src/stats.cpp src/stats.hpp src/libseccomp_init.c)

add_executable(ixy-pktgen src/app/ixy-pktgen.c ${SOURCE_COMMON})
target_link_libraries(ixy-pktgen "seccomp")
add_executable(ixy-fwd src/app/ixy-fwd.c ${SOURCE_COMMON})
target_link_libraries(ixy-fwd "seccomp")
add_executable(ixy-cpp-fwd src/app/ixy-cpp-fwd.cpp ${SOURCE_COMMON})
target_link_libraries(ixy-cpp-fwd "seccomp")

