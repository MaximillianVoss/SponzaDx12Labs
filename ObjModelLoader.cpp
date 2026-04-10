#include "ObjModelLoader.h"

#include <algorithm>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#define TINYOBJLOADER_IMPLEMENTATION
#include "ThirdParty/tiny_obj_loader.h"

#include <numeric>
#include <unordered_map>

using namespace DirectX;

namespace
{
    struct VertexKey
    {
        int PositionIndex = -1;
        int NormalIndex = -1;
        int TexcoordIndex = -1;

        bool operator==(const VertexKey& rhs) const
        {
            return PositionIndex == rhs.PositionIndex &&
                NormalIndex == rhs.NormalIndex &&
                TexcoordIndex == rhs.TexcoordIndex;
        }
    };

    struct VertexKeyHasher
    {
        size_t operator()(const VertexKey& key) const noexcept
        {
            size_t hash = std::hash<int>{}(key.PositionIndex);
            hash ^= std::hash<int>{}(key.NormalIndex) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<int>{}(key.TexcoordIndex) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            return hash;
        }
    };

    struct SubmeshBuilder
    {
        std::string Name;
        std::string MaterialName;
        std::vector<Vertex> Vertices;
        std::vector<std::uint32_t> Indices;
        std::vector<XMFLOAT3> TangentAccumulation;
        std::unordered_map<VertexKey, std::uint32_t, VertexKeyHasher> VertexLookup;
        std::vector<XMFLOAT3> Positions;
    };

    XMFLOAT3 LoadPosition(const tinyobj::attrib_t& attrib, int index)
    {
        if(index < 0)
        {
            return XMFLOAT3(0.0f, 0.0f, 0.0f);
        }

        const size_t base = static_cast<size_t>(index) * 3;
        return XMFLOAT3(
            attrib.vertices[base + 0],
            attrib.vertices[base + 1],
            attrib.vertices[base + 2]);
    }

    XMFLOAT3 LoadNormal(const tinyobj::attrib_t& attrib, int index)
    {
        if(index < 0)
        {
            return XMFLOAT3(0.0f, 1.0f, 0.0f);
        }

        const size_t base = static_cast<size_t>(index) * 3;
        return XMFLOAT3(
            attrib.normals[base + 0],
            attrib.normals[base + 1],
            attrib.normals[base + 2]);
    }

    XMFLOAT2 LoadTexcoord(const tinyobj::attrib_t& attrib, int index)
    {
        if(index < 0)
        {
            return XMFLOAT2(0.0f, 0.0f);
        }

        const size_t base = static_cast<size_t>(index) * 2;
        return XMFLOAT2(
            attrib.texcoords[base + 0],
            1.0f - attrib.texcoords[base + 1]);
    }

    std::uint32_t AddVertex(
        SubmeshBuilder& builder,
        const tinyobj::attrib_t& attrib,
        const tinyobj::index_t& index)
    {
        const VertexKey key{ index.vertex_index, index.normal_index, index.texcoord_index };
        const auto existing = builder.VertexLookup.find(key);
        if(existing != builder.VertexLookup.end())
        {
            return existing->second;
        }

        Vertex vertex = {};
        vertex.Pos = LoadPosition(attrib, index.vertex_index);
        vertex.Normal = LoadNormal(attrib, index.normal_index);
        vertex.TexC = LoadTexcoord(attrib, index.texcoord_index);
        vertex.TangentU = XMFLOAT3(1.0f, 0.0f, 0.0f);

        const std::uint32_t newIndex = static_cast<std::uint32_t>(builder.Vertices.size());
        builder.VertexLookup.emplace(key, newIndex);
        builder.Vertices.push_back(vertex);
        builder.TangentAccumulation.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
        builder.Positions.push_back(vertex.Pos);
        return newIndex;
    }

