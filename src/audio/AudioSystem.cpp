#include "AudioSystem.h"
#define MINIMP3_IMPLEMENTATION
#include <minimp3/minimp3_ex.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace outer_wilds {

static bool s_AudioDebug = false;

AudioSystem::AudioSystem() {
}

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::InitializeAudio() {
    if (!InitializeXAudio2()) {
        std::cerr << "Failed to initialize XAudio2" << std::endl;
        return false;
    }
    

    return true;
}

bool AudioSystem::InitializeXAudio2() {
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "Failed to initialize COM" << std::endl;
        return false;
    }
    
    // Create XAudio2 engine
    hr = XAudio2Create(m_XAudio2.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        std::cerr << "Failed to create XAudio2 engine" << std::endl;
        return false;
    }
    
    // Create mastering voice
    hr = m_XAudio2->CreateMasteringVoice(&m_MasteringVoice);
    if (FAILED(hr)) {
        std::cerr << "Failed to create mastering voice" << std::endl;
        return false;
    }
    
    return true;
}

void AudioSystem::Update(float deltaTime, entt::registry& registry) {
    if (!m_IsPlaying || !m_SourceVoice) {
        return;
    }
    
    // Check if the current track has finished
    XAUDIO2_VOICE_STATE state;
    m_SourceVoice->GetState(&state);
    
    if (state.BuffersQueued == 0) {
        // Current track finished, play next
        if (m_LoopPlaylist && !m_Playlist.empty()) {
            PlayNext();
        } else {
            m_IsPlaying = false;
        }
    }
}

void AudioSystem::Shutdown() {
    Stop();
    CleanupSourceVoice();
    
    if (m_MasteringVoice) {
        m_MasteringVoice->DestroyVoice();
        m_MasteringVoice = nullptr;
    }
    
    m_XAudio2.Reset();
}

bool AudioSystem::LoadMP3File(const std::string& filePath, AudioSource& outSource) {
    mp3dec_t mp3d;
    mp3dec_file_info_t info;
    
    // Decode MP3 file
    if (mp3dec_load(&mp3d, filePath.c_str(), &info, nullptr, nullptr) != 0) {
        std::cerr << "Failed to decode MP3 file: " << filePath << std::endl;
        return false;
    }
    
    // Convert to bytes
    size_t dataSize = info.samples * sizeof(mp3d_sample_t);
    outSource.audioData.resize(dataSize);
    memcpy(outSource.audioData.data(), info.buffer, dataSize);
    
    // Set up wave format
    outSource.waveFormat = {};
    outSource.waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    outSource.waveFormat.nChannels = info.channels;
    outSource.waveFormat.nSamplesPerSec = info.hz;
    outSource.waveFormat.wBitsPerSample = sizeof(mp3d_sample_t) * 8;
    outSource.waveFormat.nBlockAlign = (outSource.waveFormat.nChannels * outSource.waveFormat.wBitsPerSample) / 8;
    outSource.waveFormat.nAvgBytesPerSec = outSource.waveFormat.nSamplesPerSec * outSource.waveFormat.nBlockAlign;
    outSource.waveFormat.cbSize = 0;
    
    outSource.filePath = filePath;
    
    // Free minimp3 buffer
    free(info.buffer);
    
    return true;
}

bool AudioSystem::LoadMP3(const std::string& filePath) {
    // Stop current playback
    Stop();
    
    // Load the MP3 file
    if (!LoadMP3File(filePath, m_CurrentSource)) {
        return false;
    }
    
    // Create source voice
    if (!CreateSourceVoice(m_CurrentSource)) {
        return false;
    }
    
    // Clear playlist and add this single file
    m_Playlist.clear();
    m_Playlist.push_back(filePath);
    m_CurrentTrackIndex = 0;
    
    return true;
}

bool AudioSystem::PlaySingleTrack(const std::string& filePath) {
    // Stop current playback
    Stop();

    // Load the MP3 file
    if (!LoadMP3File(filePath, m_CurrentSource)) {
        return false;
    }

    // Create source voice
    if (!CreateSourceVoice(m_CurrentSource)) {
        return false;
    }

    // Don't set up playlist, just play this one track
    m_Playlist.clear();
    m_CurrentTrackIndex = -1;

    // Start playback immediately
    Play();

    return true;
}

