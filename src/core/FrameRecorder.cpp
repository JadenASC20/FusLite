#include "FrameRecorder.h"

#include "VulkanHelpers.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "GraphicsPipeline.h"
#include "TonemapPipeline.h"
#include "ResolvePipeline.h"
#include "DebugViewPipeline.h"
#include "SSRPipeline.h"
#include "SSAOPipeline.h"
#include "HiZPipeline.h"
#include "ShadowPipeline.h"
#include "ShadowMap.h"
#include "Skybox.h"
#include "Camera.h"
#include "Model.h"
#include "ImGuiManager.h"
#include "GpuLabel.h"

#include <cstdio>

namespace {

    // Camera near/far. These are duplicated into several push-constant blocks; keep
    // them here so there's one place to change them.
    constexpr float kNearZ = 0.1f;
    constexpr float kFarZ = 1000.0f;

    // 
    // Per-frame image handles, resolved once instead of at every use site.
    // 
    struct FrameTargets
    {
        VkExtent2D extent{};

        VkImage colorImage{};        VkImageView colorView{};        // swapchain
        VkImage depthImage{};        VkImageView depthView{};
        VkImage hdrImage{};          VkImageView hdrView{};
        VkImage motionImage{};       VkImageView motionView{};
        VkImage normalImage{};       VkImageView normalView{};
        VkImage materialImage{};     VkImageView materialView{};
        VkImage ssrImage{};          VkImageView ssrView{};
        VkImage ssaoImage{};         VkImageView ssaoView{};
        VkImage compositeImage{};    VkImageView compositeView{};
        VkImage historyWriteImage{}; VkImageView historyWriteView{};
    };

    FrameTargets GatherTargets(const PassResources& res, const FrameParams& frame)
    {
        const Swapchain& sc = *res.swapchain;
        const RenderPass& rt = *res.targets;
        const uint32_t    i = frame.imageIndex;

        FrameTargets t;
        t.extent = sc.GetExtent();

        t.colorImage = sc.GetImages()[i];              t.colorView = sc.GetImageViews()[i];
        t.depthImage = rt.GetDepthImages()[i];         t.depthView = rt.GetDepthImageViews()[i];
        t.hdrImage = rt.GetHdrImages()[i];           t.hdrView = rt.GetHdrImageViews()[i];
        t.motionImage = rt.GetMotionImages()[i];        t.motionView = rt.GetMotionImageViews()[i];
        t.normalImage = rt.GetNormalImages()[i];        t.normalView = rt.GetNormalImageViews()[i];
        t.materialImage = rt.GetMaterialImages()[i];      t.materialView = rt.GetMaterialImageViews()[i];
        t.ssrImage = rt.GetSSRImages()[i];           t.ssrView = rt.GetSSRImageViews()[i];
        t.ssaoImage = rt.GetSSAOImages()[i];          t.ssaoView = rt.GetSSAOImageViews()[i];
        t.compositeImage = rt.GetCompositeImages()[i];     t.compositeView = rt.GetCompositeImageViews()[i];

        t.historyWriteImage = rt.GetHistoryImages()[frame.historyWrite];
        t.historyWriteView = rt.GetHistoryImageViews()[frame.historyWrite];

        return t;
    }

    // Dynamic-rendering attachment boilerplate
    // 

    VkRenderingAttachmentInfo ColorAttachment(VkImageView view, VkAttachmentLoadOp loadOp,
        VkClearColorValue clear = { { 0.0f, 0.0f, 0.0f, 0.0f } })
    {
        VkRenderingAttachmentInfo a{};
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = view;
        a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.loadOp = loadOp;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.clearValue.color = clear;
        return a;
    }

    VkRenderingAttachmentInfo DepthAttachment(VkImageView view, VkAttachmentLoadOp loadOp)
    {
        VkRenderingAttachmentInfo a{};
        a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        a.imageView = view;
        a.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        a.loadOp = loadOp;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.clearValue.depthStencil = { 1.0f, 0 };
        return a;
    }

