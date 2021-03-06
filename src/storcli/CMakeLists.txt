# Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
# This file is part of Rozofs.
#
# Rozofs is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation, version 2.
#
# Rozofs is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

add_definitions(-DD_REENTRANT -D_FILE_OFFSET_BITS=64)
include_directories(${CMAKE_BINARY_DIR}) # for config.h
include_directories(${CMAKE_SOURCE_DIR}) # for librozofs headers
include_directories(${PTHREAD_INCLUDE_DIR})
link_directories(${CMAKE_BINARY_DIR}/rozofs)

add_executable(storcli
  rozofs_storcli.h                      
  rozofs_storcli_transform.h
  rozofs_storcli_lbg_cnf_supervision.h  
  storage_proto.h
  rozofs_storcli_rpc.h                  
  storcli_main.h
  rozofs_storcli_send_new.c
  rozofs_storcli_lbg_cnf_supervision.c  
  rozofs_storcli_transform.c
  rozofs_storcli_mgt.c                  
  rozofs_storcli_write.c
  rozofs_storcli_nblock_init.c          
  storcli_main.c
  rozofs_storcli_north_intf.c           
  storcli_stub.c
  rozofs_storcli_read.c                 
  rozofs_storcli_rebuild_prj.c          
  storage_lbg.c  
  rozofs_storcli_cnx_supervision.c
)
#pthread need by fuse
target_link_libraries(storcli rozofs ${PTHREAD_LIBRARY} ${CRYPT_LIBRARY})

# Install
install(TARGETS storcli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    COMPONENT storcli
)
