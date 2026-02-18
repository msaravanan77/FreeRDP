# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP Clients
#
# Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Clients

include(CMakeDependentOption)
include(InstallFreeRDPDesktop)

if(WITH_CLIENT_COMMON)
  add_subdirectory(common)
endif()

if(WITH_FUSE)
  freerdp_client_pc_add_requires_private("fuse3")
endif()

freerdp_client_pc_add_requires_private("freerdp${FREERDP_API_VERSION}")
freerdp_client_pc_add_library_private("dl;pthread")

list(REMOVE_DUPLICATES FREERDP_CLIENT_PC_REQUIRES_PRIVATE)
list(JOIN FREERDP_CLIENT_PC_REQUIRES_PRIVATE " " FREERDP_CLIENT_PC_REQUIRES_PRIVATE)

list(REMOVE_DUPLICATES FREERDP_CLIENT_PC_LIBRARY_PRIVATE)
list(JOIN FREERDP_CLIENT_PC_LIBRARY_PRIVATE " -l" FREERDP_CLIENT_PC_LIBRARY_PRIVATE)
if(FREERDP_CLIENT_PC_LIBRARY_PRIVATE)
  string(PREPEND FREERDP_CLIENT_PC_LIBRARY_PRIVATE "-l")
endif()

# Do not set Requires.Private if not a static build
if(BUILD_SHARED_LIBS)
  set(FREERDP_CLIENT_PC_REQUIRES_PRIVATE "")
  set(FREERDP_CLIENT_PC_LIBRARY_PRIVATE "")
endif()
set(FREERDP_CLIENT_PC_REQUIRES freerdp${FREERDP_API_VERSION})

include(pkg-config-install-prefix)
cleaning_configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/freerdp-client.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/freerdp-client${FREERDP_VERSION_MAJOR}.pc @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/freerdp-client${FREERDP_VERSION_MAJOR}.pc
        DESTINATION ${PKG_CONFIG_PC_INSTALL_DIR}
)

export(PACKAGE freerdp-client)

setfreerdpcmakeinstalldir(FREERDP_CLIENT_CMAKE_INSTALL_DIR "FreeRDP-Client${FREERDP_VERSION_MAJOR}")

configure_package_config_file(
  FreeRDP-ClientConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/FreeRDP-ClientConfig.cmake
  INSTALL_DESTINATION ${FREERDP_CLIENT_CMAKE_INSTALL_DIR} PATH_VARS FREERDP_INCLUDE_DIR
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/FreeRDP-ClientConfigVersion.cmake VERSION ${FREERDP_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/FreeRDP-ClientConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/FreeRDP-ClientConfigVersion.cmake
        DESTINATION ${FREERDP_CLIENT_CMAKE_INSTALL_DIR}
)

install(EXPORT FreeRDP-ClientTargets DESTINATION ${FREERDP_CLIENT_CMAKE_INSTALL_DIR})
