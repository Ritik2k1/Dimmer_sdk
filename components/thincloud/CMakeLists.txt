
set(COMPONENT_SRCS "thincloud.c"
                    "mesh_thincloud.c")

set(COMPONENT_ADD_INCLUDEDIRS "include")

# requirements can't depend on config
set(COMPONENT_REQUIRES mcommon mconfig mwifi mlink mqtt utilis)

register_component()
