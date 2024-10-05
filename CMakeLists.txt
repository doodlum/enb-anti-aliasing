cmake_minimum_required(VERSION 3.21)

project(
	ENBAntiAliasing
	VERSION 1.2.0
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# #######################################################################################################################
# # Add CMake features
# #######################################################################################################################
include(XSEPlugin)

# #######################################################################################################################
# # Find dependencies
# #######################################################################################################################
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(xbyak CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
add_subdirectory(${CMAKE_SOURCE_DIR}/cmake/Streamline)
include(FidelityFX-SDK)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	${CLIB_UTIL_INCLUDE_DIRS}
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	magic_enum::magic_enum
	xbyak::xbyak
	Microsoft::DirectXTK
	Microsoft::DirectXTex
	Streamline
)

# https://gitlab.kitware.com/cmake/cmake/-/issues/24922#note_1371990
if(MSVC_VERSION GREATER_EQUAL 1936 AND MSVC_IDE) # 17.6+
	# When using /std:c++latest, "Build ISO C++23 Standard Library Modules" defaults to "Yes".
	# Default to "No" instead.
	#
	# As of CMake 3.26.4, there isn't a way to control this property
	# (https://gitlab.kitware.com/cmake/cmake/-/issues/24922),
	# We'll use the MSBuild project system instead
	# (https://learn.microsoft.com/en-us/cpp/build/reference/vcxproj-file-structure)
	file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/Directory.Build.props" CONTENT [==[
<Project>
  <ItemDefinitionGroup>
    <ClCompile>
      <BuildStlModules>false</BuildStlModules>
    </ClCompile>
  </ItemDefinitionGroup>
</Project>
]==] @ONLY)
endif()

# #######################################################################################################################
# # clang-format
# #######################################################################################################################

find_program(CLANG_FORMAT_PATH clang-format)
if(CLANG_FORMAT_PATH)
	add_custom_target(FORMAT_CODE
		COMMAND ${CLANG_FORMAT_PATH} -i -style=file ${CPP_SOURCES};${HLSL_FILES}
		COMMENT "Running clang format for cpp and hlsl files"
	)
endif()

