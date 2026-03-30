#include "AudioManager.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <SDL3/SDL_audio.h>
#include "../Logger/Logger.h"

AudioManager& AudioManager::Instance()
{
    static AudioManager instance;
    return instance;
}

bool AudioManager::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    m_device = alcOpenDevice(nullptr);
    if (!m_device)
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: failed to open audio device", Logger::LogLevel::ERROR);
        return false;
    }

    m_context = alcCreateContext(static_cast<ALCdevice*>(m_device), nullptr);
    if (!m_context)
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: failed to create context", Logger::LogLevel::ERROR);
        alcCloseDevice(static_cast<ALCdevice*>(m_device));
        m_device = nullptr;
        return false;
    }

    if (!alcMakeContextCurrent(static_cast<ALCcontext*>(m_context)))
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: failed to activate context", Logger::LogLevel::ERROR);
        alcDestroyContext(static_cast<ALCcontext*>(m_context));
        alcCloseDevice(static_cast<ALCdevice*>(m_device));
        m_context = nullptr;
        m_device = nullptr;
        return false;
    }

    m_initialized = true;
    startLoadThread();
    return true;
}

void AudioManager::setAudioResolver(std::function<std::optional<AudioBufferData>(unsigned int)> resolver)
{
    m_assetResolver = std::move(resolver);
}

void AudioManager::setAudioReleaseCallback(std::function<void(unsigned int)> callback)
{
    m_releaseCallback = std::move(callback);
}

void AudioManager::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    stopLoadThread();
    stopAll();

    for (const auto& [assetId, buffer] : m_buffers)
    {
        ALuint alBuffer = static_cast<ALuint>(buffer);
        alDeleteBuffers(1, &alBuffer);
    }
    m_buffers.clear();
    m_sourceAssetIds.clear();

    if (m_context)
    {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(static_cast<ALCcontext*>(m_context));
        m_context = nullptr;
    }

    if (m_device)
    {
        alcCloseDevice(static_cast<ALCdevice*>(m_device));
        m_device = nullptr;
    }

    m_initialized = false;
}

void AudioManager::update()
{
    if (!m_initialized)
    {
        return;
    }

    // Process completed async load requests on the main thread (AL calls require main thread context)
    {
        std::vector<CompletedRequest> completed;
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            completed.swap(m_completedRequests);
        }

        for (auto& req : completed)
        {
            m_pendingHandles.erase(req.handle);

            if (!req.success)
            {
                Logger::Instance().log(Logger::Category::Engine,
                    "OpenAL: async audio load failed (handle=" + std::to_string(req.handle) + ")",
                    Logger::LogLevel::WARNING);
                if (req.callback)
                {
                    req.callback(0);
                }
                continue;
            }

            unsigned int alBuffer = 0;
            if (req.assetId != 0)
            {
                alBuffer = getBufferForAsset(req.assetId);
            }

            if (alBuffer == 0)
            {
                // Buffer not cached yet — create it from the data the background thread resolved
                const auto& bd = req.data;
                if (bd.channels <= 0 || bd.sampleRate <= 0 || bd.data.empty())
                {
                    continue;
                }

                ALenum alFormat = 0;
                if (bd.format == SDL_AUDIO_U8)
                {
                    alFormat = (bd.channels == 1) ? AL_FORMAT_MONO8 : AL_FORMAT_STEREO8;
                }
                else if (bd.format == SDL_AUDIO_S16LE || bd.format == SDL_AUDIO_S16BE || bd.format == SDL_AUDIO_S16)
                {
                    alFormat = (bd.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
                }

                if (alFormat == 0)
                {
                    continue;
                }

                ALuint buf = 0;
                alGenBuffers(1, &buf);
                alBufferData(buf, alFormat, bd.data.data(), static_cast<ALsizei>(bd.data.size()), bd.sampleRate);

                if (req.assetId != 0)
                {
                    m_buffers[req.assetId] = static_cast<unsigned int>(buf);
                }
                alBuffer = static_cast<unsigned int>(buf);
            }

            ALuint source = 0;
            alGenSources(1, &source);
            alSourcei(source, AL_BUFFER, static_cast<ALint>(alBuffer));
            alSourcef(source, AL_GAIN, req.gain);
            alSourcei(source, AL_LOOPING, req.loop ? AL_TRUE : AL_FALSE);

            m_sources.insert(static_cast<unsigned int>(source));
            m_sourceAssetIds[static_cast<unsigned int>(source)] = req.assetId;

            if (req.autoPlay)
            {
                alSourcePlay(source);
            }

            if (req.callback)
            {
                req.callback(static_cast<unsigned int>(source));
            }
        }
    }

    if (!m_sources.empty() && ++m_sourceCleanupCounter >= 10)
    {
        m_sourceCleanupCounter = 0;
        m_finishedSources.clear();
        for (const auto sourceId : m_sources)
        {
            ALuint source = static_cast<ALuint>(sourceId);
            ALint state = 0;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED)
            {
                m_finishedSources.push_back(sourceId);
            }
        }
        for (const auto sourceId : m_finishedSources)
        {
            invalidateHandle(sourceId);
        }
    }
}

