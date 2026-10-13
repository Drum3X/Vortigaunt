#include "core/mdl/StudioModel.h"

#include "utils/FileIO.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace {

// from studio.h.
constexpr size_t kBoneSize = 112;
constexpr size_t kSequenceSize = 176;
constexpr size_t kTextureSize = 80;
constexpr size_t kBodyPartSize = 76;
constexpr size_t kModelSize = 112;
constexpr size_t kMeshSize = 20;
constexpr size_t kAttachmentSize = 88;
constexpr size_t kAnimSize = 12;  

template <typename T>
bool readAt(const std::vector<uint8_t>& file, size_t offset, T& out)
{
    if (offset > file.size() || file.size() - offset < sizeof(T))
    {
        return false;
    }
    std::memcpy(&out, file.data() + offset, sizeof(T));
    return true;
}

bool inside(const std::vector<uint8_t>& file, size_t offset, size_t count, size_t elementSize)
{
    if (offset > file.size())
    {
        return false;
    }
    return count <= (file.size() - offset) / (elementSize ? elementSize : 1);
}

std::string readName(const std::vector<uint8_t>& file, size_t offset, size_t length)
{
    if (!inside(file, offset, length, 1))
    {
        return std::string();
    }
    const char* start = reinterpret_cast<const char*>(file.data() + offset);
    size_t n = 0;
    while (n < length && start[n] != '\0') n++;
    return std::string(start, n);
}

int32_t intAt(const std::vector<uint8_t>& file, size_t offset)
{
    int32_t value = 0;
    readAt(file, offset, value);
    return value;
}

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        return false;
    }
    const std::streamsize size = f.tellg();
    if (size <= 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good();
}

// name.mdl -> nameT.mdl, name01.mdl
std::filesystem::path sibling(const std::filesystem::path& model, const std::string& suffix)
{
    std::filesystem::path out = model;
    out.replace_filename(model.stem().native() + std::filesystem::path(suffix).native() + model.extension().native());
    return out;
}

}  // namespace

bool StudioModel::Fail(const std::string& message)
{
    m_error = message;
    return false;
}

