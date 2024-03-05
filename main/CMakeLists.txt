idf_component_register(SRCS "youtube_sub.c"
                    INCLUDE_DIRS ".")

list(APPEND EXTRA_COMPONENT_DIRS components)

set(COMPONENT_REQUIRES
    esp_https_server
)
