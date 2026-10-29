// On-screen touch controls: layout, finger tracking, the buttons they report and the image they draw.

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

#include "dw3/sio.h"
#include "present/touch_controls.h"

using dw3app::TouchAction;
using dw3app::TouchControls;

namespace {

constexpr int kW = 2400, kH = 1080;

const TouchControls::Control& find(TouchControls& t, const std::string& label, uint16_t bit = 0) {
    for (const auto& c : t.controls(kW, kH))
        if ((!label.empty() && c.label == label) || (bit && c.pad_bit == bit)) return c;
    ADD_FAILURE() << "no control " << label;
    static TouchControls::Control none{};
    return none;
}

}  // namespace

TEST(TouchControls, EveryControlFitsInTheWindowWithoutOverlap) {
    for (const int size : {100, 70, 130})
        for (const auto [w, h] : {std::pair{kW, kH}, std::pair{1920, 1080}}) {
    SCOPED_TRACE(testing::Message() << "size " << size << "% window " << w << "x" << h);
    TouchControls t;
    t.set_style(size, 100);
    const auto& cs = t.controls(w, h);
    ASSERT_GE(cs.size(), 14u);
    for (std::size_t i = 0; i < cs.size(); ++i) {
        EXPECT_GE(cs[i].x - cs[i].rx, 0.0f) << cs[i].label;
        EXPECT_LE(cs[i].x + cs[i].rx, static_cast<float>(w)) << cs[i].label;
        EXPECT_GE(cs[i].y - cs[i].ry, 0.0f) << cs[i].label;
        EXPECT_LE(cs[i].y + cs[i].ry, static_cast<float>(h)) << cs[i].label;
        for (std::size_t j = i + 1; j < cs.size(); ++j) {
            using Shape = TouchControls::Control::Shape;
            if (cs[i].shape == Shape::Round && cs[j].shape == Shape::Round) {
                EXPECT_GE(std::hypot(cs[i].x - cs[j].x, cs[i].y - cs[j].y), cs[i].rx + cs[j].rx) << cs[i].label << " overlaps " << cs[j].label;
                continue;
            }
            const bool apart_x = cs[i].x + cs[i].rx <= cs[j].x - cs[j].rx || cs[j].x + cs[j].rx <= cs[i].x - cs[i].rx;
            const bool apart_y = cs[i].y + cs[i].ry <= cs[j].y - cs[j].ry || cs[j].y + cs[j].ry <= cs[i].y - cs[i].ry;
            EXPECT_TRUE(apart_x || apart_y) << cs[i].label << " overlaps " << cs[j].label;
        }
    }
        }
}

TEST(TouchControls, SizeAndOpacityStyleTheControls) {
    TouchControls t;
    const float r = find(t, "cross").rx;
    const auto& img = t.image(kW, kH, 2);
    const auto alpha_at = [&](const TouchControls::Control& c) {
        return img.rgba[static_cast<std::size_t>(c.y / 2) * static_cast<std::size_t>(img.w) + static_cast<std::size_t>(c.x / 2)] >> 24;
    };
    const uint32_t full = alpha_at(find(t, "cross"));
    t.set_style(120, 50);
    EXPECT_FLOAT_EQ(find(t, "cross").rx, r * 1.2f);
    t.image(kW, kH, 2);
    EXPECT_NEAR(static_cast<double>(alpha_at(find(t, "cross"))), full * 0.5, 1.0);
}

TEST(TouchControls, FaceButtonsAndShouldersPressWhileHeld) {
    TouchControls t;
    const auto& cross = find(t, "cross");
    t.finger_down(1, cross.x, cross.y, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadCross);
    const auto& l1 = find(t, "L1");
    t.finger_down(2, l1.x, l1.y, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadCross | dw3::kPadL1);
    t.finger_up(1);
    EXPECT_EQ(t.buttons(), dw3::kPadL1);
    t.release_all();
    EXPECT_EQ(t.buttons(), 0);
}

