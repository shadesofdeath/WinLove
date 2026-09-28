#pragma once
// WinLove design tokens — generated from tokens.json (v0.1.0). Do not edit by hand.
#include <d2d1.h>

namespace wl::tokens {

namespace color {
namespace dark {
  inline constexpr D2D1_COLOR_F bg_base {0.1020f, 0.0980f, 0.0941f, 1.0000f}; // #1A1918
  inline constexpr D2D1_COLOR_F bg_panel {0.1255f, 0.1176f, 0.1098f, 1.0000f}; // #201E1C
  inline constexpr D2D1_COLOR_F bg_raised {0.1608f, 0.1490f, 0.1373f, 1.0000f}; // #292623
  inline constexpr D2D1_COLOR_F bg_pressed {0.1961f, 0.1804f, 0.1647f, 1.0000f}; // #322E2A
  inline constexpr D2D1_COLOR_F bg_overlay {0.1490f, 0.1373f, 0.1216f, 1.0000f}; // #26231F
  inline constexpr D2D1_COLOR_F bg_input {0.0863f, 0.0824f, 0.0784f, 1.0000f}; // #161514
  inline constexpr D2D1_COLOR_F line_subtle {0.1804f, 0.1686f, 0.1569f, 1.0000f}; // #2E2B28
  inline constexpr D2D1_COLOR_F line_strong {0.2588f, 0.2392f, 0.2196f, 1.0000f}; // #423D38
  inline constexpr D2D1_COLOR_F text_primary {0.9255f, 0.9059f, 0.8784f, 1.0000f}; // #ECE7E0
  inline constexpr D2D1_COLOR_F text_secondary {0.6392f, 0.6078f, 0.5686f, 1.0000f}; // #A39B91
  inline constexpr D2D1_COLOR_F text_tertiary {0.4353f, 0.4078f, 0.3804f, 1.0000f}; // #6F6861
  inline constexpr D2D1_COLOR_F text_disabled {0.3216f, 0.3020f, 0.2784f, 1.0000f}; // #524D47
  inline constexpr D2D1_COLOR_F text_onAccent {0.1020f, 0.0706f, 0.0627f, 1.0000f}; // #1A1210
  inline constexpr D2D1_COLOR_F accent_base {0.8314f, 0.5647f, 0.3529f, 1.0000f}; // #D4905A
  inline constexpr D2D1_COLOR_F accent_hover {0.8706f, 0.6235f, 0.4235f, 1.0000f}; // #DE9F6C
  inline constexpr D2D1_COLOR_F accent_pressed {0.7608f, 0.4980f, 0.2941f, 1.0000f}; // #C27F4B
  inline constexpr D2D1_COLOR_F accent_subtle {0.2275f, 0.1765f, 0.1333f, 1.0000f}; // #3A2D22
  inline constexpr D2D1_COLOR_F accent_focus {0.8314f, 0.5647f, 0.3529f, 1.0000f}; // #D4905A
  inline constexpr D2D1_COLOR_F status_success {0.3608f, 0.7216f, 0.4549f, 1.0000f}; // #5CB874
  inline constexpr D2D1_COLOR_F status_warning {0.8667f, 0.6667f, 0.2431f, 1.0000f}; // #DDAA3E
  inline constexpr D2D1_COLOR_F status_error {0.8941f, 0.3529f, 0.2706f, 1.0000f}; // #E45A45
  inline constexpr D2D1_COLOR_F status_info {0.4353f, 0.6588f, 0.8627f, 1.0000f}; // #6FA8DC
  inline constexpr D2D1_COLOR_F status_successSubtle {0.1373f, 0.2275f, 0.1647f, 1.0000f}; // #233A2A
  inline constexpr D2D1_COLOR_F status_warningSubtle {0.2314f, 0.1961f, 0.1255f, 1.0000f}; // #3B3220
  inline constexpr D2D1_COLOR_F status_errorSubtle {0.2431f, 0.1490f, 0.1333f, 1.0000f}; // #3E2622
  inline constexpr D2D1_COLOR_F status_infoSubtle {0.1333f, 0.1882f, 0.2353f, 1.0000f}; // #22303C
  inline constexpr D2D1_COLOR_F scrim {0.0000f, 0.0000f, 0.0000f, 0.6000f}; // #00000099
  inline constexpr D2D1_COLOR_F shadow_key {0.0000f, 0.0000f, 0.0000f, 0.4000f}; // #00000066
  inline constexpr D2D1_COLOR_F shadow_ambient {0.0000f, 0.0000f, 0.0000f, 0.2510f}; // #00000040
} // namespace dark
namespace light {
  inline constexpr D2D1_COLOR_F bg_base {0.9608f, 0.9490f, 0.9333f, 1.0000f}; // #F5F2EE
  inline constexpr D2D1_COLOR_F bg_panel {0.9255f, 0.9098f, 0.8902f, 1.0000f}; // #ECE8E3
  inline constexpr D2D1_COLOR_F bg_raised {0.8902f, 0.8706f, 0.8471f, 1.0000f}; // #E3DED8
  inline constexpr D2D1_COLOR_F bg_pressed {0.8510f, 0.8275f, 0.8000f, 1.0000f}; // #D9D3CC
  inline constexpr D2D1_COLOR_F bg_overlay {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F bg_input {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F line_subtle {0.8667f, 0.8431f, 0.8157f, 1.0000f}; // #DDD7D0
  inline constexpr D2D1_COLOR_F line_strong {0.7686f, 0.7412f, 0.7098f, 1.0000f}; // #C4BDB5
  inline constexpr D2D1_COLOR_F text_primary {0.1294f, 0.1176f, 0.1059f, 1.0000f}; // #211E1B
  inline constexpr D2D1_COLOR_F text_secondary {0.3843f, 0.3569f, 0.3294f, 1.0000f}; // #625B54
  inline constexpr D2D1_COLOR_F text_tertiary {0.5216f, 0.4902f, 0.4588f, 1.0000f}; // #857D75
  inline constexpr D2D1_COLOR_F text_disabled {0.7020f, 0.6706f, 0.6392f, 1.0000f}; // #B3ABA3
  inline constexpr D2D1_COLOR_F text_onAccent {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F accent_base {0.6588f, 0.3608f, 0.1412f, 1.0000f}; // #A85C24
  inline constexpr D2D1_COLOR_F accent_hover {0.5882f, 0.3176f, 0.1216f, 1.0000f}; // #96511F
  inline constexpr D2D1_COLOR_F accent_pressed {0.5176f, 0.2745f, 0.1020f, 1.0000f}; // #84461A
  inline constexpr D2D1_COLOR_F accent_subtle {0.9451f, 0.8627f, 0.7882f, 1.0000f}; // #F1DCC9
  inline constexpr D2D1_COLOR_F accent_focus {0.6588f, 0.3608f, 0.1412f, 1.0000f}; // #A85C24
  inline constexpr D2D1_COLOR_F status_success {0.1804f, 0.4902f, 0.2627f, 1.0000f}; // #2E7D43
  inline constexpr D2D1_COLOR_F status_warning {0.5412f, 0.3843f, 0.0000f, 1.0000f}; // #8A6200
  inline constexpr D2D1_COLOR_F status_error {0.7255f, 0.2275f, 0.1647f, 1.0000f}; // #B93A2A
  inline constexpr D2D1_COLOR_F status_info {0.1843f, 0.4275f, 0.6588f, 1.0000f}; // #2F6DA8
  inline constexpr D2D1_COLOR_F status_successSubtle {0.8667f, 0.9373f, 0.8824f, 1.0000f}; // #DDEFE1
  inline constexpr D2D1_COLOR_F status_warningSubtle {0.9608f, 0.9176f, 0.7882f, 1.0000f}; // #F5EAC9
  inline constexpr D2D1_COLOR_F status_errorSubtle {0.9647f, 0.8549f, 0.8353f, 1.0000f}; // #F6DAD5
  inline constexpr D2D1_COLOR_F status_infoSubtle {0.8549f, 0.9059f, 0.9529f, 1.0000f}; // #DAE7F3
  inline constexpr D2D1_COLOR_F scrim {0.0000f, 0.0000f, 0.0000f, 0.4000f}; // #00000066
  inline constexpr D2D1_COLOR_F shadow_key {0.0000f, 0.0000f, 0.0000f, 0.2000f}; // #00000033
  inline constexpr D2D1_COLOR_F shadow_ambient {0.0000f, 0.0000f, 0.0000f, 0.1216f}; // #0000001F
} // namespace light
namespace hc {
  inline constexpr D2D1_COLOR_F bg_base {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F bg_panel {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F bg_raised {0.1020f, 0.1020f, 0.1020f, 1.0000f}; // #1A1A1A
  inline constexpr D2D1_COLOR_F bg_pressed {0.2000f, 0.2000f, 0.2000f, 1.0000f}; // #333333
  inline constexpr D2D1_COLOR_F bg_overlay {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F bg_input {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F line_subtle {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F line_strong {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F text_primary {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F text_secondary {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F text_tertiary {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F text_disabled {0.2471f, 0.9490f, 0.2471f, 1.0000f}; // #3FF23F
  inline constexpr D2D1_COLOR_F text_onAccent {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F accent_base {0.1020f, 0.9216f, 1.0000f, 1.0000f}; // #1AEBFF
  inline constexpr D2D1_COLOR_F accent_hover {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F accent_pressed {0.1020f, 0.9216f, 1.0000f, 1.0000f}; // #1AEBFF
  inline constexpr D2D1_COLOR_F accent_subtle {0.1020f, 0.9216f, 1.0000f, 1.0000f}; // #1AEBFF
  inline constexpr D2D1_COLOR_F accent_focus {1.0000f, 1.0000f, 1.0000f, 1.0000f}; // #FFFFFF
  inline constexpr D2D1_COLOR_F status_success {0.2471f, 0.9490f, 0.2471f, 1.0000f}; // #3FF23F
  inline constexpr D2D1_COLOR_F status_warning {1.0000f, 1.0000f, 0.0000f, 1.0000f}; // #FFFF00
  inline constexpr D2D1_COLOR_F status_error {1.0000f, 0.3137f, 0.3137f, 1.0000f}; // #FF5050
  inline constexpr D2D1_COLOR_F status_info {0.1020f, 0.9216f, 1.0000f, 1.0000f}; // #1AEBFF
  inline constexpr D2D1_COLOR_F status_successSubtle {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F status_warningSubtle {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F status_errorSubtle {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F status_infoSubtle {0.0000f, 0.0000f, 0.0000f, 1.0000f}; // #000000
  inline constexpr D2D1_COLOR_F scrim {0.0000f, 0.0000f, 0.0000f, 0.8000f}; // #000000CC
  inline constexpr D2D1_COLOR_F shadow_key {0.0000f, 0.0000f, 0.0000f, 0.0000f}; // #00000000
  inline constexpr D2D1_COLOR_F shadow_ambient {0.0000f, 0.0000f, 0.0000f, 0.0000f}; // #00000000
} // namespace hc
} // namespace color

namespace space {
  inline constexpr float s0 = 0.f;
  inline constexpr float s1 = 2.f;
  inline constexpr float s2 = 4.f;
  inline constexpr float s3 = 6.f;
  inline constexpr float s4 = 8.f;
  inline constexpr float s5 = 12.f;
  inline constexpr float s6 = 16.f;
  inline constexpr float s7 = 24.f;
}
namespace radius {
  inline constexpr float r1 = 2.f;
  inline constexpr float r2 = 3.f;
  inline constexpr float r3 = 4.f;
  inline constexpr float focusOffset = 1.f;
}
namespace size {
  inline constexpr float controlSmall = 20.f;
  inline constexpr float control = 24.f;
  inline constexpr float controlLarge = 28.f;
  inline constexpr float row = 24.f;
  inline constexpr float rowComfortable = 28.f;
  inline constexpr float densityDelta = 4.f;
  inline constexpr float icon = 16.f;
  inline constexpr float iconLarge = 24.f;
  inline constexpr float titleBar = 32.f;
  inline constexpr float statusBar = 24.f;
  inline constexpr float navExpanded = 200.f;
  inline constexpr float navCollapsed = 44.f;
  inline constexpr float inspector = 280.f;
  inline constexpr float captionButton = 46.f;
  inline constexpr float checkbox = 12.f;
  inline constexpr float toggleW = 24.f;
  inline constexpr float toggleH = 12.f;
  inline constexpr float scrollbarRest = 4.f;
  inline constexpr float scrollbarHover = 8.f;
  inline constexpr float splitterHit = 6.f;
  inline constexpr float minWindowW = 1100.f;
  inline constexpr float minWindowH = 700.f;
}
namespace type {
  struct Style { const wchar_t* family; float size; float lineHeight; int weight; float letterSpacing; };
  inline constexpr Style title { L"IBM Plex Sans", 16.f, 22.f, 600 /*DWRITE_FONT_WEIGHT_SEMI_BOLD*/, 0f };
  inline constexpr Style bodyStrong { L"IBM Plex Sans", 12.f, 16.f, 500 /*DWRITE_FONT_WEIGHT_MEDIUM*/, 0f };
  inline constexpr Style body { L"IBM Plex Sans", 12.f, 16.f, 400 /*DWRITE_FONT_WEIGHT_NORMAL*/, 0f };
  inline constexpr Style section { L"IBM Plex Sans", 11.f, 16.f, 500 /*DWRITE_FONT_WEIGHT_MEDIUM*/, 0.22f };
  inline constexpr Style caption { L"IBM Plex Sans", 11.f, 16.f, 400 /*DWRITE_FONT_WEIGHT_NORMAL*/, 0f };
  inline constexpr Style mono { L"JetBrains Mono", 11.f, 16.f, 400 /*DWRITE_FONT_WEIGHT_NORMAL*/, 0f };
  inline constexpr Style kbd { L"JetBrains Mono", 10.f, 14.f, 400 /*DWRITE_FONT_WEIGHT_NORMAL*/, 0f };
}
namespace elevation {
  struct Shadow { float offsetX; float offsetY; float blurRadius; D2D1_COLOR_F color; };
  inline constexpr Shadow menu[2] { {0.f, 1.f, 2.f, color::dark::shadow_key}, {0.f, 4.f, 12.f, color::dark::shadow_ambient} };
  inline constexpr Shadow dialog[2] { {0.f, 2.f, 4.f, color::dark::shadow_key}, {0.f, 12.f, 32.f, color::dark::shadow_ambient} };
  inline constexpr Shadow toast[2] { {0.f, 1.f, 2.f, color::dark::shadow_key}, {0.f, 6.f, 16.f, color::dark::shadow_ambient} };
}
namespace motion {
  inline constexpr float instantMs = 0.f;
  inline constexpr float fastMs = 80.f;
  inline constexpr float baseMs = 140.f;
  inline constexpr float slowMs = 220.f;
  inline constexpr float enterMs = 180.f;
  inline constexpr float exitMs = 120.f;
  inline constexpr float standard[4] {0.2f, 0f, 0f, 1f};
  inline constexpr float decelerate[4] {0f, 0f, 0f, 1f};
  inline constexpr float accelerate[4] {0.4f, 0f, 1f, 1f};
  inline constexpr float springStiffness = 380.f, springDamping = 34.f, springMass = 1.f;
}
namespace opacity {
  inline constexpr float disabled = 0.45f;
  inline constexpr float hoverOverlay = 0.06f;
  inline constexpr float pressedOverlay = 0.1f;
  inline constexpr float scrim = 0.6f;
  inline constexpr float placeholder = 1f;
}
namespace z {
  inline constexpr int content = 0;
  inline constexpr int panels = 10;
  inline constexpr int splitter = 20;
  inline constexpr int statusBar = 30;
  inline constexpr int titleBar = 40;
  inline constexpr int flyout = 100;
  inline constexpr int menu = 110;
  inline constexpr int tooltip = 120;
  inline constexpr int dialogScrim = 200;
  inline constexpr int dialog = 210;
  inline constexpr int toast = 300;
  inline constexpr int commandPalette = 220;
}

} // namespace wl::tokens