bool StudioModel::Load(const std::string& path)
{
    *this = StudioModel();

    const std::filesystem::path modelPath = FileIO::toPath(path);
    std::vector<uint8_t> file;
    if (!readFile(modelPath, file))
    {
        return Fail("Could not read " + path);
    }
    if (file.size() < 244 || std::memcmp(file.data(), "IDST", 4) != 0)
    {
        return Fail("Not a GoldSrc studio model (no IDST header): " + path);
    }
    if (intAt(file, 4) != 10)
    {
        return Fail("Unsupported studio model version " + std::to_string(intAt(file, 4)) + " (GoldSrc is 10)");
    }

    name = readName(file, 8, 64);

    // ---- bones ----
    const int numBones = intAt(file, 140);
    const size_t boneIndex = static_cast<size_t>(intAt(file, 144));
    if (numBones < 0 || !inside(file, boneIndex, static_cast<size_t>(numBones), kBoneSize))
    {
        return Fail("The bone table lies outside the file");
    }
    bones.resize(static_cast<size_t>(numBones));
    for (int i = 0; i < numBones; i++)
    {
        const size_t at = boneIndex + static_cast<size_t>(i) * kBoneSize;
        StudioBone& bone = bones[i];
        bone.name = readName(file, at, 32);
        bone.parent = intAt(file, at + 32);
        if (bone.parent >= numBones || bone.parent == i)
        {
            bone.parent = -1;
        }
        std::memcpy(bone.value, file.data() + at + 64, sizeof(bone.value));
        std::memcpy(bone.scale, file.data() + at + 88, sizeof(bone.scale));
    }

    // A bone that is its own ancestor would send every walk up the skeleton
    // round for ever; such a bone is cut loose.
    for (int i = 0; i < numBones; i++)
    {
        int steps = 0;
        for (int p = bones[i].parent; p >= 0; p = bones[p].parent)
        {
            if (++steps > numBones)
            {
                bones[i].parent = -1;
                break;
            }
        }
    }

    // sequences
    const int numSequences = intAt(file, 164);
    const size_t sequenceIndex = static_cast<size_t>(intAt(file, 168));
    if (numSequences > 0 && inside(file, sequenceIndex, static_cast<size_t>(numSequences), kSequenceSize))
    {
        for (int i = 0; i < numSequences; i++)
        {
            const size_t at = sequenceIndex + static_cast<size_t>(i) * kSequenceSize;
            StudioSequence sequence;
            sequence.name = readName(file, at, 32);
            readAt(file, at + 32, sequence.fps);
            sequence.frames = intAt(file, at + 56);
            sequences.push_back(sequence);

            SequenceData data;
            data.offset = static_cast<uint32_t>(intAt(file, at + 124));
            data.group = intAt(file, at + 156);
            m_sequenceData.push_back(data);
        }
    }

    // Animations in other models: name01.mdl, name02.mdl ...
    int maxGroup = 0;
    for (const SequenceData& data : m_sequenceData) maxGroup = std::max(maxGroup, data.group);
    maxGroup = std::min(maxGroup, 99);
    m_groupFiles.resize(static_cast<size_t>(maxGroup) + 1);
    for (int g = 1; g <= maxGroup; g++)
    {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "%02d", g);
        readFile(sibling(modelPath, suffix), m_groupFiles[g]);  
    }

    //attachments
    const int numAttachments = intAt(file, 212);
    const size_t attachmentIndex = static_cast<size_t>(intAt(file, 216));
    if (numAttachments > 0 && inside(file, attachmentIndex, static_cast<size_t>(numAttachments), kAttachmentSize))
    {
        for (int i = 0; i < numAttachments; i++)
        {
            const size_t at = attachmentIndex + static_cast<size_t>(i) * kAttachmentSize;
            StudioAttachment attachment;
            attachment.bone = intAt(file, at + 36);
            float origin[3] = {};
            readAt(file, at + 40, origin);
            attachment.origin = aiVector3D(origin[0], origin[1], origin[2]);
            if (attachment.bone >= 0 && attachment.bone < numBones)
            {
                attachments.push_back(attachment);
            }
        }
    }

    // textures
    std::vector<uint8_t> textureFile;
    const std::vector<uint8_t>* textureSource = &file;
    if (intAt(file, 180) == 0)
    {
        if (readFile(sibling(modelPath, "T"), textureFile) || readFile(sibling(modelPath, "t"), textureFile))
        {
            if (textureFile.size() >= 244 && std::memcmp(textureFile.data(), "IDST", 4) == 0)
            {
                textureSource = &textureFile;
            }
        }
    }
    ReadTextures(*textureSource);


    std::vector<int16_t> skin;
    {
        const int numSkinRef = intAt(*textureSource, 192);
        const size_t skinIndex = static_cast<size_t>(intAt(*textureSource, 200));
        if (numSkinRef > 0 && inside(*textureSource, skinIndex, static_cast<size_t>(numSkinRef), 2))
        {
            skin.resize(static_cast<size_t>(numSkinRef));
            std::memcpy(skin.data(), textureSource->data() + skinIndex, skin.size() * 2);
        }
    }

    // body parts
    const int numBodyParts = intAt(file, 204);
    const size_t bodyPartIndex = static_cast<size_t>(intAt(file, 208));
    if (numBodyParts < 0 || !inside(file, bodyPartIndex, static_cast<size_t>(numBodyParts), kBodyPartSize))
    {
        return Fail("The body part table lies outside the file");
    }

    for (int b = 0; b < numBodyParts; b++)
    {
        const size_t partAt = bodyPartIndex + static_cast<size_t>(b) * kBodyPartSize;
        StudioBodyPart part;
        part.name = readName(file, partAt, 64);
        const int numModels = intAt(file, partAt + 64);
        const size_t modelIndex = static_cast<size_t>(intAt(file, partAt + 72));
        if (numModels < 0 || !inside(file, modelIndex, static_cast<size_t>(numModels), kModelSize))
        {
            return Fail("A model table lies outside the file");
        }

        for (int m = 0; m < numModels; m++)
        {
            const size_t modelAt = modelIndex + static_cast<size_t>(m) * kModelSize;
            StudioSubModel model;
            model.name = readName(file, modelAt, 64);

            const int numMeshes = intAt(file, modelAt + 72);
            const size_t meshIndex = static_cast<size_t>(intAt(file, modelAt + 76));
            const int numVertices = intAt(file, modelAt + 80);
            const size_t vertexInfoIndex = static_cast<size_t>(intAt(file, modelAt + 84));
            const size_t vertexIndex = static_cast<size_t>(intAt(file, modelAt + 88));
            const int numNormals = intAt(file, modelAt + 92);
            const size_t normalInfoIndex = static_cast<size_t>(intAt(file, modelAt + 96));
            const size_t normalIndex = static_cast<size_t>(intAt(file, modelAt + 100));

            if (numVertices < 0 || numNormals < 0 || numMeshes < 0 ||
                !inside(file, vertexIndex, static_cast<size_t>(numVertices), 12) ||
                !inside(file, vertexInfoIndex, static_cast<size_t>(numVertices), 1) ||
                !inside(file, normalIndex, static_cast<size_t>(numNormals), 12) ||
                !inside(file, normalInfoIndex, static_cast<size_t>(numNormals), 1) ||
                !inside(file, meshIndex, static_cast<size_t>(numMeshes), kMeshSize))
            {
                return Fail("Model \"" + model.name + "\" points outside the file");
            }

            model.vertices.resize(static_cast<size_t>(numVertices));
            model.vertexBones.resize(static_cast<size_t>(numVertices));
            for (int v = 0; v < numVertices; v++)
            {
                float p[3];
                std::memcpy(p, file.data() + vertexIndex + static_cast<size_t>(v) * 12, 12);
                model.vertices[v] = aiVector3D(p[0], p[1], p[2]);
                const int bone = file[vertexInfoIndex + static_cast<size_t>(v)];
                model.vertexBones[v] = bone < numBones ? bone : 0;
            }
            model.normals.resize(static_cast<size_t>(numNormals));
            model.normalBones.resize(static_cast<size_t>(numNormals));
            for (int n = 0; n < numNormals; n++)
            {
                float p[3];
                std::memcpy(p, file.data() + normalIndex + static_cast<size_t>(n) * 12, 12);
                model.normals[n] = aiVector3D(p[0], p[1], p[2]);
                const int bone = file[normalInfoIndex + static_cast<size_t>(n)];
                model.normalBones[n] = bone < numBones ? bone : 0;
            }

            for (int k = 0; k < numMeshes; k++)
            {
                const size_t meshAt = meshIndex + static_cast<size_t>(k) * kMeshSize;
                StudioMesh mesh;
                const int skinRef = intAt(file, meshAt + 8);
                int textureIndex = skinRef;
                if (skinRef >= 0 && skinRef < static_cast<int>(skin.size()))
                {
                    textureIndex = skin[skinRef];
                }
                mesh.texture = (textureIndex >= 0 && textureIndex < static_cast<int>(textures.size())) ? textureIndex : -1;

                const float width = mesh.texture >= 0 ? static_cast<float>(std::max(1, textures[mesh.texture].width)) : 1.0f;
                const float height = mesh.texture >= 0 ? static_cast<float>(std::max(1, textures[mesh.texture].height)) : 1.0f;


                size_t at = static_cast<size_t>(intAt(file, meshAt + 4));
                std::vector<StudioTriVertex> run;
                for (;;)
                {
                    int16_t count = 0;
                    if (!readAt(file, at, count) || count == 0)
                    {
                        break;
                    }
                    at += 2;
                    const bool fan = count < 0;
                    const size_t n = static_cast<size_t>(fan ? -count : count);
                    if (!inside(file, at, n, 8))
                    {
                        break;
                    }
                    run.resize(n);
                    for (size_t i = 0; i < n; i++)
                    {
                        int16_t raw[4];
                        std::memcpy(raw, file.data() + at + i * 8, 8);
                        run[i].vertex = raw[0];
                        run[i].normal = raw[1];
                        run[i].u = static_cast<float>(raw[2]) / width;
                        run[i].v = static_cast<float>(raw[3]) / height;
                    }
                    at += n * 8;

                    for (size_t i = 2; i < n; i++)
                    {
                        StudioTriVertex tri[3];
                        if (fan)
                        {
                            tri[0] = run[0]; tri[1] = run[i - 1]; tri[2] = run[i];
                        }
                        else if (i % 2 == 0)
                        {
                            tri[0] = run[i - 2]; tri[1] = run[i - 1]; tri[2] = run[i];
                        }
                        else
                        {
                            tri[0] = run[i - 1]; tri[1] = run[i - 2]; tri[2] = run[i];
                        }
                        bool valid = true;
                        for (const StudioTriVertex& tv : tri)
                        {
                            if (tv.vertex < 0 || tv.vertex >= numVertices || tv.normal < 0 || tv.normal >= numNormals)
                            {
                                valid = false;
                            }
                        }
                        if (valid)
                        {
                            mesh.triangles.insert(mesh.triangles.end(), tri, tri + 3);
                        }
                    }
                }
                model.meshes.push_back(std::move(mesh));
            }
            part.models.push_back(std::move(model));
        }
        bodyParts.push_back(std::move(part));
    }

    m_groupFiles[0] = std::move(file);
    return true;
}

