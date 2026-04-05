#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AudioManager
{
public:
    struct AudioBufferData
    {
        int channels{ 0 };
        int sampleRate{ 0 };
        int format{ 0 };
        std::vector<unsigned char> data;
    };

    static AudioManager& Instance();

    bool initialize();
    void shutdown();
    void update();

    void setAudioResolver(std::function<std::optional<AudioBufferData>(unsigned int)> resolver);
    void setAudioReleaseCallback(std::function<void(unsigned int)> callback);

    unsigned int createAudioHandle(unsigned int assetId, bool loop = false, float gain = 1.0f);
    bool playHandle(unsigned int handle);
    unsigned int playAudioAsset(unsigned int assetId, bool loop = false, float gain = 1.0f);
    unsigned int createAudioHandleAsync(unsigned int assetId, bool loop = false, float gain = 1.0f);
    unsigned int playAudioAssetAsync(unsigned int assetId, bool loop = false, float gain = 1.0f);
    unsigned int playAudioPathAsync(const std::string& absPath, bool loop = false, float gain = 1.0f);
    unsigned int createAudioPathAsync(const std::string& absPath, bool loop = false, float gain = 1.0f);
    unsigned int createAudioHandleAsync(unsigned int assetId, std::function<void(unsigned int)> callback, bool loop = false, float gain = 1.0f);
    unsigned int playAudioAssetAsync(unsigned int assetId, std::function<void(unsigned int)> callback, bool loop = false, float gain = 1.0f);
    bool isPendingHandle(unsigned int handle) const;
    bool pauseSource(unsigned int sourceId);
    bool setHandleGain(unsigned int handle, float gain);
    std::optional<float> getHandleGain(unsigned int handle) const;
    bool isSourcePlaying(unsigned int sourceId) const;
    bool isAssetPlaying(unsigned int assetId) const;
    bool stopSource(unsigned int sourceId);
    bool invalidateHandle(unsigned int handle);
    void stopAll();

    // 3D Spatial Audio
    bool setSourcePosition(unsigned int handle, float x, float y, float z);
    bool setSourceSpatial(unsigned int handle, bool is3D, float minDist = 1.0f, float maxDist = 50.0f, float rolloff = 1.0f);
    void updateListenerTransform(float posX, float posY, float posZ,
                                 float forwardX, float forwardY, float forwardZ,
                                 float upX, float upY, float upZ);

    bool isInitialized() const;

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    unsigned int getBufferForAsset(unsigned int assetId);

    void startLoadThread();
    void stopLoadThread();
    void loadThreadFunc();
    unsigned int allocatePendingHandle();

    struct PendingRequest
    {
        unsigned int handle{ 0 };
        unsigned int assetId{ 0 };
        std::string filePath;
        bool loop{ false };
        float gain{ 1.0f };
        bool autoPlay{ false };
        std::function<void(unsigned int)> callback;
    };

    struct CompletedRequest
    {
        unsigned int handle{ 0 };
        unsigned int assetId{ 0 };
        bool loop{ false };
        float gain{ 1.0f };
        bool autoPlay{ false };
        AudioBufferData data;
        bool success{ false };
        std::function<void(unsigned int)> callback;
    };

    void* m_device{ nullptr };
    void* m_context{ nullptr };
    bool m_initialized{ false };
    std::unordered_map<unsigned int, unsigned int> m_buffers;
    std::unordered_set<unsigned int> m_sources;
    std::unordered_map<unsigned int, unsigned int> m_sourceAssetIds;
    std::function<std::optional<AudioBufferData>(unsigned int)> m_assetResolver;
    std::function<void(unsigned int)> m_releaseCallback;
    std::mutex m_bufferCacheMutex;
    unsigned int m_nextInternalAssetId{ 0xE0000000 };

    std::thread m_loadThread;
    std::mutex m_pendingMutex;
    std::condition_variable m_pendingCv;
    std::vector<PendingRequest> m_pendingRequests;
    std::mutex m_completedMutex;
    std::vector<CompletedRequest> m_completedRequests;
    std::atomic<bool> m_loadThreadRunning{ false };
    unsigned int m_nextPendingHandle{ 0xF0000000 };
    std::unordered_set<unsigned int> m_pendingHandles;
    std::vector<unsigned int> m_finishedSources;
    uint32_t m_sourceCleanupCounter{ 0 };
};
