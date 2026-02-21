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
    /**
     * Returns objects in draw-order/index order.
     * @return Object container in document order.
     */
    const std::vector<EditableObject> &objects() const;

    /**
     * Returns currently selected object id, or zero if no selection.
     * @return Selected object id, or zero.
     */
    ObjectId selectedObjectId() const;

    /**
     * Returns true when an object is currently selected.
     * @return True when there is an active selection.
     */
    bool hasSelection() const;

    /**
     * Selects an object by id. Returns true when selection changed.
     * @param[in] id Object id to select.
     * @return True when selection state changed.
     */
    bool selectObject(ObjectId id);

    /**
     * Adds an object to the current selection set. Returns true when selection changed.
     * @param[in] id Object id to include in selection.
     * @return True when selection state changed.
     */
    bool addToSelection(ObjectId id);

    /**
     * Returns true when object id is part of current selection set.
     * @param[in] id Object id to test.
     * @return True when object is selected.
     */
    bool isObjectSelected(ObjectId id) const;

    /**
     * Clears selection. Returns true when selection changed.
     * @return True when selection state changed.
     */
    bool clearSelection();

    /**
     * Cycles selection to the next object. Returns true when changed.
     * @return True when selection state changed.
     */
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

    /**
     * Finds an object by id, or nullptr when missing.
     * @param[in] id Object id to find.
     * @return Mutable pointer to object, or nullptr.
     */
    EditableObject *findObject(ObjectId id);

    /**
     * Finds an object by id, or nullptr when missing.
     * @param[in] id Object id to find.
     * @return Immutable pointer to object, or nullptr.
     */
    const EditableObject *findObject(ObjectId id) const;

    /**
     * Returns selected object, or nullptr when nothing is selected.
     * @return Mutable pointer to selected object, or nullptr.
     */
    EditableObject *selectedObject();

    /**
     * Returns selected object, or nullptr when nothing is selected.
     * @return Immutable pointer to selected object, or nullptr.
     */
    const EditableObject *selectedObject() const;

    /**
     * Inserts an object at index, clamping to valid range.
     * @param[in] object Object to insert.
     * @param[in] index Preferred insertion index.
     */
    void addObject(const EditableObject &object, std::size_t index);

    /**
     * Removes an object by id and outputs previous index when found.
     * @param[in] id Object id to remove.
     * @param[out] outIndex Previous object index when removal succeeds.
     * @return Removed object when found, otherwise std::nullopt.
     */
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
