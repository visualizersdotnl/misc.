	
/*
	Player stub.

	Look for 'FIXME' & '@plek' -> stuff to either, fix, remove or formalize.

	The idea here is to:
	- Create and manage a simple render window.
	- Initialize and maintain DXGI/D3D and create the device exactly like Core wants it.
	- Kick off audio.
	- Provide a stable main loop.
	- Take care of proper shutdown and error message display.

	Add basic leak detection?
*/

#include <Core/Platform.h>
#include <intrin.h> // for SIMD check
#include <Core/Core.h>
#include "Settings.h"
#include "Resource.h"
#include "SceneTools.h"
#include "DebugCamera.h"
#include "AutoShaderReload.h"
#include "Audio.h"
#include "SetupDialog.h"

#include "Content/Demo.h"

// audio settings
const std::string kMP3Path = PLAYER_MP3_PATH;
const bool kMuteAudio = PLAYER_MUTE_AUDIO;

// configuration: windowed / full screen
bool s_windowed = PLAYER_WINDOWED_DEV; // Can be modified later by setup dialog.
const unsigned int kWindowedResX = PLAYER_WINDOWED_RES_X;
const unsigned int kWindowedResY = PLAYER_WINDOWED_RES_Y;

// When *not* using SetupDialog():
//
// In full screen mode the primary desktop resolution is adapted.
//
// Using the desktop resolution makes good sense: it's usually the display's optimal resolution.
// A beam team can very well be instructed to select a more appropriate one for performance reasons.
//
// SetupDialog() allows for full user configuration.

// global error message
static std::string s_lastError;
void SetLastError(const std::string &message) { s_lastError = message; }

// DXGI objects
static IDXGIFactory1  *s_pDXGIFactory = nullptr;
static IDXGIAdapter1  *s_pAdapter     = nullptr;
static IDXGIOutput    *s_pDisplay     = nullptr;
static DXGI_MODE_DESC  s_displayMode;

// app. window
static bool s_classRegged = false;
static HWND s_hWnd = NULL;
static bool s_wndIsActive; // set by WindowProc()

// Direct3D objects
static ID3D11Device        *s_pD3D        = nullptr;
static ID3D11DeviceContext *s_pD3DContext = nullptr;
static IDXGISwapChain      *s_pSwapChain  = nullptr;

// Debug camera and it's state.
#if defined(_DEBUG) || defined(_DESIGN)
static AutoShaderReload* s_pAutoShaderReloader = nullptr;
static DebugCamera* s_pDebugCamera = nullptr;

static bool s_isPaused = false;
static bool s_isMouseTracking = false;
static int  s_mouseTrackInitialX;
static int  s_mouseTrackInitialY;
#endif

static bool CreateDXGI(HINSTANCE hInstance)
{
	if FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&s_pDXGIFactory)))
	{
		SetLastError("Can not create DXGI 1.1 factory.");
		return false;
	}

	// get primary adapter
	s_pDXGIFactory->EnumAdapters1(0, &s_pAdapter);
	if (nullptr == s_pAdapter)
	{
		SetLastError("No primary display adapter found.");
		return false;
	}

//	DXGI_ADAPTER_DESC1 adDesc;
//	s_pAdapter->GetDesc1(&adDesc);

	// and it's display
	s_pAdapter->EnumOutputs(0, &s_pDisplay);
	if (nullptr == s_pDisplay)
	{
		SetLastError("No physical display attached to primary display adapter.");
		return false;
	}

	// get current (desktop) display mode
	DXGI_MODE_DESC modeToMatch;
	modeToMatch.Width = GetSystemMetrics(SM_CXSCREEN);
	modeToMatch.Height = GetSystemMetrics(SM_CYSCREEN);
	modeToMatch.RefreshRate.Numerator = 0;
	modeToMatch.RefreshRate.Denominator = 0;
	modeToMatch.Format = Pimp::D3D_BACKBUFFER_FORMAT_LIN;
	modeToMatch.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	modeToMatch.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	if FAILED(s_pDisplay->FindClosestMatchingMode(&modeToMatch, &s_displayMode, NULL))
	{
		SetLastError("Can not retrieve primary monitor's display mode.");
		return false;
	}

	// Now that we've found a valid backbuffer, replace the format for a gamma-corrected one.
	// FindClosestMatchingMode() won't detect those automatically.
	s_displayMode.Format = Pimp::D3D_BACKBUFFER_FORMAT_GAMMA;

	if (true == s_windowed)
	{
		// override resolution
		s_displayMode.Width  = kWindowedResX;
		s_displayMode.Height = kWindowedResY;
	}

	return true;
}

