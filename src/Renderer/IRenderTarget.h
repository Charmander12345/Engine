#pragma once

class IRenderTarget
{
public:
    virtual ~IRenderTarget() = default;

    /// Create or resize the underlying framebuffer. Returns true on success.
    virtual bool resize(int width, int height) = 0;

    /// Bind this render target as the active framebuffer for drawing.
    virtual void bind() = 0;

    /// Unbind and restore the default framebuffer.
    virtual void unbind() = 0;

    /// Release all GPU resources.
    virtual void destroy() = 0;

    /// True when the render target has been successfully allocated.
    virtual bool isValid() const = 0;

    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;

    /// Backend-opaque handle for the colour attachment (e.g. GL texture name).
    virtual unsigned int getColorTextureId() const = 0;

    /// Copy the current colour content into an internal snapshot texture.
    virtual void takeSnapshot() = 0;
    virtual bool hasSnapshot() const = 0;
    virtual unsigned int getSnapshotTextureId() const = 0;
};