    void AccumulateTangent(SubmeshBuilder& builder, std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
    {
        const XMVECTOR p0 = XMLoadFloat3(&builder.Vertices[i0].Pos);
        const XMVECTOR p1 = XMLoadFloat3(&builder.Vertices[i1].Pos);
        const XMVECTOR p2 = XMLoadFloat3(&builder.Vertices[i2].Pos);

        const XMVECTOR uv0 = XMLoadFloat2(&builder.Vertices[i0].TexC);
        const XMVECTOR uv1 = XMLoadFloat2(&builder.Vertices[i1].TexC);
        const XMVECTOR uv2 = XMLoadFloat2(&builder.Vertices[i2].TexC);

        const XMFLOAT3 pf0 = builder.Vertices[i0].Pos;
        const XMFLOAT3 pf1 = builder.Vertices[i1].Pos;
        const XMFLOAT3 pf2 = builder.Vertices[i2].Pos;

        const XMFLOAT2 tf0 = builder.Vertices[i0].TexC;
        const XMFLOAT2 tf1 = builder.Vertices[i1].TexC;
        const XMFLOAT2 tf2 = builder.Vertices[i2].TexC;

        const XMFLOAT3 edge1(pf1.x - pf0.x, pf1.y - pf0.y, pf1.z - pf0.z);
        const XMFLOAT3 edge2(pf2.x - pf0.x, pf2.y - pf0.y, pf2.z - pf0.z);
        const XMFLOAT2 deltaUv1(tf1.x - tf0.x, tf1.y - tf0.y);
        const XMFLOAT2 deltaUv2(tf2.x - tf0.x, tf2.y - tf0.y);

        const float determinant = deltaUv1.x * deltaUv2.y - deltaUv1.y * deltaUv2.x;
        if(fabsf(determinant) < 1e-6f)
        {
            return;
        }

        const float inverseDeterminant = 1.0f / determinant;
        const XMFLOAT3 tangent(
            inverseDeterminant * (deltaUv2.y * edge1.x - deltaUv1.y * edge2.x),
            inverseDeterminant * (deltaUv2.y * edge1.y - deltaUv1.y * edge2.y),
            inverseDeterminant * (deltaUv2.y * edge1.z - deltaUv1.y * edge2.z));

        auto accumulate = [&](std::uint32_t vertexIndex)
        {
            builder.TangentAccumulation[vertexIndex].x += tangent.x;
            builder.TangentAccumulation[vertexIndex].y += tangent.y;
            builder.TangentAccumulation[vertexIndex].z += tangent.z;
        };

        accumulate(i0);
        accumulate(i1);
        accumulate(i2);
    }

    void FinalizeTangents(SubmeshBuilder& builder)
    {
        for(size_t i = 0; i < builder.Vertices.size(); ++i)
        {
            const XMVECTOR normal = XMLoadFloat3(&builder.Vertices[i].Normal);
            XMVECTOR tangent = XMLoadFloat3(&builder.TangentAccumulation[i]);

            tangent = XMVector3Normalize(tangent - XMVector3Dot(normal, tangent) * normal);
            if(XMVector3Less(XMVector3LengthSq(tangent), XMVectorReplicate(1e-6f)))
            {
                tangent = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            }

            XMStoreFloat3(&builder.Vertices[i].TangentU, tangent);
        }
    }

    std::wstring ResolveTexturePath(const std::filesystem::path& root, const std::string& relativeTexture)
    {
        if(relativeTexture.empty())
        {
            return {};
        }

        return (root / std::filesystem::path(relativeTexture)).lexically_normal().wstring();
    }

    ObjMaterialInfo MakeMaterialInfo(const std::filesystem::path& root, const tinyobj::material_t& material)
    {
        ObjMaterialInfo info = {};
        info.Name = material.name.empty() ? "material" : material.name;
        info.DiffuseAlbedo = XMFLOAT4(material.diffuse[0], material.diffuse[1], material.diffuse[2], material.dissolve);
        info.FresnelR0 = XMFLOAT3(
            (std::max)(material.specular[0], 0.02f),
            (std::max)(material.specular[1], 0.02f),
            (std::max)(material.specular[2], 0.02f));
        info.Roughness = std::clamp(1.0f - (material.shininess / 256.0f), 0.05f, 0.95f);
        info.DiffuseTexturePath = ResolveTexturePath(root, material.diffuse_texname);

        std::string normalTexture = material.normal_texname;
        if(normalTexture.empty())
        {
            normalTexture = material.bump_texname;
        }
        if(normalTexture.empty())
        {
            normalTexture = material.displacement_texname;
        }

        info.NormalTexturePath = ResolveTexturePath(root, normalTexture);
        info.AlphaTexturePath = ResolveTexturePath(root, material.alpha_texname);
        info.AlphaTested = !material.alpha_texname.empty() || material.dissolve < 0.999f;
        return info;
    }
}

ObjSceneData ObjModelLoader::LoadFromFile(const std::filesystem::path& objPath)
{
    tinyobj::ObjReaderConfig config = {};
    config.triangulate = true;
    config.vertex_color = false;
    config.mtl_search_path = objPath.parent_path().string();

    tinyobj::ObjReader reader;
    if(!reader.ParseFromFile(objPath.string(), config))
    {
        throw std::runtime_error(reader.Error().empty() ? "Failed to parse OBJ file." : reader.Error());
    }

    if(!reader.Warning().empty())
    {
        OutputDebugStringA(reader.Warning().c_str());
    }

    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    const std::vector<tinyobj::material_t>& inputMaterials = reader.GetMaterials();

    ObjSceneData scene = {};
    scene.Materials.reserve((std::max<size_t>)(1, inputMaterials.size()));

    if(inputMaterials.empty())
    {
        ObjMaterialInfo fallback = {};
        fallback.Name = "default";
        scene.Materials.push_back(fallback);
    }
    else
    {
        for(const tinyobj::material_t& material : inputMaterials)
        {
            scene.Materials.push_back(MakeMaterialInfo(objPath.parent_path(), material));
        }
    }

    std::unordered_map<int, size_t> builderLookup;
    std::vector<SubmeshBuilder> builders;

    auto acquireBuilder = [&](int materialId) -> SubmeshBuilder&
    {
        if(materialId < 0 || materialId >= static_cast<int>(scene.Materials.size()))
        {
            materialId = 0;
        }

        auto it = builderLookup.find(materialId);
        if(it != builderLookup.end())
        {
            return builders[it->second];
        }

        builderLookup.emplace(materialId, builders.size());
        builders.push_back({});
        builders.back().Name = scene.Materials[materialId].Name;
        builders.back().MaterialName = scene.Materials[materialId].Name;
        return builders.back();
    };

    for(const tinyobj::shape_t& shape : shapes)
    {
        size_t indexOffset = 0;
        for(size_t faceIndex = 0; faceIndex < shape.mesh.num_face_vertices.size(); ++faceIndex)
        {
            const int fv = shape.mesh.num_face_vertices[faceIndex];
            if(fv != 3)
            {
                indexOffset += static_cast<size_t>(fv);
                continue;
            }

            const int materialId = shape.mesh.material_ids.empty() ? 0 : shape.mesh.material_ids[faceIndex];
            SubmeshBuilder& builder = acquireBuilder(materialId);

            std::uint32_t triangleIndices[3] = {};
            for(int vertexIndex = 0; vertexIndex < 3; ++vertexIndex)
            {
                const tinyobj::index_t idx = shape.mesh.indices[indexOffset + static_cast<size_t>(vertexIndex)];
                triangleIndices[vertexIndex] = AddVertex(builder, attrib, idx);
                builder.Indices.push_back(triangleIndices[vertexIndex]);
            }

            AccumulateTangent(builder, triangleIndices[0], triangleIndices[1], triangleIndices[2]);
            indexOffset += 3;
        }
    }

    std::vector<XMFLOAT3> scenePositions;

    for(SubmeshBuilder& builder : builders)
    {
        FinalizeTangents(builder);

        ObjSubmeshInfo submesh = {};
        submesh.Name = builder.Name;
        submesh.MaterialName = builder.MaterialName;
        submesh.BaseVertexLocation = static_cast<INT>(scene.Vertices.size());
        submesh.StartIndexLocation = static_cast<UINT>(scene.Indices.size());
        submesh.IndexCount = static_cast<UINT>(builder.Indices.size());

        scene.Vertices.insert(scene.Vertices.end(), builder.Vertices.begin(), builder.Vertices.end());
        for(std::uint32_t localIndex : builder.Indices)
        {
            scene.Indices.push_back(localIndex + static_cast<std::uint32_t>(submesh.BaseVertexLocation));
        }

        if(!builder.Positions.empty())
        {
            BoundingBox::CreateFromPoints(
                submesh.Bounds,
                static_cast<UINT>(builder.Positions.size()),
                builder.Positions.data(),
                sizeof(XMFLOAT3));

            scenePositions.insert(scenePositions.end(), builder.Positions.begin(), builder.Positions.end());
        }

        scene.Submeshes.push_back(submesh);
    }

    if(!scenePositions.empty())
    {
        BoundingBox::CreateFromPoints(
            scene.Bounds,
            static_cast<UINT>(scenePositions.size()),
            scenePositions.data(),
            sizeof(XMFLOAT3));
    }

    return scene;
}
