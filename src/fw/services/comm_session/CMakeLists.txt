# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

# The Pebble protocol endpoint table: which endpoint id goes to which
# handler, on which task.
set(endpoints ${CMAKE_CURRENT_BINARY_DIR}/protocol_endpoints_table.auto.h)
# Without the normal services, only the endpoints every firmware carries
# can be wired up: the rest have no handler to point at.
if(CONFIG_SERVICE_SERVICES_NORMAL)
  set(core_only "")
else()
  set(core_only --core-only)
endif()
add_custom_command(
  OUTPUT ${endpoints}
  COMMAND ${PBL_TOOLCHAIN_ENV} ${PYTHON_EXECUTABLE} ${PBL_BASE}/tools/cmake/generate.py endpoints-table
          --input ${CMAKE_CURRENT_SOURCE_DIR}/protocol_endpoints_table.json
          --output ${endpoints} ${core_only}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocol_endpoints_table.json
  WORKING_DIRECTORY ${PBL_BASE}
  COMMENT "Generating the protocol endpoint table"
  VERBATIM
)
add_custom_target(pbl_endpoints_table DEPENDS ${endpoints})
add_dependencies(pbl_generated_headers pbl_endpoints_table)

set(sources
  default_kernel_receiver.c
  default_kernel_sender.c
  meta_endpoint.c
  session.c
  session_analytics.c
  session_receive_router.c
  session_remote_version.c
  session_send_queue.c
)
if(NOT CONFIG_RECOVERY_FW)
  list(APPEND sources app_session_capabilities.c)
endif()

pbl_library()
pbl_library_sources(${sources})