static void DestroyDXGI()
{
	SAFE_RELEASE(s_pDisplay);
	SAFE_RELEASE(s_pAdapter);
	SAFE_RELEASE(s_pDXGIFactory);
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		// debug camera mouse input
#if defined(_DEBUG) || defined(_DESIGN)
	case WM_LBUTTONDOWN:
		s_isMouseTracking = true;
		s_mouseTrackInitialX = LOWORD(lParam);
		s_mouseTrackInitialY = HIWORD(lParam);
		s_pDebugCamera->StartLookAt(); 
		break;

	case WM_LBUTTONUP:
		s_isMouseTracking = false;
		s_pDebugCamera->EndLookAt();
		break;

	case WM_MOUSEMOVE:
		if (s_isMouseTracking) {
			int posX = LOWORD(lParam);
			int posY = HIWORD(lParam);
			s_pDebugCamera->LookAt(posX - s_mouseTrackInitialX, posY - s_mouseTrackInitialY); }
		break;
#endif

	case WM_CLOSE:
		PostQuitMessage(0); // terminate message loop
		s_hWnd = NULL;      // DefWindowProc() will call DestroyWindow()
		break;

	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_ESCAPE:
			PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;
		
		// debug camera (un)pause 
#if defined(_DEBUG) || defined(_DESIGN)
		case VK_SPACE:
			{
				s_isPaused = !s_isPaused;
				DEBUG_LOG(s_isPaused ? "Entering debug camera mode." : "Leaving debug camera mode.")

				s_pDebugCamera->SetEnabled(s_isPaused);

				if (false == s_windowed)
					ShowCursor(true == s_isPaused);
			}
			break;
#endif
		}

		// debug camera keyboard input
#if defined(_DEBUG) || defined(_DESIGN)
		if (s_isPaused)
		{
			if (wParam == 'A')
				s_pDebugCamera->Move(Vector3(-1.0f, 0.0f, 0.0f));
			else if (wParam == 'D')
				s_pDebugCamera->Move(Vector3(+1.0f, 0.0f, 0.0f));
			else if (wParam == 'W')
				s_pDebugCamera->Move(Vector3( 0.0f, 0.0f,-1.0f));
			else if (wParam == 'S')
				s_pDebugCamera->Move(Vector3( 0.0f, 0.0f,+1.0f));
			else if (wParam == 'Q' && !s_isMouseTracking)
				s_pDebugCamera->Roll(false);
			else if (wParam == 'E' && !s_isMouseTracking)
				s_pDebugCamera->Roll(true);			
			else if (wParam == VK_RETURN)
				s_pDebugCamera->DumpCurrentTransformToOutputWindow();
		}
#endif
		break;

	case WM_ACTIVATE:
		switch (LOWORD(wParam))
		{
		case WA_ACTIVE:
		case WA_CLICKACTIVE:
			if (false == s_windowed) 
			{
				// (re)assign WS_EX_TOPMOST style
				SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			}
			
			s_wndIsActive = true;
			break;
		
		case WA_INACTIVE:
			if (false == s_windowed) 
			{
				if (NULL != s_pSwapChain)
				{
					// push window to bottom of the Z order
					SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
				}
			}

			s_wndIsActive = false;
			break;
		};

	case WM_SIZE:
		break; // ALT+ENTER is blocked, all else is ignored or scaled if the window type permits it.
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static bool CreateAppWindow(HINSTANCE hInstance, int nCmdShow)
{
	WNDCLASSEX wndClass;
	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = 0;
	wndClass.lpfnWndProc = WindowProc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hInstance;
	wndClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wndClass.hCursor = NULL;
	wndClass.hbrBackground = (s_windowed) ? (HBRUSH) GetStockObject(BLACK_BRUSH) : NULL;
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = PLAYER_RELEASE_ID.c_str();
	wndClass.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
	
	if (0 == RegisterClassEx(&wndClass))
	{
		SetLastError("Can not create application window (RegisterClassEx() failed).");
		return false;
	}

	s_classRegged = true;
	
	DWORD windowStyle, exWindowStyle;
	if (true == s_windowed)
	{
		// windowed style
		windowStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
		exWindowStyle = 0;
	}
	else
	{
		// full screen style (WS_EX_TOPMOST assigned by WM_ACTIVATE)
		windowStyle = WS_POPUP;
		exWindowStyle = 0;
	}

	// calculate full window size
	RECT wndRect = { 0, 0, s_displayMode.Width, s_displayMode.Height };
	AdjustWindowRectEx(&wndRect, windowStyle, FALSE, exWindowStyle);
	const int wndWidth = wndRect.right - wndRect.left;
	const int wndHeight = wndRect.bottom - wndRect.top;

	s_hWnd = CreateWindowEx(
		exWindowStyle,
		PLAYER_RELEASE_ID.c_str(),
		PLAYER_RELEASE_TITLE.c_str(),
		windowStyle,
		0, 0, // always pop up on primary display's desktop area (*)
		wndWidth, wndHeight,
		NULL,
		NULL,
		hInstance,
		nullptr);	

	// * - Works fine in any windowed mode and it's automatically moved if necessary for
	//     full screen rendering.

	if (NULL == s_hWnd)
	{
		SetLastError("Can not create application window (CreateWindowEx() failed).");
		return false;
	}

	ShowWindow(s_hWnd, (s_windowed) ? nCmdShow : SW_SHOW);

	return true;
}

