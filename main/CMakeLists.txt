idf_component_register(
    SRCS
        "main.c"
        "waveshare_rgb_lcd_port.c"
        "lvgl_port.c"
        "ui/ui.cpp"
        "ui/assets.cpp"
        "ui/theme.cpp"
        "ui/dashboard.cpp"
        "ui/inverter_page.cpp"
        "ui/startup.cpp"
        "ui/widgets/value_label.cpp"
        "ui/widgets/flow_dot.cpp"
        "core/energy_model.cpp"
        "drivers/sd_card.cpp"
        "drivers/deye_rs485.cpp"
        "drivers/jk_bms.cpp"
    INCLUDE_DIRS
        "."
        "ui"
        "core"
        "drivers"
        "app"
        "ui/widgets"
)

idf_component_get_property(lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${lvgl_lib} PRIVATE -Wno-format)
