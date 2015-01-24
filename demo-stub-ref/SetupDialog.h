
#if !defined(PLAYER_DIALOG_H)
#define PLAYER_DIALOG_H

bool SetupDialog(
	HINSTANCE hInstance, 
	int &iAudioDev, 
	UINT &iAdapter, 
	UINT &iOutput, 
	DXGI_MODE_DESC &mode, // Only valid for full screen settings.
	float &aspectRatio,   // Returned value is either -1.f (correct automatically) or a forced output ratio.
	bool &windowed,
	bool &vSync,          // Should be ignored in windowed mode (see DirectX documentation).
	IDXGIFactory1 &DXGIFactory);

#endif // PLAYER_DIALOG_H
