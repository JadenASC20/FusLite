class SceneManager {
public:
    SceneId current = SceneId::Shaderballs;

    // Called once at startup, and again on every switch.
    void Load(SceneId id) {
        // Full GPU drain. Scene resources are referenced by in-flight command
        // buffers; destroying them mid-flight = use-after-free / DEVICE_LOST.
        // A switch is a rare, non-perf-critical event, so a hard idle is correct here.
        vkDeviceWaitIdle(ctx.device);

        // Tear down the OLD scene's GPU resources (skip on first load, vectors empty)
        DestroySceneResources();   // vertex/index buffers, per-mesh material textures
        DestroyIBL();              // irradiance + prefiltered + BRDF LUT images/views

        // Load the new scene
        const SceneDesc& d = GetSceneDesc(id);
        for (const auto& path : d.models)
            meshes.push_back(LoadModel(path));

        // Rebake IBL from the new HDRI
        ibl = ctx.CreateIBLFromEquirect(d.hdriPath); // irradiance 32, prefilt 128×5, LUT 128

        current = id;
    }

    void DestroySceneResources() {
        for (auto& m : meshes) DestroyMesh(m);   // vmaDestroyBuffer vtx+idx, destroy textures
        meshes.clear();
    }

    void DestroyIBL() {
        
        if (!ibl.valid) return;
        // destroy views then images then free allocations, for all three IBL products.
        DestroyIBLResources(ibl);   // <-- factor out of CreateIBLFromEquirect's cleanup path
        ibl = {};
    }

    std::vector<Mesh> meshes;
    IBLResources      ibl;
private:
    VulkanContext& ctx;
};