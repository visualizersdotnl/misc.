
#pragma once

// Use to enumerate available devices by name. 1-based!
void Audio_EnumerateDevices(std::vector<std::string> &deviceNames);

// 'iDevice' - valid device index or -1 for system default
bool Audio_Create(int iDevice, HWND hWnd, const std::string &mp3Path, bool mute); 
void Audio_Destroy();
void Audio_Update();

void Audio_Start();
void Audio_Pause();
void Audio_Unpause();
bool Audio_IsPlaying();

void Audio_SetPosition(float secPos);
float Audio_GetPosition();

// Initially implemented for TPB-06, kept in for now.
// For zero influence just set wetDry to 0 or don't touch it at all.
void Audio_FlangerMP3(float wetDry, float freqMod);