static bool UpdateAppWindow(bool &renderFrame)
{
	// skip frame unless otherwise specified
	renderFrame = false;

	// got a message to process?
	MSG msg;
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			// quit!
			return false;
		}
		
		// dispatch message
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	else
	{
		// window alive?
		if (NULL != s_hWnd)
		{
			if (false == s_windowed && s_wndIsActive)
			{
				// kill cursor for active full screen window
				SetCursor(NULL);
			}

			// render frame if windowed or full screen window has focus
			if (s_windowed || s_wndIsActive)
			{
				renderFrame = true;
			}
			else 
			{
				// full screen window out of focus: relinquish rest of time slice
				Sleep(0); 
			}
		}
	}
	
	// continue!
	return true;
}

void DestroyAppWindow(HINSTANCE hInstance)
{
	if (NULL != s_hWnd)
	{
		DestroyWindow(s_hWnd);
		s_hWnd = NULL;
	}	
	
	if (true == s_classRegged)
	{
		UnregisterClass("RenderWindow", hInstance);
	}
}

static bool CreateDirect3D()
{
	// create device
#if _DEBUG
	const UINT Flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG;
#else
	const UINT Flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#endif

	const D3D_FEATURE_LEVEL featureLevels[] =
	{
//		D3D_FEATURE_LEVEL_11_1,
		// ^^ This fails on systems that don't explicitly support it (E_INVALIDARG).

		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0
//		D3D_FEATURE_LEVEL_9_3,
//		D3D_FEATURE_LEVEL_9_2,
//		D3D_FEATURE_LEVEL_9_1
	};

	// FIXME: decide what we'll do with this information.
	D3D_FEATURE_LEVEL featureLevel;

	DXGI_SWAP_CHAIN_DESC swapDesc;
	memset(&swapDesc, 0, sizeof(swapDesc));
	swapDesc.BufferDesc = s_displayMode;
	swapDesc.SampleDesc.Count = Pimp::D3D_ANTIALIAS_NUM_SAMPLES;
	swapDesc.SampleDesc.Quality = Pimp::D3D_ANTIALIAS_QUALITY;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.BufferCount = 2;
	swapDesc.OutputWindow = s_hWnd;
	swapDesc.Windowed = s_windowed;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapDesc.Flags = 0; // DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	HRESULT hRes = D3D11CreateDeviceAndSwapChain(
		s_pAdapter,
		D3D_DRIVER_TYPE_UNKNOWN, // This is documented in the "Remarks" section of this call.
		NULL,
		Flags,
		featureLevels, ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION,
		&swapDesc,
		&s_pSwapChain,
		&s_pD3D,
		&featureLevel,
		&s_pD3DContext);
 	if (S_OK == hRes)
	{
		// Block ALT+ENTER et cetera.
		hRes = s_pDXGIFactory->MakeWindowAssociation(s_hWnd, DXGI_MWA_NO_WINDOW_CHANGES);
		ASSERT(hRes == S_OK);
		return true;
	}

	// Failed :(
	std::stringstream message;
	message << "Can't create Direct3D 11.0 device.\n\n";
	message << ((true == s_windowed) ? "Type: windowed.\n" : "Type: full screen.\n");
	message << "Resolution: " << s_displayMode.Width << "*" << s_displayMode.Height << ".\n\n";
	message << DXGetErrorString(hRes) << " - " << DXGetErrorDescription(hRes) << ".\n";
	SetLastError(message.str());
	return false;
}

