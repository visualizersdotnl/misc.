
#pragma once

// Demo ID & title (master branch).
const std::string PLAYER_RELEASE_ID = "TPB-xxx";
const std::string PLAYER_RELEASE_TITLE = "A backhand for Sally.";

// For a fine selection of modes to choose from:
// (source: https://github.com/visualizersdotnl/misc./blob/master/aspectratios.h)
#include "visualizersdotnl_aspectratios.h"

// Aspect ratio the demo is to be presented in.
// #define PLAYER_RENDER_ASPECT_RATIO 16.f/9.f
const float PLAYER_RENDER_ASPECT_RATIO = AspectRatios::kPolyvisionUltra;

// Virtual resolution for sprite rendering (should match PLAYER_ASPECT_RATIO).
// const float PLAYER_SPRITE_RES_X = 1920.f;
// const float PLAYER_SPRITE_RES_Y = 1080.f;
const float PLAYER_SPRITE_RES_X = 1800.f;
const float PLAYER_SPRITE_RES_Y = 900.f;

// Windowed (dev. only) settings.
const bool PLAYER_WINDOWED_DEV = true;

// Force setup dialog (disabled by default in debug and design builds).
// #define PLAYER_FORCE_SETUP_DIALOG

// Dev. settings if dialog isn't being used:
const bool PLAYER_VSYNC_DEV = false;
const UINT PLAYER_MULTI_SAMPLE_DEV = 1; // 1 means OFF, otherwise: 2, 4, 8.

// Development Polyvision Ultra.
const unsigned int PLAYER_WINDOWED_RES_X = 1000;
const unsigned int PLAYER_WINDOWED_RES_Y = 500;

// Development 720p.
// const unsigned int PLAYER_WINDOWED_RES_X = 1280;
// const unsigned int PLAYER_WINDOWED_RES_Y = 720;

// Use these to test letterboxing in windowed mode.
// const unsigned int PLAYER_WINDOWED_RES_X = 1280; // Letter.
// const unsigned int PLAYER_WINDOWED_RES_Y = 1024;
// const unsigned int PLAYER_WINDOWED_RES_X = 640;  // Letter.
// const unsigned int PLAYER_WINDOWED_RES_Y = 480;
// const unsigned int PLAYER_WINDOWED_RES_X = 800;  // Pillar.
// const unsigned int PLAYER_WINDOWED_RES_Y = 400;

// Shader binaries (with a 'b' appended to extension) are dumped on initial load.
// By enabling this boolean you can use them at runtime for public release.
const bool PLAYER_RUN_FROM_SHADER_BINARIES = false;

// Audio settings.
// const std::string PLAYER_MP3_PATH = "ROSS2.mp3"; // Relative to asset root (see Demo.cpp).
const std::string PLAYER_MP3_PATH = "Triace - Payflash.mp3"; // Relative to asset root (see Demo.cpp).
const bool PLAYER_MUTE_AUDIO = false;

// Audio sync. settings.
const double PLAYER_ROCKET_BPM = 120.0;
const int PLAYER_ROCKET_RPB = 8; // Rows per beat, or: Rocket tracker precision.