bool StudioModel::ReadTextures(const std::vector<uint8_t>& file)
{
    const int numTextures = intAt(file, 180);
    const size_t textureIndex = static_cast<size_t>(intAt(file, 184));
    if (numTextures <= 0 || !inside(file, textureIndex, static_cast<size_t>(numTextures), kTextureSize))
    {
        return false;
    }
    for (int i = 0; i < numTextures; i++)
    {
        const size_t at = textureIndex + static_cast<size_t>(i) * kTextureSize;
        StudioTexture texture;
        texture.name = readName(file, at, 64);
        texture.flags = intAt(file, at + 64);
        texture.width = intAt(file, at + 68);
        texture.height = intAt(file, at + 72);
        const size_t dataIndex = static_cast<size_t>(intAt(file, at + 76));

        // Pixels first, the palette straight after them.
        if (texture.width > 0 && texture.height > 0 && texture.width <= 4096 && texture.height <= 4096)
        {
            const size_t pixelCount = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
            if (inside(file, dataIndex, pixelCount + 768, 1))
            {
                texture.pixels.assign(file.begin() + dataIndex, file.begin() + dataIndex + pixelCount);
                texture.palette.assign(file.begin() + dataIndex + pixelCount, file.begin() + dataIndex + pixelCount + 768);
            }
        }
        textures.push_back(std::move(texture));
    }
    return true;
}