static void DestroyDirect3D()
{
	if (false == s_windowed && nullptr != s_pSwapChain)
		s_pSwapChain->SetFullscreenState(FALSE, nullptr);
	
	SAFE_RELEASE(s_pSwapChain);
	SAFE_RELEASE(s_pD3DContext);
	SAFE_RELEASE(s_pD3D);
}

#include "CPUID.h"

int __stdcall Main(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow)
{
	// change path to target root
	SetCurrentDirectory("..\\");

	// check for SSE4.1
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	if (0 == (cpuInfo[2] & CPUID_FEAT_ECX_SSE4_1))
//	if (0 == (cpuInfo[2] & CPUID_FEAT_ECX_AVX))
	{
		MessageBox(NULL, "Processor does not support SSE4.1 instructions.", "Error!", MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}

	// initialize Matrix4 class (FIXME: to be replaced by DirectXMath)
	Matrix4::Init();

	// initialize DXGI
	if (CreateDXGI(hInstance))
	{
#if (!defined(_DEBUG) && !defined(_DESIGN)) || defined(PLAYER_FORCE_SETUP_DIALOG)
		// At this point DXGI is ready to use the primary display and for most cases that's just what I need.
		// However, bitches at http://www.pouet.net asked for a setup dialog, so I'm providing one.
		// It does things "on top" meaning that it's easy to just skip for certain build types.
		// Any additional DXGI/Win32 logic is handled in SetupDialog.cpp itself.

		int iAudioDev;
		UINT iAdapter, iOutput;
		DXGI_MODE_DESC dispMode;
		float aspectRatio;
		bool windowed;
		bool vSync;

		if (true == SetupDialog(hInstance, iAudioDev, iAdapter, iOutput, dispMode, aspectRatio, windowed, vSync, *s_pDXGIFactory)) 
		{
			s_windowed = windowed;

			if (false == s_windowed) 
			{
				// Release primary devices.
				SAFE_RELEASE(s_pDisplay);
				SAFE_RELEASE(s_pAdapter);

				// Get selected devices.
				VERIFY(S_OK == s_pDXGIFactory->EnumAdapters1(iAdapter, &s_pAdapter));
				VERIFY(S_OK == s_pAdapter->EnumOutputs(iOutput, &s_pDisplay));
				
				// Override display mode.
				s_displayMode = dispMode;
			} 
			
			// In windowed mode we'll use the primary adapter (display is irrelevant).

			// First pick (0) is default output, which is -1 for BASS_Init().
			if (0 == iAudioDev) iAudioDev = -1;
#else
		if (true)
		{
			const int iAudioDev = -1;            // Default audio device.
			const bool vSync = PLAYER_VSYNC_DEV; // Dev. toggle.
			float aspectRatio = -1.f;            // Automatic mode.

			// Other variables are already set up correctly.
#endif

			// create app. window
			if (CreateAppWindow(hInstance, nCmdShow))
			{
				// initialize audio
				if (Audio_Create(iAudioDev, s_hWnd, Demo::GetAssetsPath() + kMP3Path, kMuteAudio))
				{
					// initialize Direct3D
					if (CreateDirect3D())
					{
						try
						{
							if (-1.f == aspectRatio)
							{
								// Derive from resolution (square pixels).
								aspectRatio = (float) s_displayMode.Width / s_displayMode.Height;
							}

							// Initialize Core D3D.
							std::unique_ptr<Pimp::D3D> pCoreD3D(new Pimp::D3D(
								*s_pD3D, *s_pD3DContext, *s_pSwapChain, 
								PLAYER_RENDER_ASPECT_RATIO, aspectRatio));
							
							// FIXME: get rid of these Core globals.
							Pimp::gD3D = pCoreD3D.get();

							// Prepare demo resources.
							const char *rocketClient = (0 == strlen(lpCmdLine)) ? "localhost" : lpCmdLine;
							DemoRef demoRef(rocketClient);
							if (true == demoRef.IsOK())
							{
#if defined(_DEBUG) || defined(_DESIGN)
								Pimp::World *pWorld = Demo::GetWorld();

								std::unique_ptr<AutoShaderReload> pAutoShaderReload(new AutoShaderReload(pWorld, 0.5f /* checkInterval */));
								std::unique_ptr<DebugCamera> pDebugCamera(new DebugCamera(pWorld));

								// FIXME: this isn't very pretty.
								s_pAutoShaderReloader = pAutoShaderReload.get();
								s_pDebugCamera = pDebugCamera.get();

								DEBUG_LOG("================================================================================");
								DEBUG_LOG("TPBDS mark III is now live!");
								DEBUG_LOG("");
								DEBUG_LOG("> SPACE: Toggle debug camera.");
								DEBUG_LOG("");
								DEBUG_LOG("Controls:");
								DEBUG_LOG("> W,S,A,D:   Translate.");
								DEBUG_LOG("> Q,E:       Roll.");
								DEBUG_LOG("> Drag LMB:  Adjust yaw and pitch.");
								DEBUG_LOG("> ENTER:     Dump current debug camera transform to output window.");
								DEBUG_LOG("");
								DEBUG_LOG("This, of course, only works on scenes that use the Rocket-driven default camera.");
								DEBUG_LOG("================================================================================");
#endif	

								// in windowed mode FPS is refreshed every 60 frames
								float timeElapsedFPS = 0.f;
								unsigned int numFramesFPS = 0;
							
								Pimp::Timer timer;
								float prevTimeElapsed = timer.Get();

								// enter (render) loop
								bool renderFrame;
								while (true == UpdateAppWindow(renderFrame))
								{
									// render frame
									const float time = timer.Get();
									const float timeElapsed = time-prevTimeElapsed;
									prevTimeElapsed = time;

#if defined(_DEBUG) || defined(_DESIGN)
									s_pAutoShaderReloader->Update();

									if (true == s_isPaused)
										Demo::Tick(timeElapsed, s_pDebugCamera->Get());
									else
									{
										if (false == Demo::Tick(timeElapsed))
											break;
									}
#else
									if (false == Demo::Tick(timeElapsed))
										break;
#endif
								
									Demo::WorldRender();
									Pimp::gD3D->Flip((true == windowed) ? 0 : true == vSync); // Windowed is always in vertical sync.

									// Crash handler test :)
//									Pimp::gD3D = (Pimp::D3D*) 0x124;

									if (true == s_windowed)
									{
										// handle FPS counter
										timeElapsedFPS += timeElapsed;

										if (++numFramesFPS == 60)
										{
											const float FPS = 60.f/timeElapsedFPS;
										
											char fpsStr[256];
											sprintf_s(fpsStr, 256, "%s (%.2f FPS)", PLAYER_RELEASE_TITLE.c_str(), FPS);
											SetWindowText(s_hWnd, fpsStr);

											timeElapsedFPS = 0.f;
											numFramesFPS = 0;
										}
									}

									Audio_Update();
								}
							}
						}

						// Catch Core exceptions.
						// Other exceptions should either signal the debugger (development) or are "handled" by the last resort handler below.
						catch(const Pimp::Exception &exception)
						{
							SetLastError(exception.what());
						}
					}

					DestroyDirect3D();
				}
			
				Audio_Destroy();
			}
		
			DestroyAppWindow(hInstance);
		} // SetupDialog()
	}

	DestroyDXGI();

	if (!s_lastError.empty())
	{
		MessageBox(NULL, s_lastError.c_str(), "Error!", MB_OK | MB_ICONEXCLAMATION);
		return 1;
	}

	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int nCmdShow)
{
#if !defined(_DEBUG) && !defined(_DESIGN)
	__try 
	{
#endif

	return Main(hInstance, hPrevInstance, cmdLine, nCmdShow);

#if !defined(_DEBUG) && !defined(_DESIGN)
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
		// Attempt to restore the desktop.
		SAFE_RELEASE(s_pSwapChain);
		if (NULL != s_hWnd) DestroyWindow(s_hWnd);

		// And shut off the audio too.
		Audio_Destroy();

		// Sound the alarm bell.
		MessageBox(NULL, "Demo crashed (unhandled exception). Now quickly: http://www.pouet.net!", PLAYER_RELEASE_ID.c_str(), MB_OK | MB_ICONEXCLAMATION);

		// Better do as little as possible past this point.
		_exit(1); 
	}
#endif

	return 0;
}
