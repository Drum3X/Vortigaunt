#pragma once


#include <assimp/types.h>

#include <cstdint>
#include <string>
#include <vector>

// from studio.h
constexpr int STUDIO_NF_CHROME = 0x0002;
constexpr int STUDIO_NF_ADDITIVE = 0x0020;
constexpr int STUDIO_NF_MASKED = 0x0040;

struct StudioBone
{
    std::string name;
    int parent = -1;
    float value[6] = {};  
    float scale[6] = {};  
};

struct StudioTexture
{
    std::string name;
    int flags = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> palette; 
};

struct StudioTriVertex
{
    int vertex = 0;  // StudioSubModel::vertices
    int normal = 0;  // StudioSubModel::normals
    float u = 0.0f;  // 0 to 1 across the texture
    float v = 0.0f;  // 0 to 1 down the texture
};

struct StudioMesh
{
    int texture = -1;                       
    std::vector<StudioTriVertex> triangles; 
};

struct StudioSubModel
{
    std::string name;
    std::vector<aiVector3D> vertices;  // each in the space of its bone
    std::vector<int> vertexBones;
    std::vector<aiVector3D> normals;
    std::vector<int> normalBones;
    std::vector<StudioMesh> meshes;
};

struct StudioBodyPart
{
    std::string name;
    std::vector<StudioSubModel> models;  // the first is the one shown by default
};

struct StudioAttachment
{
    int bone = 0;
    aiVector3D origin;  // in the bone's space
};

struct StudioSequence
{
    std::string name;
    float fps = 30.0f;
    int frames = 0;
};

class StudioModel
{
public:
    bool Load(const std::string& path);
    const std::string& GetError() const { return m_error; }

    std::vector<aiMatrix4x4> BoneTransforms(int sequence, int frame) const;

    std::string name;
    std::vector<StudioBone> bones;
    std::vector<StudioTexture> textures;
    std::vector<StudioBodyPart> bodyParts;
    std::vector<StudioAttachment> attachments;
    std::vector<StudioSequence> sequences;

private:
    struct SequenceData
    {
        int group = 0;       // which file the animation is in
        uint32_t offset = 0;  // of the per bone offset table within it
    };

    bool Fail(const std::string& message);
    bool ReadTextures(const std::vector<uint8_t>& file);

    std::string m_error;
    std::vector<SequenceData> m_sequenceData;
    std::vector<std::vector<uint8_t>> m_groupFiles;  
};