unsigned int AudioManager::createAudioHandle(unsigned int assetId, bool loop, float gain)
{
    if (!m_initialized)
    {
        return 0;
    }

    const unsigned int buffer = getBufferForAsset(assetId);
    if (buffer == 0)
    {
        return 0;
    }

    ALuint source = 0;
    alGenSources(1, &source);
    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
    alSourcef(source, AL_GAIN, gain);
    alSourcei(source, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);

    m_sources.insert(static_cast<unsigned int>(source));
    m_sourceAssetIds[static_cast<unsigned int>(source)] = assetId;
    return static_cast<unsigned int>(source);
}

bool AudioManager::playHandle(unsigned int handle)
{
    auto it = m_sources.find(handle);
    if (it == m_sources.end())
    {
        return false;
    }

    ALuint source = static_cast<ALuint>(handle);
    alSourcePlay(source);
    return true;
}

bool AudioManager::setHandleGain(unsigned int handle, float gain)
{
    auto it = m_sources.find(handle);
    if (it == m_sources.end())
    {
        return false;
    }

    ALuint source = static_cast<ALuint>(handle);
    alSourcef(source, AL_GAIN, gain);
    return true;
}

std::optional<float> AudioManager::getHandleGain(unsigned int handle) const
{
    auto it = m_sources.find(handle);
    if (it == m_sources.end())
    {
        return std::nullopt;
    }

    ALuint source = static_cast<ALuint>(handle);
    ALfloat gain = 0.0f;
    alGetSourcef(source, AL_GAIN, &gain);
    return gain;
}

unsigned int AudioManager::playAudioAsset(unsigned int assetId, bool loop, float gain)
{
    const unsigned int handle = createAudioHandle(assetId, loop, gain);
    if (handle == 0)
    {
        return 0;
    }

    if (!playHandle(handle))
    {
        invalidateHandle(handle);
        return 0;
    }

    return handle;
}

bool AudioManager::pauseSource(unsigned int sourceId)
{
    auto it = m_sources.find(sourceId);
    if (it == m_sources.end())
    {
        return false;
    }

    ALuint source = static_cast<ALuint>(sourceId);
    alSourcePause(source);
    return true;
}

bool AudioManager::isSourcePlaying(unsigned int sourceId) const
{
    auto it = m_sources.find(sourceId);
    if (it == m_sources.end())
    {
        return false;
    }

    ALuint source = static_cast<ALuint>(sourceId);
    ALint state = 0;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

bool AudioManager::isAssetPlaying(unsigned int assetId) const
{
    for (const auto& [sourceId, sourceAssetId] : m_sourceAssetIds)
    {
        if (sourceAssetId != assetId)
        {
            continue;
        }
        if (isSourcePlaying(sourceId))
        {
            return true;
        }
    }
    return false;
}

bool AudioManager::stopSource(unsigned int sourceId)
{
    return invalidateHandle(sourceId);
}

bool AudioManager::invalidateHandle(unsigned int handle)
{
    auto it = m_sources.find(handle);
    if (it == m_sources.end())
    {
        return false;
    }

    unsigned int assetId = 0;
    if (auto assetIt = m_sourceAssetIds.find(handle); assetIt != m_sourceAssetIds.end())
    {
        assetId = assetIt->second;
        m_sourceAssetIds.erase(assetIt);
    }

    ALuint source = static_cast<ALuint>(handle);
    alSourceStop(source);
    alDeleteSources(1, &source);
    m_sources.erase(it);

    if (assetId != 0 && m_releaseCallback)
    {
        m_releaseCallback(assetId);
    }

    return true;
}

void AudioManager::stopAll()
{
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_completedMutex);
        m_completedRequests.clear();
    }
    m_pendingHandles.clear();

    std::vector<unsigned int> sources(m_sources.begin(), m_sources.end());
    for (const auto sourceId : sources)
    {
        invalidateHandle(sourceId);
    }
}

