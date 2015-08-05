
#if !defined(PLAYER_DIALOG_H)
#define PLAYER_DIALOG_H

bool SetupDialog(
	HINSTANCE hInstance, 
	int &iAudioDev, 
	unsigned int &iAdapter, 
	unsigned int &iOutput, 
	DXGI_MODE_DESC &mode,       // Only valid for full screen settings.
	float &aspectRatio,         // Return value is either -1.f (correct automatically) or a forced output ratio.
	unsigned int &multiSamples, // Return value is either 1 (switch it off, set Quality to 0) or 2/4/8 (minimum Direct3D 11.0 hardware requirement).
	bool &windowed,
	bool &vSync,                // Should be ignored in windowed mode (see DirectX documentation).
	IDXGIFactory1 &DXGIFactory);

#endif // PLAYER_DIALOG_H
