#pragma once

#include <array>
#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "scene/Types.h"

namespace Scene
{
using ObjectId = uint64_t;

/**
 * Mutable object state stored in the editor document.
 */
struct EditableObject
{
    ObjectId id{};
    bx::Vec3 position{ 0.0F, 0.0F, 0.0F };
    std::vector<bx::Vec3> localVertices;
    std::vector<Face> faces;
    std::vector<std::array<uint16_t, 2>> edges;
};

/**
 * Owns object storage, index lookup, and current selection state.
 */
class Document
{
public:
    const std::vector<EditableObject> &objects() const;
    ObjectId selectedObjectId() const;
    bool hasSelection() const;
    bool selectObject(ObjectId id);
    bool addToSelection(ObjectId id);
    bool isObjectSelected(ObjectId id) const;
    bool clearSelection();
    bool selectNext();

    bool selectVertex(ObjectId objectId, uint16_t vertexIndex, bool additiveSelection);
    bool selectEdge(ObjectId objectId, uint16_t edgeIndex, bool additiveSelection);
    bool selectFace(ObjectId objectId, uint16_t faceIndex, bool additiveSelection);
    bool clearComponentSelection();
    ObjectId componentSelectionObjectId() const;
    bool isVertexSelected(ObjectId objectId, uint16_t vertexIndex) const;
    bool isEdgeSelected(ObjectId objectId, uint16_t edgeIndex) const;
    const std::unordered_set<uint16_t> &selectedVertexIndices() const;
    const std::unordered_set<uint16_t> &selectedEdgeIndices() const;
    const std::unordered_set<uint16_t> &selectedFaceIndices() const;

    EditableObject *findObject(ObjectId id);
    const EditableObject *findObject(ObjectId id) const;
    EditableObject *selectedObject();
    const EditableObject *selectedObject() const;
    void addObject(const EditableObject &object, std::size_t index);
    std::optional<EditableObject> removeObject(ObjectId id, std::size_t &outIndex);

private:
    void recomputeDerivedComponentSelection(const EditableObject &object);
    void reindexFrom(std::size_t index);

    std::vector<EditableObject> mObjects;
    std::unordered_map<ObjectId, std::size_t> mObjectIndices;
    std::unordered_set<ObjectId> mSelectedObjectIds;
    ObjectId mPrimarySelectedObjectId{};
    ObjectId mComponentObjectId{};
    std::unordered_set<uint16_t> mSelectedVertexIndices;
    std::unordered_set<uint16_t> mExplicitSelectedEdgeIndices;
    std::unordered_set<uint16_t> mExplicitSelectedFaceIndices;
    std::unordered_set<uint16_t> mResolvedSelectedEdgeIndices;
    std::unordered_set<uint16_t> mResolvedSelectedFaceIndices;
};

} // namespace Scene
