#ifndef __BAR_UI_RENDERER_H__
#define __BAR_UI_RENDERER_H__

// RmlUi -> RT64 render bridge. Ported from Zelda64Recompiled (src/ui/ui_renderer.h, MIT) and
// retargeted to this repo's RT64 pin, whose RHI lives in the extracted `plume` library
// (RT64::Render* -> plume::Render*). Builds plume command lists for the active RmlUi context and
// composites the UI over the game's frame inside an RT64 render hook (see bar_ui.cpp).

#include <memory>
#include <string>
#include <vector>

namespace plume {
    struct RenderInterface;
    struct RenderDevice;
    struct RenderCommandList;
    struct RenderFramebuffer;
};

namespace Rml {
    class RenderInterface;
}

namespace recompui {
    class RmlRenderInterface_RT64_impl;

    class RmlRenderInterface_RT64 {
    private:
        std::unique_ptr<RmlRenderInterface_RT64_impl> impl;
    public:
        RmlRenderInterface_RT64();
        ~RmlRenderInterface_RT64();
        void reset();
        void init(plume::RenderInterface* interface, plume::RenderDevice* device);
        Rml::RenderInterface* get_rml_interface();

        void start(plume::RenderCommandList* list, int image_width, int image_height);
        void end(plume::RenderCommandList* list, plume::RenderFramebuffer* framebuffer);
        void queue_image_from_bytes_file(const std::string &src, const std::vector<char> &bytes);
        void queue_image_from_bytes_rgba32(const std::string &src, const std::vector<char> &bytes, uint32_t width, uint32_t height);
    };
} // namespace recompui

#endif