bool AudioManager::isInitialized() const
{
    return m_initialized;
}

unsigned int AudioManager::getBufferForAsset(unsigned int assetId)
{
    if (assetId == 0)
    {
        return 0;
    }

    auto existing = m_buffers.find(assetId);
    if (existing != m_buffers.end())
    {
        return existing->second;
    }

    if (!m_assetResolver)
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: audio resolver not set", Logger::LogLevel::ERROR);
        return 0;
    }

    auto resolved = m_assetResolver(assetId);
    if (!resolved)
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: audio asset not loaded", Logger::LogLevel::WARNING);
        return 0;
    }

    const int channels = resolved->channels;
    const int sampleRate = resolved->sampleRate;
    const int format = resolved->format;
    const auto& samples = resolved->data;

    if (channels <= 0 || sampleRate <= 0 || samples.empty())
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: audio asset missing PCM data", Logger::LogLevel::ERROR);
        return 0;
    }

    ALenum alFormat = 0;
    if (format == SDL_AUDIO_U8)
    {
        alFormat = (channels == 1) ? AL_FORMAT_MONO8 : AL_FORMAT_STEREO8;
    }
    else if (format == SDL_AUDIO_S16LE || format == SDL_AUDIO_S16BE || format == SDL_AUDIO_S16)
    {
        alFormat = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    }

    if (alFormat == 0)
    {
        Logger::Instance().log(Logger::Category::Engine, "OpenAL: unsupported audio format", Logger::LogLevel::ERROR);
        return 0;
    }

    ALuint buffer = 0;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, alFormat, samples.data(), static_cast<ALsizei>(samples.size()), sampleRate);

    m_buffers[assetId] = static_cast<unsigned int>(buffer);
    return static_cast<unsigned int>(buffer);
}

void AudioManager::startLoadThread()
{
    if (m_loadThreadRunning.load())
    {
        return;
    }

    m_loadThreadRunning.store(true);
    m_loadThread = std::thread(&AudioManager::loadThreadFunc, this);
}

void AudioManager::stopLoadThread()
{
    if (!m_loadThreadRunning.load())
    {
        return;
    }

    m_loadThreadRunning.store(false);
    m_pendingCv.notify_all();

    if (m_loadThread.joinable())
    {
        m_loadThread.join();
    }
}

void AudioManager::loadThreadFunc()
{
    while (m_loadThreadRunning.load())
    {
        std::vector<PendingRequest> batch;
        {
            std::unique_lock<std::mutex> lock(m_pendingMutex);
            m_pendingCv.wait(lock, [this]()
                {
                    return !m_pendingRequests.empty() || !m_loadThreadRunning.load();
                });

            if (!m_loadThreadRunning.load() && m_pendingRequests.empty())
            {
                break;
            }

            batch.swap(m_pendingRequests);
        }

        for (auto& req : batch)
        {
            if (!m_loadThreadRunning.load())
            {
                break;
            }

            CompletedRequest result;
            result.handle = req.handle;
            result.assetId = req.assetId;
            result.loop = req.loop;
            result.gain = req.gain;
            result.autoPlay = req.autoPlay;
            result.callback = std::move(req.callback);
            result.success = false;

            if (!req.filePath.empty())
            {
                SDL_AudioSpec spec{};
                Uint8* wavBuffer = nullptr;
                Uint32 wavLength = 0;
                if (SDL_LoadWAV(req.filePath.c_str(), &spec, &wavBuffer, &wavLength))
                {
                    result.data.channels = static_cast<int>(spec.channels);
                    result.data.sampleRate = static_cast<int>(spec.freq);
                    result.data.format = static_cast<int>(spec.format);
                    result.data.data.assign(wavBuffer, wavBuffer + wavLength);
                    SDL_free(wavBuffer);
                    result.success = true;
                }
            }
            else if (m_assetResolver && req.assetId != 0)
            {
                auto resolved = m_assetResolver(req.assetId);
                if (resolved)
                {
                    result.data = std::move(*resolved);
                    result.success = true;
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_completedMutex);
                m_completedRequests.push_back(std::move(result));
            }
        }
    }
}

