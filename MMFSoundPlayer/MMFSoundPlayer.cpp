#include "MMFSoundPlayer.h"
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
	CurrentAudioFileDuration_100NanoSecondUnits = 0;
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
	if (FAILED(hr))
	{
		//If initialization failed, release the object
		assert(false);
		newPlayer->Release();
		return hr;
	}

	//If initialization was successful, give the caller the object
	*outputMMFSoundPlayer = newPlayer;

	//Return final success code
	return hr;
}

HRESULT MMFSoundPlayer::Initialize()
{
	//Start up the MMF library
	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Setup event handles
	ExitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (ExitEvent == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}

	PlayEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (PlayEvent == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}
	
	PauseEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (PauseEvent == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}
	
	StopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (StopEvent == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}

	TopologySetEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (TopologySetEvent == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}

	VolumeExternallyChanged = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (VolumeExternallyChanged == nullptr)
	{
		assert(false);
		return HRESULT_FROM_WIN32(GetLastError());
	}
	
	return hr;
}

MMFSoundPlayer::~MMFSoundPlayer()
{
	//If CreateInstance isn't called and yet, it should
	Shutdown();
}

HRESULT MMFSoundPlayer::Shutdown()
{
	HRESULT hr = CloseMediaSessionAndSource();

	//Shut down the MMF library
	hr = MFShutdown();

	//Throw away the event objects
	if (ExitEvent != nullptr)
	{
		CloseHandle(ExitEvent);
		ExitEvent = nullptr;
	}
	if (PlayEvent != nullptr)
	{
		CloseHandle(PlayEvent);
		PlayEvent = nullptr;
	}
	if (PauseEvent != nullptr)
	{
		CloseHandle(PauseEvent);
		PauseEvent = nullptr;
	}
	if (StopEvent != nullptr)
	{
		CloseHandle(StopEvent);
		StopEvent = nullptr;
	}
	if (TopologySetEvent != nullptr)
	{
		CloseHandle(TopologySetEvent);
		TopologySetEvent = nullptr;
	}
	if (VolumeExternallyChanged != nullptr)
	{
		CloseHandle(VolumeExternallyChanged);
		VolumeExternallyChanged = nullptr;
	}

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::CloseMediaSessionAndSource()
{
	//Signal that session is closing up
	CurrentState = PlayerState::Closing;
	
	//Close media session
	if (CurrentMediaSession != nullptr)
	{
		CurrentMediaSession->Close();
		
		//Waits on MESessionClose event, so I know the Media Session is fully closed. Timeout after 10 seconds. (This verifies if there is an error)
		DWORD result = WaitForSingleObject(ExitEvent, 10000);
		if (result == WAIT_TIMEOUT)
		{
			//THIS CAN NEVER HAPPEN, NO MATTER WHAT! (This means the Exit was never completed and this is catastrophic. NO RECOURSE)
			assert(false);
		}
	}

	//Shutdown the media session and source
	if (CurrentMediaSource != nullptr)
	{
		CurrentMediaSource->Shutdown();
	}
	if (CurrentMediaSession != nullptr)
	{
		CurrentMediaSession->Shutdown();
	}

	//Null the session and source for further use
	CurrentMediaSource = nullptr;
	CurrentMediaSession = nullptr;

	//Change the state of the player to closed
	CurrentState = PlayerState::Closed;

	//Return final success code
	return S_OK;
}

//IUnknown and IMFAsyncCallback Implementation Functions-------------------------------------------------------------------------------------------------------
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

	//Ensure the operation that triggered the event was not a total failure
	HRESULT operationStatus = S_OK;
	hr = event->GetStatus(&operationStatus);

	//If getting the status itself failed, return that failure code
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//If getting the status was successful but, the operation failed, return that operation failure code
	if (FAILED(operationStatus))
	{
		assert(false);
		return operationStatus;
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
		OutputDebugStringA("HANDLED EVENT: MESessionClosed\n");
		//Signal that the session is closed
		SetEvent(ExitEvent);
		break;

	case MESessionTopologySet:
		OutputDebugStringA("HANDLED EVENT: MESessionTopologySet\n");
		//Change the state of the player to show that it is stopped
		CurrentState = PlayerState::Stopped;

		//Signal that the topology is set
		SetEvent(TopologySetEvent);
		break;

	case MESessionStarted:
		OutputDebugStringA("HANDLED EVENT: MESessionStarted\n");
		//Change the state of the player to indicate the music has started playing
		CurrentState = PlayerState::Playing;

		//Signal that the player has started playing
		SetEvent(PlayEvent);
		break;

	case MESessionPaused:
		OutputDebugStringA("HANDLED EVENT: MESessionPaused\n");
		//Change the state of the player to indicate the music has paused
		CurrentState = PlayerState::Paused;

		//Signal that the player has paused
		SetEvent(PauseEvent);
		break;

	case MESessionStopped:
		OutputDebugStringA("HANDLED EVENT: MESessionStopped\n");
		//Change the state of the player to indicate the music has stopped
		CurrentState = PlayerState::Stopped;

		//Signal that the player has stopped
		SetEvent(StopEvent);
		break;

	case MEEndOfPresentation:
		OutputDebugStringA("HANDLED EVENT: MEEndOfPresentation\n");
		//Change the state of the player to indicate that the old song finished and that the new song is ready for loading if available
		CurrentState = PlayerState::PresentationEnd;
		break;

	case MEAudioSessionVolumeChanged:
		OutputDebugStringA("HANDLED EVENT: MEAudioSessionVolumeChanged\n");

		//Signal that the volume has changed from an external source (like the volume mixer)
		SetEvent(VolumeExternallyChanged);
		break;

	default:
		OutputDebugStringA("HANDLED EVENT: Unknown Event\n");
		break;
	}

	//Handle the next event if the session is not being closed (MESessionClosed is the final event)
	if (eventType != MESessionClosed)
	{
		hr = CurrentMediaSession->BeginGetEvent(this, nullptr);
		if (FAILED(hr))
		{
			assert(false);
			return hr;
		}
	}

	//Return a success code
	return S_OK;
}