    VkRenderingInfo MakeRenderingInfo(VkExtent2D extent,
        const VkRenderingAttachmentInfo* colors, uint32_t colorCount,
        const VkRenderingAttachmentInfo* depth = nullptr)
    {
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = { { 0, 0 }, extent };
        info.layerCount = 1;
        info.colorAttachmentCount = colorCount;
        info.pColorAttachments = colors;
        info.pDepthAttachment = depth;
        return info;
    }

    void SetFullViewport(VkCommandBuffer cmd, VkExtent2D extent)
    {
        VkViewport vp{ 0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f };
        VkRect2D   sc{ { 0, 0 }, extent };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    void DrawFullscreenTriangle(VkCommandBuffer cmd)
    {
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    // Shorthands for the two transitions used over and over.
    void ToColorAttachment(VkCommandBuffer cmd, VkImage image)
    {
        TransitionImage(cmd, image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }

    void ColorToShaderRead(VkCommandBuffer cmd, VkImage image)
    {
        TransitionImage(cmd, image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // 
    // Pass 0: shadow map, depth-only from the light's point of view
    // 
    void RecordShadowPass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const std::vector<Model>& models, const Scene& scene)
    {
        GpuLabel _lbl(cmd, "Shadow Pass", 0.5f, 0.4f, 0.2f);

        const ShadowMap& sm = *res.shadowMap;

        TransitionImage(cmd, sm.GetImage(),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo depth = DepthAttachment(sm.GetImageView(), VK_ATTACHMENT_LOAD_OP_CLEAR);
        VkExtent2D shadowExtent{ sm.GetResolution(), sm.GetResolution() };
        VkRenderingInfo info = MakeRenderingInfo(shadowExtent, nullptr, 0, &depth);

        vkCmdBeginRendering(cmd, &info);
        res.shadowPipeline->Bind(cmd);

        for (size_t i = 0; i < models.size(); i++) {
            ShadowPushConstants pc{};
            pc.lightViewProj = frame.lightViewProj;
            pc.model = scene.objects[i].transform;
            res.shadowPipeline->PushConstants(cmd, pc);
            models[i].DrawGeometryOnly(cmd);
        }
        vkCmdEndRendering(cmd);

        TransitionImage(cmd, sm.GetImage(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Pass 1: scene G-buffer -- HDR + motion + normal + material, plus skybox
    // 
    void RecordScenePass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const std::vector<Model>& models, const Scene& scene)
    {
        GpuLabel _lbl(cmd, "Scene G-Buffer", 0.3f, 0.7f, 0.3f);

        ToColorAttachment(cmd, t.hdrImage);
        ToColorAttachment(cmd, t.motionImage);
        ToColorAttachment(cmd, t.normalImage);
        ToColorAttachment(cmd, t.materialImage);
        TransitionImage(cmd, t.depthImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo colors[4] = {
            ColorAttachment(t.hdrView,      VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.1f, 0.1f, 0.2f, 1.0f } }),
            ColorAttachment(t.motionView,   VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.0f, 0.0f, 0.0f, 0.0f } }),
            ColorAttachment(t.normalView,   VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.5f, 0.5f, 1.0f, 1.0f } }), // N = (0,0,1)
            ColorAttachment(t.materialView, VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.5f, 0.0f, 0.0f, 0.0f } }), // rough 0.5, metal 0
        };
        VkRenderingAttachmentInfo depth = DepthAttachment(t.depthView, VK_ATTACHMENT_LOAD_OP_CLEAR);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, colors, 4, &depth);

        vkCmdBeginRendering(cmd, &info);

        res.skybox->Draw(cmd, frame.imageIndex);

        const GraphicsPipeline& pipe = *res.scenePipeline;

        pipe.Bind(cmd);
        pipe.PushParams(cmd, frame.renderParams);
        for (size_t i = 0; i < models.size(); i++) {
            models[i].DrawOpaque(cmd, pipe.GetLayout(), frame.imageIndex,
                frame.renderParams, scene.objects[i].materials);
        }

