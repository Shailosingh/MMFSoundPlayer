#include "Helpers.h"

using namespace MMFSoundPlayerLib;

template<typename T> void Helpers::SafeRelease(T** ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = nullptr;
	}
}