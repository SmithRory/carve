#include "Document.h"

#include <bx/math.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace Scene
{
namespace
{
uint32_t edgeKey(uint16_t a, uint16_t b)
{
    /* Canonicalize undirected edge endpoints into a stable 32-bit lookup key. */
    const uint16_t lo = bx::min(a, b);
    const uint16_t hi = bx::max(a, b);
    return (static_cast<uint32_t>(lo) << 16U) | static_cast<uint32_t>(hi);
}
} // namespace

const std::vector<EditableObject> &Document::objects() const
{
    return mObjects;
}

ObjectId Document::selectedObjectId() const
{
    return mPrimarySelectedObjectId;
}

bool Document::hasSelection() const
{
    return !mSelectedObjectIds.empty();
}

bool Document::selectObject(ObjectId id)
{
    if (id == 0U)
    {
        return clearSelection();
    }

    if (mObjectIndices.find(id) == mObjectIndices.end())
    {
        return false;
    }

    if ((mSelectedObjectIds.size() == 1U) && (mPrimarySelectedObjectId == id))
    {
        return false;
    }

    mSelectedObjectIds.clear();
    mSelectedObjectIds.insert(id);
    mPrimarySelectedObjectId = id;
    clearComponentSelection();
    return true;
}

bool Document::addToSelection(ObjectId id)
{
    if (id == 0U)
    {
        return false;
    }

    if (mObjectIndices.find(id) == mObjectIndices.end())
    {
        return false;
    }

    const auto inserted = mSelectedObjectIds.insert(id);
    mPrimarySelectedObjectId = id;
    clearComponentSelection();
    if (inserted.second)
    {
        return true;
    }

    return false;
}

bool Document::isObjectSelected(ObjectId id) const
{
    return mSelectedObjectIds.find(id) != mSelectedObjectIds.end();
}

bool Document::clearSelection()
{
    if (mSelectedObjectIds.empty())
    {
        return false;
    }

    mSelectedObjectIds.clear();
    mPrimarySelectedObjectId = 0U;
    clearComponentSelection();
    return true;
}

bool Document::selectNext()
{
    if (mObjects.empty())
    {
        return clearSelection();
    }

    if (mPrimarySelectedObjectId == 0U)
    {
        mSelectedObjectIds.clear();
        mSelectedObjectIds.insert(mObjects.front().id);
        mPrimarySelectedObjectId = mObjects.front().id;
        clearComponentSelection();
        return true;
    }

    const auto found = mObjectIndices.find(mPrimarySelectedObjectId);
    if (found == mObjectIndices.end())
    {
        mSelectedObjectIds.clear();
        mSelectedObjectIds.insert(mObjects.front().id);
        mPrimarySelectedObjectId = mObjects.front().id;
        clearComponentSelection();
        return true;
    }

    const std::size_t nextIndex = (found->second + 1U) % mObjects.size();
    const ObjectId nextId = mObjects[nextIndex].id;
    if ((mSelectedObjectIds.size() == 1U) && (nextId == mPrimarySelectedObjectId))
    {
        return false;
    }

    mSelectedObjectIds.clear();
    mSelectedObjectIds.insert(nextId);
    mPrimarySelectedObjectId = nextId;
    clearComponentSelection();
    return true;
}

bool Document::selectVertex(ObjectId objectId, uint16_t vertexIndex, bool additiveSelection)
{
    EditableObject *object = findObject(objectId);
    if (object == nullptr || vertexIndex >= object->localVertices.size())
    {
        return false;
    }

    bool changed = false;
    if (!additiveSelection || mComponentObjectId != objectId)
    {
        changed = clearComponentSelection() || changed;
        mComponentObjectId = objectId;
    }

    const auto inserted = mSelectedVertexIndices.insert(vertexIndex);
    changed = changed || inserted.second;
    const auto oldEdgeCount = mResolvedSelectedEdgeIndices.size();
    const auto oldFaceCount = mResolvedSelectedFaceIndices.size();
    recomputeDerivedComponentSelection(*object);
    changed = changed || (mResolvedSelectedEdgeIndices.size() != oldEdgeCount) || (mResolvedSelectedFaceIndices.size() != oldFaceCount);
    return changed;
}

bool Document::selectEdge(ObjectId objectId, uint16_t edgeIndex, bool additiveSelection)
{
    EditableObject *object = findObject(objectId);
    if (object == nullptr || edgeIndex >= object->edges.size())
    {
        return false;
    }

    bool changed = false;
    if (!additiveSelection || mComponentObjectId != objectId)
    {
        changed = clearComponentSelection() || changed;
        mComponentObjectId = objectId;
    }

    const auto inserted = mExplicitSelectedEdgeIndices.insert(edgeIndex);
    changed = changed || inserted.second;
    const auto oldEdgeCount = mResolvedSelectedEdgeIndices.size();
    const auto oldFaceCount = mResolvedSelectedFaceIndices.size();
    recomputeDerivedComponentSelection(*object);
    changed = changed || (mResolvedSelectedEdgeIndices.size() != oldEdgeCount) || (mResolvedSelectedFaceIndices.size() != oldFaceCount);
    return changed;
}

bool Document::selectFace(ObjectId objectId, uint16_t faceIndex, bool additiveSelection)
{
    EditableObject *object = findObject(objectId);
    if (object == nullptr)
    {
        return false;
    }

    const std::size_t faceCount = object->faces.size();
    if (faceIndex >= faceCount)
    {
        return false;
    }

    bool changed = false;
    if (!additiveSelection || mComponentObjectId != objectId)
    {
        changed = clearComponentSelection() || changed;
        mComponentObjectId = objectId;
    }

    const auto inserted = mExplicitSelectedFaceIndices.insert(faceIndex);
    changed = changed || inserted.second;
    const auto oldEdgeCount = mResolvedSelectedEdgeIndices.size();
    const auto oldFaceCount = mResolvedSelectedFaceIndices.size();
    recomputeDerivedComponentSelection(*object);
    changed = changed || (mResolvedSelectedEdgeIndices.size() != oldEdgeCount) || (mResolvedSelectedFaceIndices.size() != oldFaceCount);
    return changed;
}

bool Document::clearComponentSelection()
{
    if (mComponentObjectId == 0U && mSelectedVertexIndices.empty() && mExplicitSelectedEdgeIndices.empty() && mExplicitSelectedFaceIndices.empty() && mResolvedSelectedEdgeIndices.empty() && mResolvedSelectedFaceIndices.empty())
    {
        return false;
    }

    mComponentObjectId = 0U;
    mSelectedVertexIndices.clear();
    mExplicitSelectedEdgeIndices.clear();
    mExplicitSelectedFaceIndices.clear();
    mResolvedSelectedEdgeIndices.clear();
    mResolvedSelectedFaceIndices.clear();
    return true;
}

ObjectId Document::componentSelectionObjectId() const
{
    return mComponentObjectId;
}

bool Document::isVertexSelected(ObjectId objectId, uint16_t vertexIndex) const
{
    return (mComponentObjectId == objectId) && (mSelectedVertexIndices.find(vertexIndex) != mSelectedVertexIndices.end());
}

bool Document::isEdgeSelected(ObjectId objectId, uint16_t edgeIndex) const
{
    return (mComponentObjectId == objectId) && (mResolvedSelectedEdgeIndices.find(edgeIndex) != mResolvedSelectedEdgeIndices.end());
}

const std::unordered_set<uint16_t> &Document::selectedVertexIndices() const
{
    return mSelectedVertexIndices;
}

const std::unordered_set<uint16_t> &Document::selectedEdgeIndices() const
{
    return mResolvedSelectedEdgeIndices;
}

const std::unordered_set<uint16_t> &Document::selectedFaceIndices() const
{
    return mResolvedSelectedFaceIndices;
}

EditableObject *Document::findObject(ObjectId id)
{
    const auto found = mObjectIndices.find(id);
    if (found == mObjectIndices.end())
    {
        return nullptr;
    }

    return &mObjects[found->second];
}

const EditableObject *Document::findObject(ObjectId id) const
{
    const auto found = mObjectIndices.find(id);
    if (found == mObjectIndices.end())
    {
        return nullptr;
    }

    return &mObjects[found->second];
}

EditableObject *Document::selectedObject()
{
    return findObject(mPrimarySelectedObjectId);
}

const EditableObject *Document::selectedObject() const
{
    return findObject(mPrimarySelectedObjectId);
}

void Document::addObject(const EditableObject &object, std::size_t index)
{
    const std::size_t clampedIndex = bx::min(index, mObjects.size());
    mObjects.insert(mObjects.begin() + static_cast<std::ptrdiff_t>(clampedIndex), object);
    reindexFrom(clampedIndex);
}

std::optional<EditableObject> Document::removeObject(ObjectId id, std::size_t &outIndex)
{
    const auto found = mObjectIndices.find(id);
    if (found == mObjectIndices.end())
    {
        return std::nullopt;
    }

    outIndex = found->second;
    EditableObject removed = mObjects[outIndex];

    mObjects.erase(mObjects.begin() + static_cast<std::ptrdiff_t>(outIndex));
    mObjectIndices.erase(found);
    reindexFrom(outIndex);

    const auto selectedFound = mSelectedObjectIds.find(id);
    if (selectedFound != mSelectedObjectIds.end())
    {
        mSelectedObjectIds.erase(selectedFound);
        if (mPrimarySelectedObjectId == id)
        {
            if (mSelectedObjectIds.empty())
            {
                mPrimarySelectedObjectId = 0U;
            }
            else
            {
                mPrimarySelectedObjectId = *mSelectedObjectIds.begin();
            }
        }
    }

    if (mComponentObjectId == id)
    {
        clearComponentSelection();
    }

    return removed;
}

void Document::recomputeDerivedComponentSelection(const EditableObject &object)
{
    /* Start from explicitly selected edges, then add edges implied by selected endpoint pairs. */
    mResolvedSelectedEdgeIndices = mExplicitSelectedEdgeIndices;

    for (std::size_t edgeIndex = 0U; edgeIndex < object.edges.size(); ++edgeIndex)
    {
        const auto &edge = object.edges[edgeIndex];
        if (mSelectedVertexIndices.find(edge[0]) != mSelectedVertexIndices.end() && mSelectedVertexIndices.find(edge[1]) != mSelectedVertexIndices.end())
        {
            mResolvedSelectedEdgeIndices.insert(static_cast<uint16_t>(edgeIndex));
        }
    }

    /* A face is treated as selected when all of its boundary edges are selected. */
    mResolvedSelectedFaceIndices = mExplicitSelectedFaceIndices;
    std::unordered_map<uint32_t, uint16_t> edgeLookup;
    edgeLookup.reserve(object.edges.size());
    for (std::size_t edgeIndex = 0U; edgeIndex < object.edges.size(); ++edgeIndex)
    {
        edgeLookup[edgeKey(object.edges[edgeIndex][0], object.edges[edgeIndex][1])] = static_cast<uint16_t>(edgeIndex);
    }

    for (std::size_t faceIdx = 0U; faceIdx < object.faces.size(); ++faceIdx)
    {
        const Face &face = object.faces[faceIdx];
        if (face.size() < 3U)
        {
            continue;
        }

        bool allEdgesSelected = true;
        for (std::size_t i = 0U; i < face.size(); ++i)
        {
            const uint16_t v0 = face[i];
            const uint16_t v1 = face[(i + 1U) % face.size()];
            const auto edgeIt = edgeLookup.find(edgeKey(v0, v1));
            if (edgeIt == edgeLookup.end() || mResolvedSelectedEdgeIndices.find(edgeIt->second) == mResolvedSelectedEdgeIndices.end())
            {
                allEdgesSelected = false;
                break;
            }
        }

        if (allEdgesSelected)
        {
            const uint16_t faceIndex = static_cast<uint16_t>(faceIdx);
            mResolvedSelectedFaceIndices.insert(faceIndex);
        }
    }
}

void Document::reindexFrom(std::size_t index)
{
    for (std::size_t i = index; i < mObjects.size(); ++i)
    {
        mObjectIndices[mObjects[i].id] = i;
    }
}

} // namespace Scene