//Public Functions---------------------------------------------------------------------------------------------------------------------------------------------
HRESULT MMFSoundPlayer::SetFileIntoPlayer(PCWSTR inputFilePath)
{
	//Close up any existing sessions and source
	HRESULT hr = CloseMediaSessionAndSource();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Startup the media session
	hr = CreateMediaSession();
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}

	//Reset song info
	CurrentFilePath = L"No File Loaded";
	CurrentAudioFileDuration_100NanoSecondUnits = 0;

	//Begin opening the file
	CurrentState = PlayerState::OpenPending;

	//Create new media source with new input file
	hr = CreateMediaSource(inputFilePath);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return hr;
	}

	//Retrieve the Presentation Descriptor for the file's media source
	CComPtr<IMFPresentationDescriptor> presentationDescriptor;
	hr = CurrentMediaSource->CreatePresentationDescriptor(&presentationDescriptor);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return hr;
	}

	//Use the presentation descriptor to get the file's audio duration
	UINT64 tempCurrentAudioFileDuration = 0;
	hr = presentationDescriptor->GetUINT64(MF_PD_DURATION, &tempCurrentAudioFileDuration);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return hr;
	}

	//Use presentation descriptor to create Playback Topology
	CComPtr<IMFTopology> playbackTopology;
	hr = CreatePlaybackTopology(presentationDescriptor, &playbackTopology);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return hr;
	}

	//Set the playback topology into the media session and set flag so that the old presentation is immediately stopped and cleared before setting new topology
	hr = CurrentMediaSession->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, playbackTopology);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return hr;
	}
	
	//Wait at most 3 seconds for the topology to be set
	DWORD waitResult = WaitForSingleObject(TopologySetEvent, 3000);
	if (waitResult == WAIT_TIMEOUT)
	{
		assert(false);
		CurrentState = PlayerState::Ready;
		return E_FAIL;
	}

	//Play the sound and wait at most three seconds for it to do so
	hr = Play();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Setup the current file path and audio file duration
	CurrentFilePath = inputFilePath;
	CurrentAudioFileDuration_100NanoSecondUnits = tempCurrentAudioFileDuration;

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Play()
{
	//Ensure the player is either paused or stopped. If not, ignore this call
	if (!(CurrentState == PlayerState::Paused || CurrentState == PlayerState::Stopped))
	{
		return S_OK;
	}

	//Set the audio to start playing at the current time (if stopped, will start audio track from beginning)
	PROPVARIANT varStart;
	PropVariantInit(&varStart);

	//Start the session
	HRESULT hr = CurrentMediaSession->Start(&GUID_NULL, &varStart);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Wait at most 3 seconds for song to start playing
	DWORD waitResult = WaitForSingleObject(PlayEvent, 3000);
	if (waitResult == WAIT_TIMEOUT)
	{
		assert(false);
		return E_FAIL;
	}

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Pause()
{
	//Ensure the player is currently playing. If not, ignore this call
	if (!(CurrentState == PlayerState::Playing))
	{
		return S_OK;
	}

	//Pause the session
	HRESULT hr = CurrentMediaSession->Pause();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Wait at most 3 seconds for song to pause
	DWORD waitResult = WaitForSingleObject(PauseEvent, 3000);
	if (waitResult == WAIT_TIMEOUT)
	{
		assert(false);
		return E_FAIL;
	}
	
	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Stop()
{
	//Ensure the player is either paused or playing. If not, ignore this call
	if (!(CurrentState == PlayerState::Paused || CurrentState == PlayerState::Playing))
	{
		return S_OK;
	}

	//Stop the session
	HRESULT hr = CurrentMediaSession->Stop();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Wait at most 3 seconds for song to stop
	DWORD waitResult = WaitForSingleObject(StopEvent, 3000);
	if (waitResult == WAIT_TIMEOUT)
	{
		assert(false);
		return E_FAIL;
	}

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Seek(UINT64 seekPosition_100NanoSecondUnits)
{
	//Ensure the player is either paused or playing. If not, ignore this call
	if (!(CurrentState == PlayerState::Paused || CurrentState == PlayerState::Playing))
	{
		return S_OK;
	}

	//Ensure the seek position is within the bounds of the file
	if (!(seekPosition_100NanoSecondUnits <= CurrentAudioFileDuration_100NanoSecondUnits))
	{
		assert(false);
		return E_INVALIDARG;
	}

	//Pause the session
	HRESULT hr = Pause();
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Set the start time to the seek position
	PROPVARIANT varStart;
	PropVariantInit(&varStart);
	varStart.vt = VT_I8;
	varStart.hVal.QuadPart = seekPosition_100NanoSecondUnits;

	//Start the session
	hr = CurrentMediaSession->Start(&GUID_NULL, &varStart);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Wait at most 3 seconds for song to start playing again
	DWORD waitResult = WaitForSingleObject(PlayEvent, 3000);
	if (waitResult == WAIT_TIMEOUT)
	{
		assert(false);
		return E_FAIL;
	}

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::SetVolume(float volumeLevel)
{
	//Get volume object
	CComPtr<IMFSimpleAudioVolume> simpleAudioVolume;
	HRESULT hr = MFGetService(CurrentMediaSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&simpleAudioVolume));
	if (FAILED(hr))
	{
		return hr;
	}

	//Try to set volume
	hr = simpleAudioVolume->SetMasterVolume(volumeLevel);
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
	//Create an empty topology
	CComPtr<IMFTopology> newTopology;
	HRESULT hr = MFCreateTopology(&newTopology);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Ensure that there is only one stream in the file. If there is more than one stream, then the file is not supported at this time.
	DWORD streamCount = 0;
	hr = inputPresentationDescriptor->GetStreamDescriptorCount(&streamCount);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	if (streamCount != 1)
	{
		assert(false);
		return E_INVALIDARG;
	}
	
	//Check if the single stream is audio by getting the stream descriptor
	CComPtr<IMFStreamDescriptor> streamDescriptor;
	BOOL selected = FALSE;
	hr = inputPresentationDescriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Ensure that the stream is selected. If it isn't there are serious issues with playing the file.
	if (!selected)
	{
		assert(false);
		return E_FAIL;
	}

	//Check the media type by getting the media type handler and checking its major type. If it is non-audio, it is not supported
	CComPtr<IMFMediaTypeHandler> mediaTypeHandler;
	hr = streamDescriptor->GetMediaTypeHandler(&mediaTypeHandler);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	GUID majorType;
	hr = mediaTypeHandler->GetMajorType(&majorType);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	if (majorType != MFMediaType_Audio)
	{
		assert(false);
		return E_INVALIDARG;
	}

	//Create media sink for SAR (Streaming Audio Renderer)
	CComPtr<IMFActivate> mediaSinkActivationObject;
	hr = MFCreateAudioRendererActivate(&mediaSinkActivationObject);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Add Source Node to the topology
	CComPtr<IMFTopologyNode> sourceNode;
	hr = AddSourceNode(newTopology, inputPresentationDescriptor, streamDescriptor, &sourceNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Add Output Node to the topology
	CComPtr<IMFTopologyNode> outputNode;
	hr = AddOutputNode(newTopology, mediaSinkActivationObject, &outputNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Connect the source node to the output node
	hr = sourceNode->ConnectOutput(0, outputNode, 0);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Give the caller the pointer to newTopology through the output parameter
	*outputTopology = newTopology.Detach();
	
	//Return the final code
	return hr;
}


HRESULT MMFSoundPlayer::AddSourceNode(IMFTopology* inputTopology, IMFPresentationDescriptor* inputPresentationDescriptor, IMFStreamDescriptor* inputStreamDescriptor, IMFTopologyNode** sourceNode)
{
	//Create the input node
	CComPtr<IMFTopologyNode> newNode;
	HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &newNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Load the media source, presentation descriptor, and stream descriptor into the node for the topology
	hr = newNode->SetUnknown(MF_TOPONODE_SOURCE, CurrentMediaSource);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	hr = newNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, inputPresentationDescriptor);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	hr = newNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, inputStreamDescriptor);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Finally add the node to the topology
	hr = inputTopology->AddNode(newNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Return the newNode pointer to caller through outputNode and return the final code
	*sourceNode = newNode.Detach();
	return hr;
}

HRESULT MMFSoundPlayer::AddOutputNode(IMFTopology* inputTopology, IMFActivate* inputMediaSinkActivationObject, IMFTopologyNode** outputNode)
{
	//Create the output node
	CComPtr<IMFTopologyNode> newNode;
	HRESULT hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &newNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Bind the media sink activation object to the output node
	hr = newNode->SetObject(inputMediaSinkActivationObject);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Ensure that the node is shut down when the topology is swapped out
	hr = newNode->SetUINT32(MF_TOPONODE_NOSHUTDOWN_ON_REMOVE, FALSE);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}
	
	//Finally add the node to the topology
	hr = inputTopology->AddNode(newNode);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Return the newNode pointer to caller through outputNode and return the final code
	*outputNode = newNode.Detach();
	return hr;
}

//Getters------------------------------------------------------------------------------------------------------------------------------------------------------
PlayerState MMFSoundPlayer::GetPlayerState()
{
	return CurrentState;
}

std::wstring MMFSoundPlayer::GetAudioFilepath()
{
	return CurrentFilePath;
}

UINT64 MMFSoundPlayer::GetAudioFileDuration_100NanoSecondUnits()
{
	return CurrentAudioFileDuration_100NanoSecondUnits;
}

UINT64 MMFSoundPlayer::GetCurrentPresentationTime_100NanoSecondUnits()
{
	//If the session is nulled, return 0
	if (CurrentMediaSession == nullptr)
	{
		return 0;
	}

	//Get the media session's clock and if it is unavailable, return 0
	CComPtr<IMFClock> mediaSessionClock;
	HRESULT hr = CurrentMediaSession->GetClock(&mediaSessionClock);
	if (FAILED(hr))
	{
		return 0;
	}

	//Query the media session clock for a presentation clock and if there is an error, return 0
	CComPtr<IMFPresentationClock> presentationClock;
	hr = mediaSessionClock->QueryInterface(IID_PPV_ARGS(&presentationClock));
	if (FAILED(hr))
	{
		return 0;
	}

	//Return the current time of the presentation. Return 0 if there is an error
	MFTIME currentPresentationTime = 0;
	hr = presentationClock->GetTime(&currentPresentationTime);
	if (FAILED(hr))
	{
		return 0;
	}
	return currentPresentationTime;
}

HRESULT MMFSoundPlayer::GetVolumeLevel(float& currentVolumeLevel)
{
	//Get volume object
	CComPtr<IMFSimpleAudioVolume> simpleAudioVolume;
	HRESULT hr = MFGetService(CurrentMediaSession, MR_POLICY_VOLUME_SERVICE, IID_PPV_ARGS(&simpleAudioVolume));
	if (FAILED(hr))
	{
		return hr;
	}

	//Try to retrieve the volume
	hr = simpleAudioVolume->GetMasterVolume(&currentVolumeLevel);
	return hr;
}