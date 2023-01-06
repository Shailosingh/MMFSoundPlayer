#include "MMFSoundPlayer.h"
#include "Helpers.h"
#include <mfapi.h>
#include <stdexcept>
#include <cassert>
#include <shlwapi.h>

using namespace MMFSoundPlayerLib;

//Constructor/Initialization and Destructors/Deinitialization--------------------------------------------------------------------------------------------------
MMFSoundPlayer::MMFSoundPlayer()
{
	//Initialize variables
	CurrentState = PlayerState::Closed;
	CurrentFilePath = L"No File Loaded";
	ReferenceCount = 1;
}

HRESULT MMFSoundPlayer::CreateInstance(MMFSoundPlayer** outputMMFSoundPlayer)
{
	//Ensure that the double pointer actually points somewhere
	if (outputMMFSoundPlayer == nullptr)
	{
		return E_POINTER;
	}

	//Create the object using "new" and ensure it doesn't throw exceptions, so an HRESULt can be returned
	MMFSoundPlayer* newPlayer = new (std::nothrow) MMFSoundPlayer();
	if (newPlayer == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	//Initialize the object
	HRESULT hr = newPlayer->Initialize();
	if (SUCCEEDED(hr))
	{
		//If initialization was successful, give the caller the object
		*outputMMFSoundPlayer = newPlayer;
	}
	else
	{
		//If initialization failed, release the object
		newPlayer->Release();
	}

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Initialize()
{
	//Start up the MMF library
	HRESULT hr = MFStartup(MF_VERSION);
	if (SUCCEEDED(hr))
	{
		//Setup the exit event handle
		ExitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (ExitEvent == nullptr)
		{
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
	}
	return hr;
}

MMFSoundPlayer::~MMFSoundPlayer()
{
	CloseMediaSessionAndSource();

	//Shut down the MMF library
	MFShutdown();

	//Throw away the event object
	if (ExitEvent != nullptr)
	{
		CloseHandle(ExitEvent);
	}
}

STDMETHODIMP_(ULONG) MMFSoundPlayer::Release()
{
	//Decrement the reference count
	LONG newCount = InterlockedDecrement(&ReferenceCount);

	//If the reference count is 0, delete the object
	if (newCount == 0)
	{
		delete this;
	}

	//Return the new reference count
	return newCount;
}

HRESULT MMFSoundPlayer::CloseMediaSessionAndSource()
{
	//Initialize variables
	HRESULT hr = S_OK;
	
	//Signal that session is closing up
	CurrentState = PlayerState::Closing;
	
	//Close media session (asynchronously)
	if (CurrentMediaSession != nullptr)
	{
		hr = CurrentMediaSession->Close();
		
		//Waits on MESessionClose event, so I know the Media Session is fully closed. Timeout after 10 seconds
		if (SUCCEEDED(hr))
		{
			DWORD result = WaitForSingleObject(ExitEvent, 10000);
			if (result == WAIT_TIMEOUT)
			{
				//THIS CAN NEVER HAPPEN, NO MATTER WHAT! (This means the Exit was never completed and this is catastrophic. NO RECOURSE)
				assert(false);
			}
		}
	}

	//Shutdown the media session and source
	if (SUCCEEDED(hr))
	{
		if (CurrentMediaSource != nullptr)
		{
			CurrentMediaSource->Shutdown();
		}

		if (CurrentMediaSession != nullptr)
		{
			CurrentMediaSession->Shutdown();
		}
	}

	//Change the state of the player to closed
	CurrentState = PlayerState::Closed;
}

//IUnknown and IMFAsyncCallback Implementation Functions-------------------------------------------------------------------------------------------------------
STDMETHODIMP MMFSoundPlayer::Invoke(IMFAsyncResult* pAsyncResult)
{
	//Dequeue an event from the event queue
	CComPtr<IMFMediaEvent> event;
	HRESULT hr = CurrentMediaSession->EndGetEvent(pAsyncResult, &event);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Get the event type so it can be handled
	MediaEventType eventType = MEUnknown;
	hr = event->GetType(&eventType);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Handle the event
	switch (eventType)
	{
	case MESessionClosed:
		//Signal that the session is closed
		SetEvent(ExitEvent);
		break;

	default:
		//Handle the next event
		hr = CurrentMediaSession->BeginGetEvent(this, nullptr);
		if (FAILED(hr))
		{
			assert(false);
			return hr;
		}
		break;
	}

	//Return a success code
	return S_OK;
}
STDMETHODIMP MMFSoundPlayer::GetParameters(DWORD* pdwFlags, DWORD* pdwQueue)
{
	return E_NOTIMPL;
}

STDMETHODIMP MMFSoundPlayer::QueryInterface(REFIID iid, void** ppv)
{
	static const QITAB qit[] =
	{
		QITABENT(MMFSoundPlayer, IMFAsyncCallback),
		{ 0 }
	};
	return QISearch(this, qit, iid, ppv);
}

STDMETHODIMP_(ULONG) MMFSoundPlayer::AddRef()
{
	//Atomic Increment
	return InterlockedIncrement(&ReferenceCount);
}

//Public Functions---------------------------------------------------------------------------------------------------------------------------------------------
HRESULT MMFSoundPlayer::SetFileIntoPlayer(PCWSTR inputFilePath)
{
	//Close up any current instances of the media session and source
	HRESULT hr = CloseMediaSessionAndSource();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	CurrentState = PlayerState::OpenPending;

	//Startup new media session
	hr = CreateMediaSession();
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}

	//Create new media source with new input file
	hr = CreateMediaSource(inputFilePath);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}

	//Retrieve the Presentation Desciptor for the file's media source
	CComPtr<IMFPresentationDescriptor> presentationDescriptor;
	hr = CurrentMediaSource->CreatePresentationDescriptor(&presentationDescriptor);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}

	//Use presentation descriptor to create Playback Topology
	CComPtr<IMFTopology> playbackTopology;
	hr = CreatePlaybackTopology(presentationDescriptor, &playbackTopology);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}

	//Set the playback topology into the media session
	hr = CurrentMediaSession->SetTopology(0, playbackTopology);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}
	
	//NOTE: SetTopology is asynchronous, so the state will not change and the player will not be ready until the MESessionTopologySet event is received and handled

	//Return final code
	return hr;
}

