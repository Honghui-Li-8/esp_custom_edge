set(srcs
        "board.c")

idf_component_register(SRCS "local_edge_device.c" "ble_mesh_config_edge.c" "main.c" "${srcs}"
                    INCLUDE_DIRS  ".")
