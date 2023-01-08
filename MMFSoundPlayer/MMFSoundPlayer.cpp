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
	
	//Startup the media session
	hr = CreateMediaSession();
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
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
		//Signal that the session is closed
		SetEvent(ExitEvent);
		break;

	case MESessionTopologySet:
		//Signal that the topology is set (thus player is stopped and ready to play), then start the music
		CurrentState = PlayerState::Stopped;
		hr = Play();
		if (FAILED(hr))
		{
			assert(false);
			return hr;
		}
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
	//TODO: Stop the session and wait for the event to ensure it is stopped before continuing

	//TODO: Destroy the current media source if it exists to make room for a new one for the new file
	
	CurrentState = PlayerState::OpenPending;

	//Create new media source with new input file
	HRESULT hr = CreateMediaSource(inputFilePath);
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

	//Set the playback topology into the media session and set flag so that the old presentation is immediately stopped and cleared before setting new topology
	hr = CurrentMediaSession->SetTopology(MFSESSION_SETTOPOLOGY_IMMEDIATE, playbackTopology);
	if (FAILED(hr))
	{
		assert(false);
		CurrentState = PlayerState::Closed;
		return hr;
	}
	
	//NOTE: SetTopology is asynchronous, so the state will not change and the player will not start playing until the MESessionTopologySet event is received and handled

	//Return final code
	return hr;
}

HRESULT MMFSoundPlayer::Play()
{
	//Ensure the player is either paused or stopped
	if (!(CurrentState == PlayerState::Paused || CurrentState == PlayerState::Stopped))
	{
		assert(false);
		return E_NOT_VALID_STATE;
	}

	//Start the session
	HRESULT hr = CurrentMediaSession->Start(&GUID_NULL, nullptr);
	if (FAILED(hr))
	{
		assert(false);
		return hr;
	}

	//Change the state of the player to indicate the music has started
	CurrentState = PlayerState::Started;

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
		return E_FAIL;
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
	*outputTopology = newTopology;
	
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
	*sourceNode = newNode;
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
	*outputNode = newNode;
	return hr;
}