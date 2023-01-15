#pragma once

#include <mfidl.h>
#include <string>
#include <atlbase.h>

namespace MMFSoundPlayerLib
{
	enum PlayerState
	{
		Closed,         // No session.
		Ready,          // Session was created, ready to open a file. 
		OpenPending,    // Session is opening a file.
		Playing,        // Session is playing a file.
		Paused,         // Session is paused.
		Stopped,        // Session is stopped (ready to play).
		Closing         // Application has closed the session, but is waiting for MESessionClosed.
	};
	
	class MMFSoundPlayer : public IMFAsyncCallback
	{
	private:
		//Player datafields
		CComPtr<IMFMediaSession> CurrentMediaSession;
		CComPtr<IMFMediaSource> CurrentMediaSource;
		PlayerState CurrentState;
		
		//Song info
		std::wstring CurrentFilePath;
		UINT64 CurrentAudioFileDuration_100NanoSecondUnits;
		
		//Event Handles
		HANDLE ExitEvent;
		HANDLE PlayEvent;
		HANDLE PauseEvent;
		HANDLE StopEvent;
		HANDLE TopologySetEvent;
		
		//Reference count for IUnknown
		long ReferenceCount;

		//Acts as constructor, called by CreateInstance
		HRESULT Initialize();

		//Private Constructor (public should call CreateInstance) and Destructor (public should call release)
		MMFSoundPlayer();
		~MMFSoundPlayer();

		//Setup Functions
		HRESULT CreateMediaSession();
		HRESULT CreateMediaSource(PCWSTR inputFilePath);
		HRESULT CreatePlaybackTopology(IMFPresentationDescriptor* inputPresentationDescriptor, IMFTopology** outputTopology);
		HRESULT AddSourceNode(IMFTopology* inputTopology, IMFPresentationDescriptor* inputPresentationDescriptor, IMFStreamDescriptor* inputStreamDescriptor, IMFTopologyNode** sourceNode);
		HRESULT AddOutputNode(IMFTopology* inputTopology, IMFActivate* inputMediaSinkActivationObject, IMFTopologyNode** outputNode);
		
		//Destruction functions
		HRESULT CloseMediaSessionAndSource();
		
	public:
		//A static public function to create an instance of the object (needed to make object a COM object)
		static HRESULT CreateInstance(MMFSoundPlayer** outputMMFSoundPlayer);

		//Public destructor function that must be called before program ends
		HRESULT Shutdown();

		//IMFAsyncCallback methods (required for handling of events)
		STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult);
		STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue);
		
		//IUnknown methods (required for IMFAsyncCallback)
		STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
		STDMETHODIMP_(ULONG) AddRef();
		STDMETHODIMP_(ULONG) Release();

		//Audio Control
		HRESULT SetFileIntoPlayer(PCWSTR inputFilepath);
		HRESULT Play();
		HRESULT Pause();
		HRESULT Stop(); 
		HRESULT Seek(UINT64 seekPosition_100NanoSecondUnits);

		//Getters
		PlayerState GetPlayerState();
		std::wstring GetAudioFilepath();
		UINT64 GetAudioFileDuration_100NanoSecondUnits();
		UINT64 GetCurrentPresentationTime_100NanoSecondUnits();
	};
}