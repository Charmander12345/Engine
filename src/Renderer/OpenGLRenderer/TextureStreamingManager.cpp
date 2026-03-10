#include "TextureStreamingManager.h"

#include "OpenGLTexture.h"
#include "../Texture.h"
#include "../../Logger/Logger.h"
#include "gl.h"

TextureStreamingManager::TextureStreamingManager() = default;

TextureStreamingManager::~TextureStreamingManager()
{
    shutdown();
}

bool TextureStreamingManager::initialize()
{
    if (m_initialized.load(std::memory_order_relaxed))
        return true;

    // Create 1×1 magenta placeholder texture
    {
        auto placeholderCpu = std::make_shared<Texture>();
        placeholderCpu->setWidth(1);
        placeholderCpu->setHeight(1);
        placeholderCpu->setChannels(4);
        std::vector<unsigned char> magenta = { 255, 0, 255, 255 };
        placeholderCpu->setData(std::move(magenta));

        m_placeholder = std::make_shared<OpenGLTexture>();
        if (!m_placeholder->initialize(*placeholderCpu))
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "TextureStreamingManager: failed to create placeholder texture",
                Logger::LogLevel::ERROR);
            return false;
        }
    }

    // Start background loader thread
    m_shutdownRequested.store(false, std::memory_order_relaxed);
    m_loaderThread = std::thread(&TextureStreamingManager::loaderThreadFunc, this);

    m_initialized.store(true, std::memory_order_release);

    Logger::Instance().log(Logger::Category::Rendering,
        "TextureStreamingManager: initialized (placeholder + loader thread)",
        Logger::LogLevel::INFO);
    return true;
}

void TextureStreamingManager::shutdown()
{
    if (!m_initialized.load(std::memory_order_relaxed))
        return;

    m_shutdownRequested.store(true, std::memory_order_release);
    if (m_loaderThread.joinable())
        m_loaderThread.join();

    // Drain queues
    {
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        while (!m_loadQueue.empty()) m_loadQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
        while (!m_uploadQueue.empty()) m_uploadQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_streamCache.clear();
    }

    m_placeholder.reset();
    m_initialized.store(false, std::memory_order_relaxed);

    Logger::Instance().log(Logger::Category::Rendering,
        "TextureStreamingManager: shut down",
        Logger::LogLevel::INFO);
}

std::shared_ptr<OpenGLTexture> TextureStreamingManager::streamTexture(
    const std::shared_ptr<Texture>& cpuTexture,
    std::function<void()> onReady)
{
    if (!cpuTexture || !m_initialized.load(std::memory_order_acquire))
        return nullptr;

    const Texture* key = cpuTexture.get();

    // De-duplicate: check if we already have a GPU texture for this CPU texture
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_streamCache.find(key);
        if (it != m_streamCache.end())
        {
            if (auto existing = it->second.lock())
                return existing;
            m_streamCache.erase(it);
        }
    }

    // The CPU texture already has its pixel data loaded (from AssetManager).
    // We create a GPU texture handle that initially copies the placeholder,
    // then enqueue the real data for GPU upload on the render thread.
    auto gpuTexture = std::make_shared<OpenGLTexture>();

    // Initialize with placeholder data (1×1 magenta) so it's valid immediately
    {
        Texture tiny;
        tiny.setWidth(1);
        tiny.setHeight(1);
        tiny.setChannels(4);
        std::vector<unsigned char> magenta = { 255, 0, 255, 255 };
        tiny.setData(std::move(magenta));
        gpuTexture->initialize(tiny);
    }

    // Cache it
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_streamCache[key] = gpuTexture;
    }

    // If the CPU texture already has pixel data loaded, skip the loader thread
    // and go straight to the upload queue.
    bool hasData = false;
    if (cpuTexture->isCompressed())
    {
        hasData = !cpuTexture->getCompressedMips().empty();
    }
    else
    {
        hasData = !cpuTexture->getData().empty();
    }

    if (hasData)
    {
        // Data already in memory → enqueue directly for GPU upload
        std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
        m_uploadQueue.push({ cpuTexture, gpuTexture, std::move(onReady) });
    }
    else
    {
        // Need disk load first → enqueue to loader thread
        std::lock_guard<std::mutex> lock(m_loadQueueMutex);
        m_loadQueue.push({ cpuTexture, gpuTexture, std::move(onReady) });
    }

    return gpuTexture;
}

void TextureStreamingManager::processUploads(int maxUploadsPerFrame)
{
    if (!m_initialized.load(std::memory_order_acquire))
        return;

    int uploaded = 0;
    while (uploaded < maxUploadsPerFrame)
    {
        UploadRequest req;
        {
            std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
            if (m_uploadQueue.empty())
                break;
            req = std::move(m_uploadQueue.front());
            m_uploadQueue.pop();
        }

        if (!req.cpuTexture || !req.targetGpuTexture)
            continue;

        // Re-initialize the GPU texture with the real data.
        // This replaces the placeholder 1×1 texture.
        if (req.targetGpuTexture->initialize(*req.cpuTexture))
        {
            ++uploaded;
        }
        else
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "TextureStreamingManager: failed to upload texture to GPU",
                Logger::LogLevel::WARNING);
        }

        // Fire the ready callback
        if (req.onReady)
            req.onReady();
    }
}

size_t TextureStreamingManager::getPendingCount() const
{
    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_loadQueueMutex));
        count += m_loadQueue.size();
    }
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_uploadQueueMutex));
        count += m_uploadQueue.size();
    }
    return count;
}

void TextureStreamingManager::loaderThreadFunc()
{
    auto& logger = Logger::Instance();

    while (!m_shutdownRequested.load(std::memory_order_acquire))
    {
        LoadJob job;
        bool hasJob = false;

        {
            std::lock_guard<std::mutex> lock(m_loadQueueMutex);
            if (!m_loadQueue.empty())
            {
                job = std::move(m_loadQueue.front());
                m_loadQueue.pop();
                hasJob = true;
            }
        }

        if (!hasJob)
        {
            // Sleep briefly to avoid busy-spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // The CPU texture should already have data loaded by AssetManager.
        // If for some reason it doesn't, log and skip.
        bool hasData = false;
        if (job.cpuTexture->isCompressed())
        {
            hasData = !job.cpuTexture->getCompressedMips().empty();
        }
        else
        {
            hasData = !job.cpuTexture->getData().empty();
        }

        if (!hasData)
        {
            logger.log(Logger::Category::Rendering,
                "TextureStreamingManager: loader thread — texture has no data, skipping",
                Logger::LogLevel::WARNING);
            continue;
        }

        // Move to upload queue for the render thread
        {
            std::lock_guard<std::mutex> lock(m_uploadQueueMutex);
            m_uploadQueue.push({ job.cpuTexture, job.targetGpuTexture, std::move(job.onReady) });
        }
    }
}
