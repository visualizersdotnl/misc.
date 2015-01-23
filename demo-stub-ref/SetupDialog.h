
#if !defined(PLAYER_DIALOG_H)
#define PLAYER_DIALOG_H

bool SetupDialog(
	HINSTANCE hInstance, 
	int &iAudioDev, 
	UINT &iAdapter, 
	UINT &iOutput, 
	DXGI_MODE_DESC &mode, 
	bool &windowed,
	bool &vSync,
	IDXGIFactory1 &DXGIFactory);

#endif // PLAYER_DIALOG_H
