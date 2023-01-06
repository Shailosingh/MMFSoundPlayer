#pragma once

namespace MMFSoundPlayerLib
{
	class Helpers
	{
	public:
		/// <summary>
		/// This method is used to safely release COM objects.
		/// </summary>
		/// <typeparam name="T">Type of COM object to be released</typeparam>
		/// <param name="ppT">Pointer to pointer of COM object to be released</param>
		template<typename T>
		static void SafeRelease(T** ppT);
	};
}