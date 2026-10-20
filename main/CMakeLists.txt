# Select the source file with app_main
set(MAIN_SRC "main-4.c")

idf_component_register(SRCS "${MAIN_SRC}"
                    INCLUDE_DIRS ".")
