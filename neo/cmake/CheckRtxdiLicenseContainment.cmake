# Configure-time containment check for the RTXDI license remediation lane.
#
# This does not remove the current external RTXDI dependency. It only prevents
# accidental expansion while rbdoom-owned replacement modules are being built.

if(NOT GIT_FOUND)
	find_package(Git QUIET)
endif()

get_filename_component(RBDOOM_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(RTXDI_LICENSE_INVENTORY "${RBDOOM_REPO_ROOT}/docs/rtxdi_license_remediation/impacted_files.txt")

if(NOT GIT_FOUND)
	message(WARNING "Git not found; skipping RTXDI license containment check")
	return()
endif()

if(NOT EXISTS "${RTXDI_LICENSE_INVENTORY}")
	message(FATAL_ERROR "RTXDI license containment inventory is missing: ${RTXDI_LICENSE_INVENTORY}")
endif()

execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${RBDOOM_REPO_ROOT}" ls-files
	OUTPUT_VARIABLE RTXDI_TRACKED_FILES
	RESULT_VARIABLE RTXDI_GIT_RESULT
	OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT RTXDI_GIT_RESULT EQUAL 0)
	message(WARNING "git ls-files failed; skipping RTXDI license containment check")
	return()
endif()

file(READ "${RTXDI_LICENSE_INVENTORY}" RTXDI_LICENSE_INVENTORY_TEXT)
string(REPLACE "\r\n" "\n" RTXDI_TRACKED_FILES "${RTXDI_TRACKED_FILES}")
string(REPLACE "\r" "\n" RTXDI_TRACKED_FILES "${RTXDI_TRACKED_FILES}")
string(REPLACE "\n" ";" RTXDI_TRACKED_FILES "${RTXDI_TRACKED_FILES}")

set(RTXDI_TEXT_FILE_REGEX "(CMakeLists\\.txt|\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|hlsl|hlsli|cmake|txt|md|ps1|py|sh|json|xml|yml|yaml))$")
set(RTXDI_INCLUDE_FILE_REGEX "(CMakeLists\\.txt|\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|hlsl|hlsli|cmake))$")

foreach(RTXDI_FILE IN LISTS RTXDI_TRACKED_FILES)
	if(RTXDI_FILE STREQUAL "")
		continue()
	endif()

	if(RTXDI_FILE MATCHES "^(neo/extern/|docs/rtxdi_license_remediation/)")
		continue()
	endif()

	if(RTXDI_FILE MATCHES "^(LICENSE\\.md|LICENSE_EXCEPTIONS\\.md)$")
		continue()
	endif()

	if(NOT RTXDI_FILE MATCHES "${RTXDI_TEXT_FILE_REGEX}")
		continue()
	endif()

	set(RTXDI_ABS_FILE "${RBDOOM_REPO_ROOT}/${RTXDI_FILE}")
	if(NOT EXISTS "${RTXDI_ABS_FILE}")
		continue()
	endif()

	file(READ "${RTXDI_ABS_FILE}" RTXDI_FILE_TEXT)

	if(RTXDI_FILE_TEXT MATCHES "Copyright[^\n\r]*(NVIDIA|Nvidia)" OR
	   RTXDI_FILE_TEXT MATCHES "(NVIDIA|Nvidia)[^\n\r]*Copyright" OR
	   RTXDI_FILE_TEXT MATCHES "LicenseRef-NvidiaProprietary" OR
	   RTXDI_FILE_TEXT MATCHES "RTX SDKs License")
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" -C "${RBDOOM_REPO_ROOT}" cat-file -e "HEAD:${RTXDI_FILE}"
			RESULT_VARIABLE RTXDI_FILE_IN_HEAD_RESULT
			OUTPUT_QUIET
			ERROR_QUIET)

		if(NOT RTXDI_FILE_IN_HEAD_RESULT EQUAL 0)
			message(FATAL_ERROR
				"RTXDI license containment check failed: newly tracked file ${RTXDI_FILE} contains NVIDIA proprietary license/copyright text. "
				"Do not add NVIDIA source text to rbdoom; document license status under docs/rtxdi_license_remediation instead.")
		endif()
	endif()

	if(RTXDI_FILE MATCHES "${RTXDI_INCLUDE_FILE_REGEX}" AND
	   RTXDI_FILE_TEXT MATCHES "#[ \t]*include[ \t]+[<\"]Rtxdi[\\/]")
		string(REPLACE "\\" "/" RTXDI_FILE_NORMALIZED "${RTXDI_FILE}")
		string(FIND "${RTXDI_LICENSE_INVENTORY_TEXT}" "${RTXDI_FILE_NORMALIZED}" RTXDI_FULL_PATH_INVENTORY_INDEX)
		get_filename_component(RTXDI_FILE_NAME "${RTXDI_FILE_NORMALIZED}" NAME)
		string(FIND "${RTXDI_LICENSE_INVENTORY_TEXT}" "${RTXDI_FILE_NAME}" RTXDI_FILE_NAME_INVENTORY_INDEX)

		if(RTXDI_FULL_PATH_INVENTORY_INDEX EQUAL -1 AND RTXDI_FILE_NAME_INVENTORY_INDEX EQUAL -1)
			message(FATAL_ERROR
				"RTXDI license containment check failed: ${RTXDI_FILE} includes Rtxdi/* but is not listed in "
				"docs/rtxdi_license_remediation/impacted_files.txt.")
		endif()
	endif()
endforeach()
