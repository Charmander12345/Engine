#include "../Renderer.h"

class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    void initialize() override;
    void clear() override;
    void present() override;
    const std::string& name() const override;

private:
    bool m_initialized;
    std::string m_name;
};