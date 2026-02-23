#include "Document.h"

#include <array>
#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <unordered_map>

namespace Scene
{

/**
 * Returns objects in draw-order/index order.
 * @return Object container in document order.
 */
const std::vector<EditableObject> &Document::objects() const
{
    return mObjects;
}

/**
 * Returns currently selected object id, or zero if no selection.
 * @return Selected object id, or zero.
 */
ObjectId Document::selectedObjectId() const
{
    return mPrimarySelectedObjectId;
}

/**
 * Returns true when an object is currently selected.
 * @return True when there is an active selection.
 */
bool Document::hasSelection() const
{
    return !mSelectedObjectIds.empty();
}

/**
 * Selects an object by id. Returns true when selection changed.
 * @param[in] id Object id to select.
 * @return True when selection state changed.
 */
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

/**
 * Adds an object to the current selection set. Returns true when selection changed.
 * @param[in] id Object id to include in selection.
 * @return True when selection state changed.
 */
bool Document::addToSelection(ObjectId id)
{
    if (id == 0U)
    {
        return false;
    }

    if (!mObjectIndices.contains(id))
    {
        return false;
    }

    const auto inserted = mSelectedObjectIds.insert(id);
    mPrimarySelectedObjectId = id;
    clearComponentSelection();

    return inserted.second;
}

/**
 * Returns true when object id is part of current selection set.
 * @param[in] id Object id to test.
 * @return True when object is selected.
 */
bool Document::isObjectSelected(ObjectId id) const
{
    return mSelectedObjectIds.find(id) != mSelectedObjectIds.end();
}

/**
 * Clears selection. Returns true when selection changed.
 * @return True when selection state changed.
 */
bool Document::clearSelection()
{
    const bool hadSelectedObjects = !mSelectedObjectIds.empty();
    const bool hadPrimarySelection = mPrimarySelectedObjectId != 0U;
    const bool hadComponentSelection =
        (mComponentObjectId != 0U)
        || !mSelectedVertexIndices.empty()
        || !mExplicitSelectedEdgeIndices.empty()
        || !mExplicitSelectedFaceIndices.empty()
        || !mResolvedSelectedEdgeIndices.empty()
        || !mResolvedSelectedFaceIndices.empty();

    if (!hadSelectedObjects && !hadPrimarySelection && !hadComponentSelection)
    {
        return false;
    }

    mSelectedObjectIds.clear();
    mPrimarySelectedObjectId = 0U;
    clearComponentSelection();

    return true;
}

/**
 * Cycles selection to the next object. Returns true when changed.
 * @return True when selection state changed.
 */
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
    if (mComponentObjectId == 0U
        && mSelectedVertexIndices.empty()
        && mExplicitSelectedEdgeIndices.empty()
        && mExplicitSelectedFaceIndices.empty()
        && mResolvedSelectedEdgeIndices.empty()
        && mResolvedSelectedFaceIndices.empty())
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

/**
 * Finds an object by id, or nullptr when missing.
 * @param[in] id Object id to find.
 * @return Mutable pointer to object, or nullptr.
 */
EditableObject *Document::findObject(ObjectId id)
{
    const auto found = mObjectIndices.find(id);
    if (found == mObjectIndices.end())
    {
        return nullptr;
    }

    return &mObjects[found->second];
}

/**
 * Finds an object by id, or nullptr when missing.
 * @param[in] id Object id to find.
 * @return Immutable pointer to object, or nullptr.
 */
const EditableObject *Document::findObject(ObjectId id) const
{
    const auto found = mObjectIndices.find(id);
    if (found == mObjectIndices.end())
    {
        return nullptr;
    }

    return &mObjects[found->second];
}

/**
 * Returns selected object, or nullptr when nothing is selected.
 * @return Mutable pointer to selected object, or nullptr.
 */
EditableObject *Document::selectedObject()
{
    return findObject(mPrimarySelectedObjectId);
}

/**
 * Returns selected object, or nullptr when nothing is selected.
 * @return Immutable pointer to selected object, or nullptr.
 */
const EditableObject *Document::selectedObject() const
{
    return findObject(mPrimarySelectedObjectId);
}

/**
 * Inserts an object at index, clamping to valid range.
 * @param[in] object Object to insert.
 * @param[in] index Preferred insertion index.
 */
void Document::addObject(const EditableObject &object, std::size_t index)
{
    const std::size_t clampedIndex = bx::min(index, mObjects.size());
    mObjects.insert(mObjects.begin() + static_cast<std::ptrdiff_t>(clampedIndex), object);
    reindexFrom(clampedIndex);
}

/**
 * Removes an object by id and outputs previous index when found.
 * @param[in] id Object id to remove.
 * @param[out] outIndex Previous object index when removal succeeds.
 * @return Removed object when found, otherwise std::nullopt.
 */
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
    }

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
    else if (mPrimarySelectedObjectId != 0U)
    {
        const bool primaryExists = mObjectIndices.contains(mPrimarySelectedObjectId);
        const bool primarySelected = mSelectedObjectIds.contains(mPrimarySelectedObjectId);
        if (!primaryExists || !primarySelected)
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

    for (const std::size_t edgeIndex : std::views::iota(std::size_t{ 0U }, object.edges.size()))
    {
        const auto &edge = object.edges[edgeIndex];
        if (mSelectedVertexIndices.contains(edge.first) && mSelectedVertexIndices.contains(edge.second))
        {
            mResolvedSelectedEdgeIndices.insert(static_cast<uint16_t>(edgeIndex));
        }
    }

    /* A face is treated as selected when all of its boundary edges are selected. */
    mResolvedSelectedFaceIndices = mExplicitSelectedFaceIndices;
    std::unordered_map<EdgeHash, uint16_t> edgeLookup;
    edgeLookup.reserve(object.edges.size());
    for (const std::size_t edgeIndex : std::views::iota(std::size_t{ 0U }, object.edges.size()))
    {
        const auto &edge = object.edges[edgeIndex];
        edgeLookup[edgeHash(edge.first, edge.second)] = static_cast<uint16_t>(edgeIndex);
    }

    for (const std::size_t faceIdx : std::views::iota(std::size_t{ 0U }, object.faces.size()))
    {
        const Face &face = object.faces[faceIdx];
        if (face.size() < 3U)
        {
            continue;
        }

        bool allEdgesSelected = true;
        const uint16_t firstVertex = face.front();
        uint16_t previousVertex = firstVertex;
        for (const uint16_t currentVertex : face | std::views::drop(1))
        {
            const auto edgeIt = edgeLookup.find(edgeHash(previousVertex, currentVertex));
            if (edgeIt == edgeLookup.end() || !mResolvedSelectedEdgeIndices.contains(edgeIt->second))
            {
                allEdgesSelected = false;
                break;
            }
            previousVertex = currentVertex;
        }

        if (allEdgesSelected)
        {
            const auto edgeIt = edgeLookup.find(edgeHash(previousVertex, firstVertex));
            if (edgeIt == edgeLookup.end() || !mResolvedSelectedEdgeIndices.contains(edgeIt->second))
            {
                allEdgesSelected = false;
            }
        }

        if (allEdgesSelected)
        {
            const auto faceIndex = static_cast<uint16_t>(faceIdx);
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