bool AudioSystem::LoadPlaylistFromDirectory(const std::string& directoryPath) {
    namespace fs = std::filesystem;
    
    // Clear existing playlist
    m_Playlist.clear();
    
    try {
        // Scan directory for MP3 files
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                
                if (ext == ".mp3") {
                    m_Playlist.push_back(entry.path().string());
                }
            }
        }
        
        // Sort playlist alphabetically
        std::sort(m_Playlist.begin(), m_Playlist.end());
        
        if (m_Playlist.empty()) {
            std::cerr << "No MP3 files found in directory: " << directoryPath << std::endl;
            return false;
        }
        if(s_AudioDebug){
        std::cout << "Loaded " << m_Playlist.size() << " tracks from: " << directoryPath << std::endl;
        }
        // Load and play the first track
        m_CurrentTrackIndex = 0;
        if (!LoadMP3(m_Playlist[m_CurrentTrackIndex])) {
            return false;
        }
        
        return true;
        
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
        return false;
    }
}

bool AudioSystem::CreateSourceVoice(const AudioSource& source) {
    CleanupSourceVoice();
    
    HRESULT hr = m_XAudio2->CreateSourceVoice(&m_SourceVoice, &source.waveFormat);
    if (FAILED(hr)) {
        std::cerr << "Failed to create source voice" << std::endl;
        return false;
    }
    
    return true;
}

void AudioSystem::CleanupSourceVoice() {
    if (m_SourceVoice) {
        m_SourceVoice->Stop();
        m_SourceVoice->FlushSourceBuffers();
        m_SourceVoice->DestroyVoice();
        m_SourceVoice = nullptr;
    }
}

void AudioSystem::Play() {
    if (!m_SourceVoice || m_CurrentSource.audioData.empty()) {
        std::cerr << "No audio loaded" << std::endl;
        return;
    }
    
    if (m_IsPlaying) {
        return; // Already playing
    }
    
    // Submit audio buffer
    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = static_cast<UINT32>(m_CurrentSource.audioData.size());
    buffer.pAudioData = m_CurrentSource.audioData.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    
    HRESULT hr = m_SourceVoice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to submit source buffer" << std::endl;
        return;
    }
    
    // Start playback
    hr = m_SourceVoice->Start(0);
    if (FAILED(hr)) {
        std::cerr << "Failed to start playback" << std::endl;
        return;
    }
    
    m_IsPlaying = true;
    std::cout << "Playing: " << m_CurrentSource.filePath << std::endl;
}

void AudioSystem::Pause() {
    if (m_SourceVoice && m_IsPlaying) {
        m_SourceVoice->Stop();
        m_IsPlaying = false;
    }
}

void AudioSystem::Stop() {
    if (m_SourceVoice) {
        m_SourceVoice->Stop();
        m_SourceVoice->FlushSourceBuffers();
        m_IsPlaying = false;
    }
}

void AudioSystem::SetVolume(float volume) {
    m_Volume = (std::max)(0.0f, (std::min)(1.0f, volume));
    if (m_SourceVoice) {
        m_SourceVoice->SetVolume(m_Volume);
    }
}

void AudioSystem::PlayNext() {
    if (m_Playlist.empty()) {
        return;
    }
    
    m_CurrentTrackIndex++;
    if (m_CurrentTrackIndex >= m_Playlist.size()) {
        if (m_LoopPlaylist) {
            m_CurrentTrackIndex = 0; // Loop back to first track
        } else {
            m_IsPlaying = false;
            return;
        }
    }
    
    // Load and play the next track
    Stop();
    if (LoadMP3(m_Playlist[m_CurrentTrackIndex])) {
        Play();
    }
}

void AudioSystem::PlayPrevious() {
    if (m_Playlist.empty()) {
        return;
    }
    
    m_CurrentTrackIndex--;
    if (m_CurrentTrackIndex < 0) {
        m_CurrentTrackIndex = m_Playlist.size() - 1; // Wrap to last track
    }
    
    // Load and play the previous track
    Stop();
    if (LoadMP3(m_Playlist[m_CurrentTrackIndex])) {
        Play();
    }
}

} // namespace outer_wilds
