#pragma once
#include "../core/ECS.h"
#include <xaudio2.h>
#include <string>
#include <vector>
#include <memory>
#include <wrl/client.h>

namespace outer_wilds {

// Audio component - marks entities that have audio
struct AudioComponent {
    bool isPlaying = false;
    bool isLooping = false;
    float volume = 1.0f;
};

// Audio source - holds decoded audio data
struct AudioSource {
    std::vector<BYTE> audioData;
    WAVEFORMATEX waveFormat;
    std::string filePath;
};

class AudioSystem : public System {
public:
    AudioSystem();
    ~AudioSystem();

    bool InitializeAudio();
    void Update(float deltaTime, entt::registry& registry) override;
    void Shutdown();

    // Load and play MP3 file
    bool LoadMP3(const std::string& filePath);
    
    // Play single MP3 file (without playlist)
    bool PlaySingleTrack(const std::string& filePath);
    
    // Load all MP3 files from a directory
    bool LoadPlaylistFromDirectory(const std::string& directoryPath);
    
    // Playback control
    void Play();
    void Pause();
    void Stop();
    void SetVolume(float volume); // 0.0 to 1.0
    
    // Playlist control
    void PlayNext();
    void PlayPrevious();
    void SetLoopPlaylist(bool loop) { m_LoopPlaylist = loop; }
    
    bool IsPlaying() const { return m_IsPlaying; }
    const std::string& GetCurrentTrack() const { 
        if (m_CurrentTrackIndex >= 0 && m_CurrentTrackIndex < m_Playlist.size()) {
            return m_Playlist[m_CurrentTrackIndex];
        }
        static std::string empty;
        return empty;
    }

private:
    bool InitializeXAudio2();
    bool LoadMP3File(const std::string& filePath, AudioSource& outSource);
    bool CreateSourceVoice(const AudioSource& source);
    void CleanupSourceVoice();
    
    // XAudio2 objects
    Microsoft::WRL::ComPtr<IXAudio2> m_XAudio2;
    IXAudio2MasteringVoice* m_MasteringVoice = nullptr;
    IXAudio2SourceVoice* m_SourceVoice = nullptr;
    
    // Current audio source
    AudioSource m_CurrentSource;
    
    // Playlist management
    std::vector<std::string> m_Playlist;
    int m_CurrentTrackIndex = -1;
    bool m_LoopPlaylist = true;
    
    // Playback state
    bool m_IsPlaying = false;
    float m_Volume = 1.0f;
    
    // Check if current track finished
    bool m_CheckNextTrack = false;
};

} // namespace outer_wilds
