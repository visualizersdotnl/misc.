
/*
	Global platform include for Core and all depending projects.
	- Includes API, CRT & STL headers & a few local essentials.
	- And some of that "misc. stuff". 

	Additional dependencies:
	- ..\3rdparty\DxErr_June_2010_SDK_x64\DxErr.lib
	- d3d11.lib
	- dxgi.lib
	- d3dcompiler.lib
	- dxguid.lib
*/

#if !defined(CORE_PLATFORM_H)
#define CORE_PLATFORM_H

// APIs
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

// CRT & STL
#include <stdint.h>
#include <string>
#include <iostream>
#include <sstream>
#include <array>
#include <vector>
#include <list>
#include <algorithm>
#include <exception>
#include <memory>

// Local sys.
#include "Sys/Assert.h"
#include "Sys/Exception.h"
#include "Sys/Noncopyable.h"
#include "Sys/ComPtr.h"
#include "Sys/DebugLog.h"
#include "Sys/Timer.h"

// Standard C++ 3D math primitives.
#include "Std3DMath_Copy/Math.h"


// For easy access to DirectXMath types.
using namespace DirectX; 
using namespace DirectX::PackedVector;

#define SAFE_RELEASE(pX) if (nullptr != (pX)) (pX)->Release()

inline bool IsPowerOfTwo(unsigned int X) 
{ 
	return (0 != X) && (X & (~X + 1)) == X;
}

// Source: http://stackoverflow.com/questions/98153/whats-the-best-hashing-algorithm-to-use-on-a-stl-string-when-using-hash-map
inline unsigned int SimpleStringHash(const std::string &path, unsigned int seed)
{
	unsigned int hash = seed;
	for (auto character : path)
		hash = hash*101 + character;

	return hash;
}

#endif // CORE_PLATFORM_H
