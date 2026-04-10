#pragma once

#include "Common/d3dUtil.h"

#include <numeric>

class SceneOctree
{
public:
    struct Entry
    {
        DirectX::BoundingBox Bounds;
        size_t Payload = 0;
    };

    void Build(const DirectX::BoundingBox& sceneBounds, std::vector<Entry> entries, int maxDepth = 6, size_t maxItemsPerNode = 24)
    {
        mEntries = std::move(entries);
        mMaxDepth = maxDepth;
        mMaxItemsPerNode = maxItemsPerNode;
        mRoot = std::make_unique<Node>();
        mRoot->Bounds = sceneBounds;
        mRoot->Items.resize(mEntries.size());
        std::iota(mRoot->Items.begin(), mRoot->Items.end(), 0);
        Subdivide(*mRoot, 0);
    }

    void Query(const DirectX::BoundingFrustum& frustum, std::vector<size_t>& visiblePayloads) const
    {
        visiblePayloads.clear();
        if(!mRoot)
        {
            return;
        }

        QueryNode(*mRoot, frustum, visiblePayloads);
    }

private:
    struct Node
    {
        DirectX::BoundingBox Bounds;
        std::vector<size_t> Items;
        std::array<std::unique_ptr<Node>, 8> Children;
        bool IsLeaf() const
        {
            return std::all_of(Children.begin(), Children.end(), [](const std::unique_ptr<Node>& child) { return child == nullptr; });
        }
    };

    void Subdivide(Node& node, int depth)
    {
        if(depth >= mMaxDepth || node.Items.size() <= mMaxItemsPerNode)
        {
            return;
        }

        const DirectX::XMFLOAT3 center = node.Bounds.Center;
        const DirectX::XMFLOAT3 extents = node.Bounds.Extents;
        const DirectX::XMFLOAT3 childExtents(extents.x * 0.5f, extents.y * 0.5f, extents.z * 0.5f);

        std::array<std::vector<size_t>, 8> childItems;
        for(size_t itemIndex : node.Items)
        {
            for(int childIndex = 0; childIndex < 8; ++childIndex)
            {
                const DirectX::BoundingBox childBounds = ComputeChildBounds(center, childExtents, childIndex);
                if(childBounds.Intersects(mEntries[itemIndex].Bounds))
                {
                    childItems[childIndex].push_back(itemIndex);
                }
            }
        }

        bool hasChildren = false;
        for(int childIndex = 0; childIndex < 8; ++childIndex)
        {
            if(childItems[childIndex].empty() || childItems[childIndex].size() == node.Items.size())
            {
                continue;
            }

            node.Children[childIndex] = std::make_unique<Node>();
            node.Children[childIndex]->Bounds = ComputeChildBounds(center, childExtents, childIndex);
            node.Children[childIndex]->Items = std::move(childItems[childIndex]);
            Subdivide(*node.Children[childIndex], depth + 1);
            hasChildren = true;
        }

        if(hasChildren)
        {
            node.Items.clear();
        }
    }

    void QueryNode(const Node& node, const DirectX::BoundingFrustum& frustum, std::vector<size_t>& visiblePayloads) const
    {
        if(!frustum.Intersects(node.Bounds))
        {
            return;
        }

        if(node.IsLeaf())
        {
            for(size_t entryIndex : node.Items)
            {
                if(frustum.Intersects(mEntries[entryIndex].Bounds))
                {
                    visiblePayloads.push_back(mEntries[entryIndex].Payload);
                }
            }
            return;
        }

        for(const std::unique_ptr<Node>& child : node.Children)
        {
            if(child)
            {
                QueryNode(*child, frustum, visiblePayloads);
            }
        }
    }

    static DirectX::BoundingBox ComputeChildBounds(const DirectX::XMFLOAT3& parentCenter, const DirectX::XMFLOAT3& childExtents, int childIndex)
    {
        const float offsetX = (childIndex & 1) ? childExtents.x : -childExtents.x;
        const float offsetY = (childIndex & 2) ? childExtents.y : -childExtents.y;
        const float offsetZ = (childIndex & 4) ? childExtents.z : -childExtents.z;
        return DirectX::BoundingBox(
            DirectX::XMFLOAT3(parentCenter.x + offsetX, parentCenter.y + offsetY, parentCenter.z + offsetZ),
            childExtents);
    }

private:
    int mMaxDepth = 6;
    size_t mMaxItemsPerNode = 24;
    std::vector<Entry> mEntries;
    std::unique_ptr<Node> mRoot;
};
