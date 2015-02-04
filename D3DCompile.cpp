
bool D3D::CompileShader(
	const std::string& path,
	const std::string& entryPoint,
	const std::string& target,
	const char* source, 
	size_t sourceLen,
	uint8_t** outBytecode, 
	size_t* outBytecodeLen,
	std::string& errorMsg)
{	
	ID3DBlob* shader = nullptr;
	ID3DBlob* errors = nullptr;

	const HRESULT hRes = D3DCompile(
		source, 
		sourceLen,
		path.c_str(),                     
		nullptr,                           // No defines.
		D3D_COMPILE_STANDARD_FILE_INCLUDE, // Use default include handler (FIXME: create "smart" one).
		entryPoint.c_str(),              
		target.c_str(),      
#if defined(_DEBUG) || defined(_DESIGN)
		D3DCOMPILE_OPTIMIZATION_LEVEL3,
#else
		D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_SKIP_VALIDATION,
#endif
		0,
		&shader,
		&errors);

	if (S_OK != hRes)
	{
		if (nullptr != errors)
		{			
			errorMsg = std::string(reinterpret_cast<char*>(errors->GetBufferPointer()));

			// Not using D3D_ASSERT_MSG as this may be called from a job thread so I don't want a throw.
			ASSERT_MSG(0, errorMsg.c_str());
		}

		SAFE_RELEASE(errors);
		return false;
	}

	ASSERT(nullptr != shader && shader->GetBufferSize() > 0);
	*outBytecode = new uint8_t[shader->GetBufferSize()];
	memcpy(*outBytecode, reinterpret_cast<uint8_t*>(shader->GetBufferPointer()), (size_t) shader->GetBufferSize());
	*outBytecodeLen = (size_t) shader->GetBufferSize();
	shader->Release();

	return true;
}
