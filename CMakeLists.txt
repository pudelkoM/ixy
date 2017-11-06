cmake_minimum_required(VERSION 2.8)
project(ixy)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

add_compile_options(-march=native
	-D_DEFAULT_SOURCE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Wno-unused-function
	-Wformat=2
)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -fomit-frame-pointer")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -fomit-frame-pointer")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O1 -g -fsanitize=address -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O1 -g -fsanitize=address -fno-omit-frame-pointer")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SOURCE_COMMON src/pci.c src/memory.c src/stats.c src/driver/ixgbe.c src/driver/device.hpp src/driver/ixgbe.cpp src/driver/ixgbe.hpp src/stats.cpp src/stats.hpp src/libseccomp_init.c)

set(SOURCE_ALLOCATOR
		src/allocator/allocator.h
		src/allocator/allocator_common.h
		src/allocator/stack_allocator.c
		src/allocator/mallocator.c
		src/allocator/fallback_allocator.c
		src/allocator/null_allocator.c
		src/allocator/spinlock_stack_allocator.c
		src/allocator/rte_spinlock.h
		src/allocator/rte_per_lcore.h
		src/allocator/dma_allocator.c)

add_executable(ixy-pktgen src/app/ixy-pktgen.c ${SOURCE_COMMON})
target_link_libraries(ixy-pktgen "seccomp")
add_executable(ixy-fwd src/app/ixy-fwd.c ${SOURCE_COMMON})
target_link_libraries(ixy-fwd "seccomp")
add_executable(ixy-cpp-fwd src/app/ixy-cpp-fwd.cpp ${SOURCE_COMMON})
target_link_libraries(ixy-cpp-fwd "seccomp")

enable_testing()
add_executable(allocator-example src/app/allocator-example.c ${SOURCE_ALLOCATOR})
add_executable(spinlock-test src/allocator/tests/spinlock_stack_allocator.cpp ${SOURCE_ALLOCATOR})
target_link_libraries(spinlock-test pthread)
add_test(NAME spinlock-test COMMAND spinlock-test)
