# Art for the overlays (party and battle HUDs, touch controls, minimap): raw RGBA images cut from the owner's HUD asset sheet and embedded in the executable.
# hud_frame.rgba is 480x205, hud_coin.rgba 64x64, touch_dpad.rgba 256x256, touch_face.rgba 128x128, touch_shoulder_l.rgba and
# touch_shoulder_r.rgba 256x96, touch_pill.rgba 192x64, minimap_frame.rgba 343x318,
# battle_card.rgba 512x102, battle_enemy.rgba 512x66 (straight alpha, red in the first byte).
set(DW3_HUD_ART_HEADERS)
foreach(art hud_frame hud_coin touch_dpad touch_face touch_shoulder_l touch_shoulder_r touch_pill minimap_frame battle_card battle_enemy)
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