std::vector<aiMatrix4x4> StudioModel::BoneTransforms(int sequence, int frame) const
{
    const std::vector<uint8_t>* file = nullptr;
    size_t animBase = 0;
    if (sequence >= 0 && sequence < static_cast<int>(m_sequenceData.size()))
    {
        const SequenceData& data = m_sequenceData[sequence];
        if (data.group >= 0 && data.group < static_cast<int>(m_groupFiles.size()) && !m_groupFiles[data.group].empty())
        {
            file = &m_groupFiles[data.group];
            animBase = data.offset;
            if (!inside(*file, animBase, bones.size(), kAnimSize))
            {
                file = nullptr;
            }
        }
        frame = std::clamp(frame, 0, std::max(0, sequences[sequence].frames - 1));
    }

    std::vector<aiMatrix4x4> local(bones.size());

    for (size_t i = 0; i < bones.size(); i++)
    {
        const StudioBone& bone = bones[i];
        float value[6];
        std::memcpy(value, bone.value, sizeof(value));

        if (file)
        {
            const size_t animAt = animBase + i * kAnimSize;
            uint16_t offsets[6] = {};
            readAt(*file, animAt, offsets);
            for (int j = 0; j < 6; j++)
            {
                if (offsets[j] == 0)
                {
                    continue;
                }

                size_t at = animAt + offsets[j];
                int k = frame;
                bool ok = true;
                for (;;)
                {
                    uint8_t header[2];
                    if (!readAt(*file, at, header))
                    {
                        ok = false;
                        break;
                    }
                    const int valid = header[0], total = header[1];
                    if (total == 0)
                    {
                        ok = false;
                        break;
                    }
                    if (total > k)
                    {
                        int16_t sample = 0;
                        const size_t index = static_cast<size_t>(valid > k ? k + 1 : valid);
                        if (readAt(*file, at + index * 2, sample))
                        {
                            value[j] = bone.value[j] + static_cast<float>(sample) * bone.scale[j];
                        }
                        break;
                    }
                    k -= total;
                    at += static_cast<size_t>(valid + 1) * 2;
                }
                (void)ok;
            }
        }

        local[i].FromEulerAnglesXYZ(value[3], value[4], value[5]);
        local[i].a4 = value[0];
        local[i].b4 = value[1];
        local[i].c4 = value[2];
    }


    std::vector<aiMatrix4x4> world(bones.size());
    std::vector<bool> done(bones.size(), false);
    std::vector<int> chain;
    for (size_t i = 0; i < bones.size(); i++)
    {
        chain.clear();
        for (int b = static_cast<int>(i); b >= 0 && !done[b]; b = bones[b].parent)
        {
            chain.push_back(b);
        }
        for (size_t k = chain.size(); k-- > 0;)
        {
            const int b = chain[k];
            const int parent = bones[b].parent;
            world[b] = parent >= 0 ? world[parent] * local[b] : local[b];
            done[b] = true;
        }
    }
    return world;
}
