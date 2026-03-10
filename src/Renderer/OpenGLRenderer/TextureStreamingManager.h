#pragma once

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <cstdint>

class Texture;
class OpenGLTexture;

/// Manages asynchronous texture loading from disk and batched GPU upload.
/// Textures requested via `streamTexture()` return a shared placeholder
/// immediately; the real data is loaded on a background thread and uploaded
/// to the GPU in small per-frame batches via `processUploads()`.
class TextureStreamingManager
{
public:
    TextureStreamingManager();
    ~TextureStreamingManager();

    TextureStreamingManager(const TextureStreamingManager&) = delete;
    TextureStreamingManager& operator=(const TextureStreamingManager&) = delete;

    /// Call once after GL context is current. Creates the 1×1 placeholder
    /// texture and starts the background loader thread.
    bool initialize();

    /// Shut down the background thread and release GPU resources.
    void shutdown();

    /// Request a texture to be streamed. Returns a shared_ptr to a GPU
    /// texture that initially points to the placeholder. Once the real
    /// texture data has been loaded from disk and uploaded, the shared_ptr's
    /// internal OpenGLTexture is swapped to the full-res version.
    ///
    /// @param cpuTexture  The CPU-side Texture with pixel/DDS data.
    ///                    May be null (returns nullptr).
    /// @param onReady     Optional callback invoked on the main thread
    ///                    once the GPU texture is ready.
    /// @return            Shared OpenGLTexture (placeholder until upload).
    std::shared_ptr<OpenGLTexture> streamTexture(
        const std::shared_ptr<Texture>& cpuTexture,
        std::function<void()> onReady = {});

    /// Upload up to `maxUploadsPerFrame` queued textures to the GPU.
    /// Call once per frame from the render thread (GL context must be current).
    void processUploads(int maxUploadsPerFrame = 4);

    /// Returns the 1×1 magenta placeholder texture.
    std::shared_ptr<OpenGLTexture> getPlaceholder() const { return m_placeholder; }

    /// Number of textures still waiting for disk load + GPU upload.
    size_t getPendingCount() const;

    /// Returns true if the manager has been initialized.
    bool isInitialized() const { return m_initialized.load(std::memory_order_relaxed); }

private:
    /// A pending texture request: CPU data loaded from disk, waiting for GPU upload.
    struct UploadRequest
    {
        std::shared_ptr<Texture> cpuTexture;            // pixel data
        std::shared_ptr<OpenGLTexture> targetGpuTexture; // will be re-initialized with real data
        std::function<void()> onReady;
    };

    /// A disk-load job: texture data that needs to be read from disk on the background thread.
    struct LoadJob
    {
        std::shared_ptr<Texture> cpuTexture;            // contains path/metadata; data will be populated
        std::shared_ptr<OpenGLTexture> targetGpuTexture;
        std::function<void()> onReady;
    };

    void loaderThreadFunc();

    std::shared_ptr<OpenGLTexture> m_placeholder;
    std::atomic<bool> m_initialized{ false };

    // Background loader thread
    std::thread m_loaderThread;
    std::atomic<bool> m_shutdownRequested{ false };

    // Load queue (main → loader thread)
    std::mutex m_loadQueueMutex;
    std::queue<LoadJob> m_loadQueue;

    // Upload queue (loader thread → main/render thread)
    std::mutex m_uploadQueueMutex;
    std::queue<UploadRequest> m_uploadQueue;

    // De-duplication: maps CPU Texture pointer → existing GPU texture
    std::mutex m_cacheMutex;
    std::unordered_map<const Texture*, std::weak_ptr<OpenGLTexture>> m_streamCache;
};
