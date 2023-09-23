
set(COMPONENT_SRCS "button.c")

set(COMPONENT_ADD_INCLUDEDIRS "include")

# requirements can't depend on config
set(COMPONENT_REQUIRES mconfig)

register_component()
