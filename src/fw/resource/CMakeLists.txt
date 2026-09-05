# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(excl)
if(NOT CONFIG_SERVICE_FILESYSTEM)
  list(APPEND excl
    resource_storage_file.c
  )
endif()
pbl_library()
file(GLOB sources CONFIGURE_DEPENDS *.c)
pbl_filter_sources(sources EXCLUDE ${excl})
pbl_library_sources(${sources})
