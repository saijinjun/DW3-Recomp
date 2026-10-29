# Art for the overlays (party HUD): raw RGBA images cut from the owner's HUD asset sheet and embedded in the executable.
# hud_frame.rgba is 480x205, hud_coin.rgba 64x64 (straight alpha, red in the first byte).
set(DW3_HUD_ART_HEADERS)
foreach(art hud_frame hud_coin)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${art}_data.h)
    add_custom_command(
        OUTPUT ${header}
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${art}.rgba -DOUTPUT=${header} -DSYMBOL=${art}
                -DNAMESPACE=hud_art -P ${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${art}.rgba ${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake
        COMMENT "Embedding ${art}"
        VERBATIM)
    list(APPEND DW3_HUD_ART_HEADERS ${header})
endforeach()

add_library(dw3hudart STATIC hud_art.cpp ${DW3_HUD_ART_HEADERS})
target_include_directories(dw3hudart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
dw3_target_defaults(dw3hudart)
