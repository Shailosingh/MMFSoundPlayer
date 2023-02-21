#include <iostream>
#include "../MMFSoundPlayer/MMFSoundPlayer.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <format>

namespace fs = std::filesystem;
using namespace MMFSoundPlayerLib;

//Function declarations
std::string Convert100NanoSecondsToTimestamp(UINT64 input100NanoSeconds);

//Constants
UINT64 const OneSecond_100NanoSecondUnits = 10000000;

int main()
{
   //Create media player instance
	CComPtr<MMFSoundPlayer> player = nullptr;
	HRESULT hr = MMFSoundPlayer::CreateInstance(&player);
	if (FAILED(hr))
	{
		std::cout << "Failed to create media player instance\n";
		return 1;
	}

	//Play music file
	std::wstring musicFilepath = L"C:\\Users\\compu\\Documents\\MMFSoundPlayer Test\\Gee.wav";
	hr = player->SetFileIntoPlayer(musicFilepath.c_str());
	if (FAILED(hr))
	{
		std::cout << "Failed to set file into player\n";
		return 1;
	}
	
	//Tell user what song is playing and then sleep for 20 seconds to let it play
	std::wstring songName = fs::path(player->GetAudioFilepath()).filename().wstring();
	std::wcout << "Song playing: " << songName << "\n";
	std::cout << "20 seconds of music playing...\n";
	std::this_thread::sleep_for(std::chrono::seconds(20));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Pause the music for 5 seconds
	hr = player->Pause();
	if (FAILED(hr))
	{
		std::cout << "Failed to pause music\n";
		return 1;
	}
	std::cout << "Pausing the music for 5 seconds\n";
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Resume music again for 10 seconds
	hr = player->Play();
	if (FAILED(hr))
	{
		std::cout << "Failed to resume music\n";
		return 1;
	}
	std::wcout << "Song playing: " << songName << "\n";
	std::cout << "10 seconds of music playing...\n";
	std::this_thread::sleep_for(std::chrono::seconds(10));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";
	
	//Stop the music for 5 seconds
	hr = player->Stop();
	if (FAILED(hr))
	{
		std::cout << "Failed to stop music\n";
		return 1;
	}
	std::cout << "Stopping the music for 5 seconds\n";
	std::this_thread::sleep_for(std::chrono::seconds(5));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Start the music from the top for 10 seconds
	hr = player->Play();
	if (FAILED(hr))
	{
		std::cout << "Failed to resume music\n";
		return 1;
	}
	std::wcout << L"Song playing: " << songName << "\n";
	std::cout << "10 seconds of music playing...\n";
	std::this_thread::sleep_for(std::chrono::seconds(10));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Seek to the 20 second mark of the song and play for 10 seconds
	hr = player->Seek(20 * OneSecond_100NanoSecondUnits);
	if (FAILED(hr))
	{
		std::cout << "Failed to seek to 20 second mark\n";
		return 1;
	}
	std::cout << "Seeked to 20 second mark and playing for 10 seconds\n";
	std::this_thread::sleep_for(std::chrono::seconds(10));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Seek to the final 10 seconds of the song and play for final 10 seconds plus 5 seconds (15 seconds)
	hr = player->Seek(player->GetAudioFileDuration_100NanoSecondUnits() - (10 * OneSecond_100NanoSecondUnits));
	if (FAILED(hr))
	{
		std::cout << "Failed to seek to 20 second mark\n";
		return 1;
	}
	std::cout << "Seeked to last 10 seconds and wait for 5 seconds after\n";
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n";
	std::this_thread::sleep_for(std::chrono::seconds(15));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";

	//Play new song (Oh!) from scratch for 10 seconds
	musicFilepath = L"C:\\Users\\compu\\Documents\\MMFSoundPlayer Test\\Oh!.mp3";
	hr = player->SetFileIntoPlayer(musicFilepath.c_str());
	if (FAILED(hr))
	{
		std::cout << "Failed to set file into player\n";
		return 1;
	}
	
	songName = fs::path(player->GetAudioFilepath()).filename().wstring();
	std::wcout << L"Song playing: " << songName << "\n";
	std::wcout << "10 seconds of music playing...\n";
	std::this_thread::sleep_for(std::chrono::seconds(10));
	std::cout << "Current Timestamp: " << Convert100NanoSecondsToTimestamp(player->GetCurrentPresentationTime_100NanoSecondUnits()) << "\n\n";
	
	//Shutdown the player
	std::cout << "Shutting down player\n";
	player->Shutdown();
}

std::string Convert100NanoSecondsToTimestamp(UINT64 input100NanoSeconds)
{
	UINT64 seconds = input100NanoSeconds / OneSecond_100NanoSecondUnits;
	UINT64 minutes = seconds / 60;
	seconds = seconds % 60;

	return std::format("{:02}:{:02}", minutes, seconds);
}