//Private Functions--------------------------------------------------------------------------------------------------------------------------------------------
HRESULT MMFSoundPlayer::CreateMediaSession()
{
	//Create the media session
	HRESULT hr = MFCreateMediaSession(nullptr, &CurrentMediaSession);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Set the media session event handler
	CurrentMediaSession->BeginGetEvent((IMFAsyncCallback*)this, NULL);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Successfully created media session
	CurrentState = PlayerState::Ready;
	return hr;
}

HRESULT MMFSoundPlayer::CreateMediaSource(PCWSTR inputFilePath)
{
	//Create source resolver
	CComPtr<IMFSourceResolver> sourceResolver;
	HRESULT hr = MFCreateSourceResolver(&sourceResolver);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	/*
	Synchrounously create the media source with the input file (Not doing it asynchronously because,
	I never use a network file and this media player is typically going to be managed in a seperate
	thread from the GUI anyways.

	The GUI can look at the "OpenPending" state and place a loading screen or something like that while
	the media source is being created.
	*/
	CComPtr<IUnknown> source;
	MF_OBJECT_TYPE objectType = MF_OBJECT_INVALID;
	hr = sourceResolver->CreateObjectFromURL(
		inputFilePath,             // URL of the source.
		MF_RESOLUTION_MEDIASOURCE, // Create a source object.
		nullptr,                    // Optional property store.
		&objectType,            // Receives the created object type. 
		&source                   // Receives a pointer to the media source.
	);
	
	//Only media sources should be allowed at this point
	assert(objectType == MF_OBJECT_MEDIASOURCE); 
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Query and get the IMFMediaSource interface from the media source.
	hr = source->QueryInterface(IID_PPV_ARGS(&CurrentMediaSource));
	
	//Return the final code
	assert(SUCCEEDED(hr));
	return hr;
}

HRESULT MMFSoundPlayer::CreatePlaybackTopology(IMFPresentationDescriptor* inputPresentationDescriptor, IMFTopology** outputTopology)
{
	//TODO
}