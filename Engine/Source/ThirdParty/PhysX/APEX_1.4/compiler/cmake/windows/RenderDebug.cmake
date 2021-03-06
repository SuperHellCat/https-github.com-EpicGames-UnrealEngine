#
# Build RenderDebug
#

SET(GW_DEPS_ROOT $ENV{GW_DEPS_ROOT})
FIND_PACKAGE(PxShared REQUIRED)

SET(APEX_MODULE_DIR ${PROJECT_SOURCE_DIR}/../../../module)

SET(RD_SOURCE_DIR ${PROJECT_SOURCE_DIR}/../../../shared/general/RenderDebug)
	
SET(RENDERDEBUG_PLATFORM_INCLUDES

	${PXSHARED_ROOT_DIR}/include/cudamanager
)

SET(RENDERDEBUG_COMPILE_DEFS
	# Common to all configurations

	PX_FOUNDATION_DLL=0;${APEX_WINDOWS_COMPILE_DEFS}

	$<$<CONFIG:debug>:${APEX_WINDOWS_DEBUG_COMPILE_DEFS};>
	$<$<CONFIG:checked>:${APEX_WINDOWS_CHECKED_COMPILE_DEFS};>
	$<$<CONFIG:profile>:${APEX_WINDOWS_PROFILE_COMPILE_DEFS};>
	$<$<CONFIG:release>:${APEX_WINDOWS_RELEASE_COMPILE_DEFS};>
)

# include common RenderDebug.cmake
INCLUDE(../common/RenderDebug.cmake)