unsigned int AudioManager::allocatePendingHandle()
{
    unsigned int handle = m_nextPendingHandle++;
    if (m_nextPendingHandle == 0)
    {
        m_nextPendingHandle = 0xF0000000;
    }
    m_pendingHandles.insert(handle);
    return handle;
}

unsigned int AudioManager::createAudioHandleAsync(unsigned int assetId, bool loop, float gain)
{
    if (!m_initialized || assetId == 0)
    {
        return 0;
    }

    // If buffer is already cached, create synchronously (fast path, no I/O)
    auto existing = m_buffers.find(assetId);
    if (existing != m_buffers.end())
    {
        return createAudioHandle(assetId, loop, gain);
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.assetId = assetId;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = false;

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(req);
    }
    m_pendingCv.notify_one();

    return handle;
}

unsigned int AudioManager::createAudioHandleAsync(unsigned int assetId, std::function<void(unsigned int)> callback, bool loop, float gain)
{
    if (!m_initialized || assetId == 0)
    {
        return 0;
    }

    auto existing = m_buffers.find(assetId);
    if (existing != m_buffers.end())
    {
        unsigned int h = createAudioHandle(assetId, loop, gain);
        if (callback && h != 0)
        {
            callback(h);
        }
        return h;
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.assetId = assetId;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = false;
    req.callback = std::move(callback);

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(std::move(req));
    }
    m_pendingCv.notify_one();

    return handle;
}

unsigned int AudioManager::playAudioAssetAsync(unsigned int assetId, std::function<void(unsigned int)> callback, bool loop, float gain)
{
    if (!m_initialized || assetId == 0)
    {
        return 0;
    }

    auto existing = m_buffers.find(assetId);
    if (existing != m_buffers.end())
    {
        unsigned int h = playAudioAsset(assetId, loop, gain);
        if (callback && h != 0)
        {
            callback(h);
        }
        return h;
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.assetId = assetId;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = true;
    req.callback = std::move(callback);

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(std::move(req));
    }
    m_pendingCv.notify_one();

    return handle;
}

unsigned int AudioManager::playAudioAssetAsync(unsigned int assetId, bool loop, float gain)
{
    if (!m_initialized || assetId == 0)
    {
        return 0;
    }

    // If buffer is already cached, play synchronously (fast path, no I/O)
    auto existing = m_buffers.find(assetId);
    if (existing != m_buffers.end())
    {
        return playAudioAsset(assetId, loop, gain);
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.assetId = assetId;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = true;

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(req);
    }
    m_pendingCv.notify_one();

    return handle;
}

bool AudioManager::isPendingHandle(unsigned int handle) const
{
    return m_pendingHandles.count(handle) > 0;
}

unsigned int AudioManager::createAudioPathAsync(const std::string& absPath, bool loop, float gain)
{
    if (!m_initialized || absPath.empty())
    {
        return 0;
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.filePath = absPath;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = false;

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(std::move(req));
    }
    m_pendingCv.notify_one();

    return handle;
}

unsigned int AudioManager::playAudioPathAsync(const std::string& absPath, bool loop, float gain)
{
    if (!m_initialized || absPath.empty())
    {
        return 0;
    }

    unsigned int handle = allocatePendingHandle();

    PendingRequest req;
    req.handle = handle;
    req.filePath = absPath;
    req.loop = loop;
    req.gain = gain;
    req.autoPlay = true;

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingRequests.push_back(std::move(req));
    }
    m_pendingCv.notify_one();

    return handle;
}
