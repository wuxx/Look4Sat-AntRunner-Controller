#set(COMPONENT_SRCS "main.c")

#register_component()

idf_component_register(SRCS "main.c"
                    EMBED_TXTFILES web/config.html
                    INCLUDE_DIRS ".")
