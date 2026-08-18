#pragma once
#include <string>
#include <vector>

enum class SceneId { Shaderballs, HeroMcLaren };

struct SceneDesc {
    const char* name;
    std::vector<std::string> models;   // .obj/.gltf paths, loaded via Assimp path
    std::string hdriPath;              // equirect HDR fed to CreateIBLFromEquirect
    
};

inline const SceneDesc& GetSceneDesc(SceneId id) {
    static const SceneDesc kScenes[] = {
        /* Shaderballs */ { "Shaderballs",
            { "assets/shaderball.obj", "assets/floor.obj" },
            "assets/hdri/studio_small.hdr" },
        /* HeroMcLaren */ { "McLaren Hero",
            { "assets/mclaren/mclaren.gltf", "assets/turntable_disc.obj" },
            "assets/hdri/mclaren_studio.hdr" },
    };
    return kScenes[static_cast<int>(id)];
}