        pipe.BindTransparent(cmd);
        pipe.PushParams(cmd, frame.renderParams);
        for (size_t i = 0; i < models.size(); i++) {
            models[i].DrawTransparent(cmd, pipe.GetLayout(), frame.imageIndex,
                frame.renderParams, scene.objects[i].materials);
        }
        
        vkCmdEndRendering(cmd);
        
        
    }

    // 
    // G-buffer -> shader read, then build the Hi-Z pyramid.
    // SSAO and SSR both depend on this having run.
    // 
    void RecordGBufferReadbackAndHiZ(VkCommandBuffer cmd, const PassResources& res,
        const FrameParams& frame, const FrameTargets& t)
    {
        ColorToShaderRead(cmd, t.hdrImage);
        ColorToShaderRead(cmd, t.normalImage);
        ColorToShaderRead(cmd, t.materialImage);

        TransitionImage(cmd, t.depthImage,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        res.hiz->Build(cmd, frame.imageIndex,
            res.targets->GetHiZImage(),
            res.targets->GetHiZBaseExtent(),
            res.targets->GetHiZMipLevels(),
            kNearZ, kFarZ);
    }

    // 
    // SSAO -- reads depth + normal, writes the AO target
    // 
    void RecordSSAOPass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const Camera& camera, const SSAOSettings& settings)
    {
        GpuLabel _lbl(cmd, "SSAO", 0.8f, 0.6f, 0.2f);

        ToColorAttachment(cmd, t.ssaoImage);

        VkRenderingAttachmentInfo ao = ColorAttachment(t.ssaoView, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &ao, 1);

        vkCmdBeginRendering(cmd, &info);
        SetFullViewport(cmd, t.extent);

        SSAOPipeline::SSAOPush push{};
        push.proj = camera.GetProjectionMatrixNoJitter();
        push.invProj = glm::inverse(push.proj);
        push.view = camera.GetViewMatrix();
        push.screenSize = glm::vec2(float(t.extent.width), float(t.extent.height));
        push.radius = settings.radius;
        push.bias = settings.bias;
        push.power = settings.power;

        res.ssao->Bind(cmd, frame.imageIndex, push);
        DrawFullscreenTriangle(cmd);
        vkCmdEndRendering(cmd);

        ColorToShaderRead(cmd, t.ssaoImage);
    }

    // 
    // SSR -- Hi-Z traversal against the HDR colour buffer
    // 
    void RecordSSRPass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const Camera& camera, const SSRSettings& settings)
    {
        GpuLabel _lbl(cmd, "SSR", 0.7f, 0.3f, 0.7f);

        ToColorAttachment(cmd, t.ssrImage);

        // When SSR is off we still need the target in a defined state for the
        // composite pass, so clear it instead of skipping the render entirely.
        VkRenderingAttachmentInfo ssrAtt = ColorAttachment(t.ssrView,
            settings.enabled ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &ssrAtt, 1);

        vkCmdBeginRendering(cmd, &info);
        if (settings.enabled) {
            SetFullViewport(cmd, t.extent);

            const glm::mat4 proj = camera.GetProjectionMatrixNoJitter();

            SSRPipeline::SSRPush push{};
            push.invProj = glm::inverse(proj);
            push.view = camera.GetViewMatrix();
            push.proj = proj;
            push.screenSize = glm::vec2(float(t.extent.width), float(t.extent.height));
            push.nearZ = kNearZ;
            push.farZ = kFarZ;
            push.maxSteps = settings.maxSteps;
            push.stepSize = settings.stepSize;
            push.thickness = settings.thickness;
            push.hizMipCount = int(res.targets->GetHiZMipLevels());

            res.ssr->BindSSR(cmd, frame.imageIndex, push);
            DrawFullscreenTriangle(cmd);
        }
        vkCmdEndRendering(cmd);

        ColorToShaderRead(cmd, t.ssrImage);
    }

    // Composite -- HDR + SSR + AO into the composite target that TAA resolves from.
    // The depth-aware AO blur is folded into composite.frag rather than being a
    // separate pass.
   
    void RecordCompositePass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const Camera& camera, const RenderSettings& settings)
    {
        GpuLabel _lbl(cmd, "Composite", 0.3f, 0.5f, 0.9f);

        ToColorAttachment(cmd, t.compositeImage);

        VkRenderingAttachmentInfo comp = ColorAttachment(t.compositeView, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &comp, 1);

        vkCmdBeginRendering(cmd, &info);
        SetFullViewport(cmd, t.extent);

        SSRPipeline::CompPush push{};
        push.invProj = glm::inverse(camera.GetProjectionMatrixNoJitter());
        push.invView = glm::inverse(camera.GetViewMatrix());
        push.cameraPos = glm::vec4(camera.GetPosition(), 0.0f);
        push.reflectivity = settings.ssr.reflectivity;
        push.aoStrength = settings.ssao.enabled ? 1.0f : 0.0f;

        res.ssr->BindComposite(cmd, frame.imageIndex, push);
        DrawFullscreenTriangle(cmd);
        vkCmdEndRendering(cmd);

        ColorToShaderRead(cmd, t.compositeImage);
    }

    // TAA resolve.
    //
    // Reads composite + motion + history[read], writes history[write], then copies
    // the result back into the HDR image so the tonemap pass keeps reading HDR.
    // Both history images are in SHADER_READ at frame start and are left that way
    // at frame end -- that invariant is what makes the ping-pong barrier-free.
    void RecordTAAResolve(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const TAASettings& taa)
    {
        GpuLabel _lbl(cmd, "TAA Resolve", 0.6f, 0.6f, 0.3f);

        ColorToShaderRead(cmd, t.motionImage);

        TransitionImage(cmd, t.historyWriteImage,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo att = ColorAttachment(t.historyWriteView, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &att, 1);

        vkCmdBeginRendering(cmd, &info);

        ResolvePipeline::PushConstants push{};
        push.texelSize[0] = 1.0f / float(t.extent.width);
        push.texelSize[1] = 1.0f / float(t.extent.height);
        push.blendAlpha = taa.resolveEnabled ? taa.blendAlpha : 1.0f;
        push.firstFrame = frame.firstFrame ? 1 : 0;

        res.resolve->Bind(cmd, frame.imageIndex, frame.historyRead, push);
        DrawFullscreenTriangle(cmd);
        vkCmdEndRendering(cmd);

        // Copy resolved history back into HDR.
        TransitionImage(cmd, t.historyWriteImage,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        TransitionImage(cmd, t.hdrImage,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        VkImageCopy copy{};
        copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        copy.extent = { t.extent.width, t.extent.height, 1 };
        vkCmdCopyImage(cmd,
            t.historyWriteImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            t.hdrImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy);

        // Restore the steady-state layouts: history becomes next frame's read
        // source, HDR goes back to being sampled by tonemap.
        TransitionImage(cmd, t.historyWriteImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);

        TransitionImage(cmd, t.hdrImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

   
    // Auto-exposure: blit the resolved HDR down to 1x1 and copy to a staging
    // buffer. The CPU reads it next frame, so this is one frame of latency by
    // design -- no stall.
    
    void RecordAutoExposureReadback(VkCommandBuffer cmd, const PassResources& res,
        const FrameParams& frame, const FrameTargets& t)
    {
        GpuLabel _lbl(cmd, "Auto-Exposure", 0.4f, 0.4f, 0.4f);

        TransitionImage(cmd, t.hdrImage,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkImage lumImage = res.targets->GetLumImage();
        TransitionImage(cmd, lumImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);

        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { int32_t(t.extent.width), int32_t(t.extent.height), 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { 1, 1, 1 };
        vkCmdBlitImage(cmd,
            t.hdrImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            lumImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        TransitionImage(cmd, lumImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);

        VkBufferImageCopy toBuf{};
        toBuf.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        toBuf.imageExtent = { 1, 1, 1 };
        vkCmdCopyImageToBuffer(cmd, lumImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            res.targets->GetLumStagingBuffer(frame.frameInFlight), 1, &toBuf);

        TransitionImage(cmd, t.hdrImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // Picks the image the debug viewer samples. Every candidate is already in
    // SHADER_READ_ONLY by this point in the frame, so no extra barrier is needed.
    VkImageView DebugSourceView(const FrameTargets& t, int debugMode)
    {
        switch (debugMode) {
        case 1:  return t.hdrView;
        case 2:  return t.motionView;
        case 3:  return t.normalView;
        case 4:  return t.depthView;
        case 5:  return t.ssrView;
        case 6:  return t.ssaoView;
        default: return VK_NULL_HANDLE;
        }
    }

    // 
    // Tonemap (or debug view) into the swapchain image
    void RecordTonemapPass(VkCommandBuffer cmd, const PassResources& res, const FrameParams& frame,
        const FrameTargets& t, const RenderSettings& settings)
    {
        GpuLabel _lbl(cmd, "Tonemap", 0.9f, 0.5f, 0.3f);

        ToColorAttachment(cmd, t.colorImage);

        VkRenderingAttachmentInfo att = ColorAttachment(t.colorView, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &att, 1);

        vkCmdBeginRendering(cmd, &info);

        if (settings.debugView == 0) {
            res.tonemap->Bind(cmd, frame.imageIndex,
                settings.exposure.tonemapMode, settings.exposure.exposure);
            DrawFullscreenTriangle(cmd);
        }
        else {
            SetFullViewport(cmd, t.extent);

            DebugViewPipeline::PushConstants push{};
            push.mode = settings.debugView;
            push.nearZ = kNearZ;
            push.farZ = kFarZ;
            res.debugView->Bind(cmd, DebugSourceView(t, settings.debugView), push);
        }

        vkCmdEndRendering(cmd);
    }

    // ImGui overlay, drawn straight onto the swapchain image
    void RecordUIPass(VkCommandBuffer cmd, const PassResources& res, const FrameTargets& t)
    {
        GpuLabel _lbl(cmd, "ImGui", 0.7f, 0.7f, 0.7f);

        VkRenderingAttachmentInfo att = ColorAttachment(t.colorView, VK_ATTACHMENT_LOAD_OP_LOAD);
        VkRenderingInfo info = MakeRenderingInfo(t.extent, &att, 1);

        vkCmdBeginRendering(cmd, &info);
        res.imgui->RecordDrawCommands(cmd);
        vkCmdEndRendering(cmd);
    }

}

void RecordFrame(VkCommandBuffer cmd,
    const PassResources& res,
    const FrameParams& frame,
    const std::vector<Model>& models,
    const Scene& scene,
    const Camera& camera,
    const RenderSettings& settings)
{
    const FrameTargets t = GatherTargets(res, frame);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    RecordShadowPass(cmd, res, frame, models, scene);
    RecordScenePass(cmd, res, frame, t, models, scene);

    RecordGBufferReadbackAndHiZ(cmd, res, frame, t);
    RecordSSAOPass(cmd, res, frame, t, camera, settings.ssao);
    RecordSSRPass(cmd, res, frame, t, camera, settings.ssr);
    RecordCompositePass(cmd, res, frame, t, camera, settings);

    RecordTAAResolve(cmd, res, frame, t, settings.taa);
    RecordAutoExposureReadback(cmd, res, frame, t);

    RecordTonemapPass(cmd, res, frame, t, settings);
    if (settings.showGui) {
        RecordUIPass(cmd, res, t);
    }

    TransitionImage(cmd, t.colorImage,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);

    VkResult endRes = vkEndCommandBuffer(cmd);
    if (endRes != VK_SUCCESS) {
        fprintf(stderr, ">>> vkEndCommandBuffer FAILED: %d\n", endRes);
    }
}