#pragma once

#include <mfidl.h>
#include <string>

namespace MMFSoundPlayerLib
{
	enum PlayerState
	{
		Closed,         // No session.
		Ready,          // Session was created, ready to open a file. 
		OpenPending,    // Session is opening a file.
		Started,        // Session is playing a file.
		Paused,         // Session is paused.
		Stopped,        // Session is stopped (ready to play).
		Closing         // Application has closed the session, but is waiting for MESessionClosed.
	};
	
	class MMFSoundPlayer : public IMFAsyncCallback
	{
	private:
		//Datafields
		IMFMediaSession* CurrentMediaSession;
		IMFMediaSource* CurrentMediaSource;
		PlayerState CurrentState;
		std::wstring CurrentFilePath;
		
		//Event Handles
		HANDLE ExitEvent;
		
		//Reference count for IUnknown
		long ReferenceCount;

		//Private Constructor (public should call CreateInstance) and Destructor (public should call release)
		MMFSoundPlayer();
		~MMFSoundPlayer();

		//Setup Functions
		HRESULT CreateMediaSession();
		HRESULT CreateMediaSource(PCWSTR inputFilePath);
		HRESULT CreatePlaybackTopology(IMFPresentationDescriptor* inputPresentationDescriptor, IMFTopology** outputTopology);

		//Destruction functions
		HRESULT CloseMediaSessionAndSource();
		
	public:
		//A static public function to create an instance of the object (needed to make object a COM object)
		static HRESULT CreateInstance(MMFSoundPlayer** outputMMFSoundPlayer);

		//Acts as constructor, called by CreateInstance
		HRESULT Initialize(); 

		//IMFAsyncCallback methods (required for handling of events)
		STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult);
		STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue);
		
		//IUnknown methods (required for IMFAsyncCallback)
		STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
		STDMETHODIMP_(ULONG) AddRef();
		STDMETHODIMP_(ULONG) Release();

		//Audio Control
		HRESULT SetFileIntoPlayer(PCWSTR inputFilepath);
		HRESULT TogglePlayPause();
		HRESULT Stop(); 

		//Getters
		PlayerState GetPlayerState();
	};
}