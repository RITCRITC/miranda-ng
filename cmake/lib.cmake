add_library(${TARGET} SHARED ${SOURCES})
set_target_properties(${TARGET} PROPERTIES
	LINK_FLAGS "/SUBSYSTEM:WINDOWS"
	SUFFIX ".mir"
	RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/$<CONFIG>/Libs"
)
target_link_libraries(${TARGET} ${COMMON_LIBS})