TEST(TouchControls, DPadFollowsTheFingerIncludingDiagonals) {
    TouchControls t;
    const TouchControls::Control dpad = t.controls(kW, kH)[0];
    ASSERT_EQ(dpad.shape, TouchControls::Control::Shape::DPad);
    t.finger_down(7, dpad.x, dpad.y - dpad.rx * 0.7f, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadUp);
    t.finger_move(7, dpad.x + dpad.rx * 0.6f, dpad.y + dpad.rx * 0.6f, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadRight | dw3::kPadDown);
    t.finger_move(7, dpad.x, dpad.y, kW, kH);  // dead centre
    EXPECT_EQ(t.buttons(), 0);
    // Sliding past the edge keeps steering: the finger stays on the d-pad.
    t.finger_move(7, dpad.x - dpad.rx * 2.5f, dpad.y, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadLeft);
}

TEST(TouchControls, AFingerRollsBetweenFaceButtons) {
    TouchControls t;
    const auto& cross = find(t, "cross");
    const auto& circle = find(t, "circle");
    t.finger_down(1, cross.x, cross.y, kW, kH);
    t.finger_move(1, circle.x, circle.y, kW, kH);
    EXPECT_EQ(t.buttons(), dw3::kPadCircle);
}

TEST(TouchControls, AppButtonsReportTapsOnce) {
    TouchControls t;
    const auto& save = find(t, "SAVE");
    t.finger_down(3, save.x, save.y, kW, kH);
    EXPECT_EQ(t.buttons(), 0);
    auto actions = t.take_actions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0], TouchAction::SaveState);
    EXPECT_TRUE(t.take_actions().empty());
}

TEST(TouchControls, EncountersButtonCyclesAndLights) {
    TouchControls t;
    const auto& enc = find(t, "ENC");
    t.finger_down(4, enc.x, enc.y, kW, kH);
    t.finger_up(4);
    const auto actions = t.take_actions();
    ASSERT_EQ(actions.size(), 1u);
    EXPECT_EQ(actions[0], TouchAction::Encounters);
    t.image(kW, kH, 2);
    const uint64_t serial = t.serial();
    t.set_encounters_off(true);
    t.image(kW, kH, 2);
    EXPECT_NE(t.serial(), serial);
}

TEST(TouchControls, HiddenControlsHoldNothingAndATouchShowsThem) {
    TouchControls t;
    const auto& cross = find(t, "cross");
    t.finger_down(1, cross.x, cross.y, kW, kH);
    t.set_visible(false);
    EXPECT_EQ(t.buttons(), 0);
    t.finger_down(2, cross.x, cross.y, kW, kH);
    EXPECT_TRUE(t.visible());
    EXPECT_EQ(t.buttons(), dw3::kPadCross);
}

TEST(TouchControls, ImageIsRedrawnOnlyWhenSomethingChanges) {
    TouchControls t;
    const auto& img = t.image(kW, kH, 2);
    EXPECT_EQ(img.w, kW / 2);
    EXPECT_EQ(img.h, kH / 2);
    const uint64_t serial = t.serial();
    t.image(kW, kH, 2);
    EXPECT_EQ(t.serial(), serial);
    const auto& cross = find(t, "cross");
    t.finger_down(1, cross.x, cross.y, kW, kH);
    t.image(kW, kH, 2);
    EXPECT_NE(t.serial(), serial);
    // Something is drawn where the button is and the middle of the screen stays clear.
    const auto& drawn = t.image(kW, kH, 2);
    const auto at = [&](float x, float y) { return drawn.rgba[static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(drawn.w) + static_cast<std::size_t>(x / 2)]; };
    EXPECT_NE(at(cross.x, cross.y) >> 24, 0u);
    EXPECT_EQ(at(kW / 2.0f, kH / 2.0f) >> 24, 0u);
}
