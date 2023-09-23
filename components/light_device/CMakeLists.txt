
set(COMPONENT_SRCS "light_handle.c"")

set(COMPONENT_ADD_INCLUDEDIRS "include")

add_definitions(-fgnu89-inline)

# requirements can't depend on config
set(COMPONENT_REQUIRES mcommon mconfig mwifi mlink mqtt utilis mesh_utils )

register_component()
