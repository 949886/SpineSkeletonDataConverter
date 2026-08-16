#include "SkeletonData.h"
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace spine43 {

namespace {

enum ConstraintType43 {
    CONSTRAINT_IK_43 = 0,
    CONSTRAINT_PATH_43 = 1,
    CONSTRAINT_TRANSFORM_43 = 2,
    CONSTRAINT_PHYSICS_43 = 3,
    CONSTRAINT_SLIDER_43 = 4
};

struct ConstraintRef43 {
    int type = -1;
    size_t localIndex = 0;
};

[[noreturn]] void unsupported(const std::string& what) {
    throw std::runtime_error("Spine 4.3 feature cannot be represented by the current converter model: " + what);
}

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error("Invalid Spine 4.3 binary: " + message);
}

std::string requireString(const OptStr& value, const char* what) {
    if (!value) throw std::runtime_error(std::string("Invalid Spine 4.3 binary: missing ") + what);
    return *value;
}

std::string constraintName(const SkeletonData& skeletonData, const ConstraintRef43& ref) {
    switch (ref.type) {
        case CONSTRAINT_IK_43:
            return requireString(skeletonData.ikConstraints.at(ref.localIndex).name, "IK constraint name");
        case CONSTRAINT_PATH_43:
            return requireString(skeletonData.pathConstraints.at(ref.localIndex).name, "path constraint name");
        case CONSTRAINT_TRANSFORM_43:
            return requireString(skeletonData.transformConstraints.at(ref.localIndex).name, "transform constraint name");
        case CONSTRAINT_PHYSICS_43:
            return requireString(skeletonData.physicsConstraints.at(ref.localIndex).name, "physics constraint name");
        default:
            unsupported("slider constraints");
    }
}

Sequence readSequence43(DataInput* input) {
    Sequence sequence;
    sequence.count = readVarint(input, true);
    sequence.start = readVarint(input, true);
    sequence.digits = readVarint(input, true);
    sequence.setupIndex = readVarint(input, true);
    return sequence;
}

void readFloatArray43(DataInput* input, int n, std::vector<float>& array) {
    array.resize(n, 0);
    for (int i = 0; i < n; i++) array[i] = readFloat(input);
}

void readShortArray43(DataInput* input, int n, std::vector<unsigned short>& array) {
    array.resize(n, 0);
    for (int i = 0; i < n; i++) array[i] = static_cast<unsigned short>(readVarint(input, true));
}

int readVertices43(DataInput* input, std::vector<float>& vertices, bool weighted) {
    const int vertexCount = readVarint(input, true);
    if (!weighted) {
        readFloatArray43(input, vertexCount << 1, vertices);
        return vertexCount;
    }

    // 4.3 writes the total number of entries in the bone-index array before
    // the per-vertex bone data. This field did not exist in the 4.2 encoding.
    const int bonesLength = readVarint(input, true);
    int b = 0;
    int decodedVertices = 0;
    while (b < bonesLength) {
        const int boneCount = readVarint(input, true);
        vertices.push_back(static_cast<float>(boneCount));
        ++b;
        ++decodedVertices;
        for (int ii = 0; ii < boneCount; ++ii) {
            vertices.push_back(static_cast<float>(readVarint(input, true)));
            vertices.push_back(readFloat(input));
            vertices.push_back(readFloat(input));
            vertices.push_back(readFloat(input));
            ++b;
        }
    }
    require(decodedVertices == vertexCount, "weighted vertex count mismatch");
    return vertexCount;
}

void readCurve43(DataInput* input, TimelineFrame& frame, int timelineCount) {
    for (int i = 0; i < timelineCount * 4; i++) frame.curve.push_back(readFloat(input));
}

Timeline readTimeline43(DataInput* input, int frameCount, int valueNum) {
    Timeline timeline;
    if (frameCount <= 0) return timeline;
    float time = readFloat(input);
    float value1 = readFloat(input);
    float value2 = valueNum > 1 ? readFloat(input) : 0.0f;
    float value3 = valueNum > 2 ? readFloat(input) : 0.0f;
    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
        TimelineFrame frame;
        frame.time = time;
        frame.value1 = value1;
        if (valueNum > 1) frame.value2 = value2;
        if (valueNum > 2) frame.value3 = value3;
        time = readFloat(input);
        value1 = readFloat(input);
        if (valueNum > 1) value2 = readFloat(input);
        if (valueNum > 2) value3 = readFloat(input);
        switch (readSByte(input)) {
            case CURVE_STEPPED:
                frame.curveType = CurveType::CURVE_STEPPED;
                break;
            case CURVE_BEZIER:
                frame.curveType = CurveType::CURVE_BEZIER;
                readCurve43(input, frame, valueNum);
                break;
            default:
                break;
        }
        timeline.push_back(frame);
    }
    TimelineFrame frame;
    frame.time = time;
    frame.value1 = value1;
    if (valueNum > 1) frame.value2 = value2;
    if (valueNum > 2) frame.value3 = value3;
    timeline.push_back(frame);
    return timeline;
}

Skin readSkin43(DataInput* input, bool defaultSkin, SkeletonData* skeletonData,
                const std::vector<ConstraintRef43>& constraintRefs) {
    Skin skin;
    int slotCount = 0;
    if (defaultSkin) {
        slotCount = readVarint(input, true);
        skin.name = "default";
    } else {
        skin.name = requireString(readString(input), "skin name");
        if (skeletonData->nonessential) {
            Color color = readColor(input);
            if (color != Color{0xff, 0xff, 0xff, 0xff}) skin.color = color;
        }
        for (int i = 0, n = readVarint(input, true); i < n; i++) {
            const int boneIndex = readVarint(input, true);
            skin.bones.push_back(requireString(skeletonData->bones.at(boneIndex).name, "skin bone name"));
        }
        for (int i = 0, n = readVarint(input, true); i < n; i++) {
            const int constraintIndex = readVarint(input, true);
            require(constraintIndex >= 0 && constraintIndex < static_cast<int>(constraintRefs.size()), "skin constraint index out of range");
            const auto& ref = constraintRefs[constraintIndex];
            const std::string name = constraintName(*skeletonData, ref);
            switch (ref.type) {
                case CONSTRAINT_IK_43: skin.ik.push_back(name); break;
                case CONSTRAINT_TRANSFORM_43: skin.transform.push_back(name); break;
                case CONSTRAINT_PATH_43: skin.path.push_back(name); break;
                case CONSTRAINT_PHYSICS_43: skin.physics.push_back(name); break;
                default: unsupported("slider constraints referenced by a skin");
            }
        }
        slotCount = readVarint(input, true);
    }

    for (int i = 0; i < slotCount; i++) {
        const int slotIndex = readVarint(input, true);
        const std::string slotName = requireString(skeletonData->slots.at(slotIndex).name, "skin slot name");
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const std::string attachmentName = requireString(readStringRef(input, skeletonData), "attachment placeholder");
            Attachment attachment;
            const int flags = readByte(input);
            attachment.name = (flags & 8) != 0 ? requireString(readStringRef(input, skeletonData), "attachment name") : attachmentName;
            attachment.type = static_cast<AttachmentType>(flags & 0x7);
            switch (attachment.type) {
                case AttachmentType_Region: {
                    RegionAttachment region;
                    attachment.path = (flags & 16) != 0 ? requireString(readStringRef(input, skeletonData), "region path") : attachment.name;
                    if ((flags & 32) != 0) region.color = readColor(input);
                    if ((flags & 64) != 0) region.sequence = readSequence43(input);
                    if ((flags & 128) != 0) region.rotation = readFloat(input);
                    region.x = readFloat(input);
                    region.y = readFloat(input);
                    region.scaleX = readFloat(input);
                    region.scaleY = readFloat(input);
                    region.width = readFloat(input);
                    region.height = readFloat(input);
                    attachment.data = region;
                    break;
                }
                case AttachmentType_Boundingbox: {
                    BoundingboxAttachment box;
                    attachment.path = attachment.name;
                    box.vertexCount = readVertices43(input, box.vertices, (flags & 16) != 0);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) box.color = color;
                    }
                    attachment.data = box;
                    break;
                }
                case AttachmentType_Mesh: {
                    MeshAttachment mesh;
                    attachment.path = (flags & 16) != 0 ? requireString(readStringRef(input, skeletonData), "mesh path") : attachment.name;
                    if ((flags & 32) != 0) mesh.color = readColor(input);
                    if ((flags & 64) != 0) mesh.sequence = readSequence43(input);
                    mesh.hullLength = readVarint(input, true);
                    const int vertexCount = readVertices43(input, mesh.vertices, (flags & 128) != 0);
                    readFloatArray43(input, vertexCount << 1, mesh.uvs);
                    readShortArray43(input, (vertexCount * 2 - mesh.hullLength - 2) * 3, mesh.triangles);
                    const int timelineSlotCount = readVarint(input, true);
                    if (timelineSlotCount != 0) unsupported("mesh timelineSlots");
                    if (skeletonData->nonessential) {
                        readShortArray43(input, readVarint(input, true), mesh.edges);
                        mesh.width = readFloat(input);
                        mesh.height = readFloat(input);
                    }
                    attachment.data = mesh;
                    break;
                }
                case AttachmentType_Linkedmesh: {
                    LinkedmeshAttachment linkedMesh;
                    attachment.path = (flags & 16) != 0 ? requireString(readStringRef(input, skeletonData), "linked mesh path") : attachment.name;
                    if ((flags & 32) != 0) linkedMesh.color = readColor(input);
                    if ((flags & 64) != 0) linkedMesh.sequence = readSequence43(input);
                    linkedMesh.timelines = (flags & 128) != 0 ? 1 : 0;
                    const int sourceSlotIndex = readVarint(input, true);
                    if (sourceSlotIndex != slotIndex) unsupported("linked mesh source slot different from destination slot");
                    linkedMesh.skinIndex = readVarint(input, true);
                    linkedMesh.parentMesh = requireString(readStringRef(input, skeletonData), "linked mesh source");
                    if (skeletonData->nonessential) {
                        linkedMesh.width = readFloat(input);
                        linkedMesh.height = readFloat(input);
                    }
                    attachment.data = linkedMesh;
                    break;
                }
                case AttachmentType_Path: {
                    PathAttachment path;
                    attachment.path = attachment.name;
                    path.closed = (flags & 16) != 0;
                    path.constantSpeed = (flags & 32) != 0;
                    path.vertexCount = readVertices43(input, path.vertices, (flags & 64) != 0);
                    readFloatArray43(input, path.vertexCount / 3, path.lengths);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) path.color = color;
                    }
                    attachment.data = path;
                    break;
                }
                case AttachmentType_Point: {
                    PointAttachment point;
                    attachment.path = attachment.name;
                    // 4.3 order is rotation, x, y (4.2 was x, y, rotation).
                    point.rotation = readFloat(input);
                    point.x = readFloat(input);
                    point.y = readFloat(input);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) point.color = color;
                    }
                    attachment.data = point;
                    break;
                }
                case AttachmentType_Clipping: {
                    if ((flags & 32) != 0) unsupported("clipping convex flag");
                    if ((flags & 64) != 0) unsupported("clipping inverse flag");
                    ClippingAttachment clipping;
                    attachment.path = attachment.name;
                    clipping.endSlot = skeletonData->slots.at(readVarint(input, true)).name;
                    clipping.vertexCount = readVertices43(input, clipping.vertices, (flags & 16) != 0);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != Color{0xff, 0xff, 0xff, 0xff}) clipping.color = color;
                    }
                    attachment.data = clipping;
                    break;
                }
                default:
                    throw std::runtime_error("Invalid Spine 4.3 binary: unknown attachment type");
            }
            skin.attachments[slotName][attachmentName] = attachment;
        }
    }
    return skin;
}

const ConstraintRef43& checkedConstraintRef(const std::vector<ConstraintRef43>& refs, int index, int expectedType, const char* timelineName) {
    require(index >= 0 && index < static_cast<int>(refs.size()), std::string(timelineName) + " constraint index out of range");
    const auto& ref = refs[index];
    require(ref.type == expectedType, std::string(timelineName) + " references a constraint of the wrong type");
    return ref;
}

Animation readAnimation43(DataInput* input, SkeletonData* skeletonData,
                          const std::vector<ConstraintRef43>& constraintRefs) {
    Animation animation;
    animation.name = requireString(readString(input), "animation name");
    (void) readVarint(input, true); // timeline count is a capacity hint.

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const std::string slotName = requireString(skeletonData->slots.at(readVarint(input, true)).name, "animation slot name");
        MultiTimeline slotTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const SlotTimelineType timelineType = static_cast<SlotTimelineType>(readByte(input));
            const int frameCount = readVarint(input, true);
            switch (timelineType) {
                case SLOT_ATTACHMENT: {
                    Timeline timeline;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = readFloat(input);
                        frame.str1 = readStringRef(input, skeletonData);
                        timeline.push_back(frame);
                    }
                    slotTimeline["attachment"] = timeline;
                    break;
                }
                case SLOT_RGBA:
                case SLOT_RGB:
                case SLOT_RGBA2:
                case SLOT_RGB2:
                case SLOT_ALPHA: {
                    const int valueCount = timelineType == SLOT_RGBA ? 4 : timelineType == SLOT_RGB ? 3 :
                                           timelineType == SLOT_RGBA2 ? 7 : timelineType == SLOT_RGB2 ? 6 : 1;
                    (void) readVarint(input, true); // bezier count
                    Timeline timeline;
                    if (frameCount == 0) break;
                    float time = readFloat(input);
                    Color c1, c2;
                    float alpha = 0;
                    auto readValues = [&]() {
                        if (timelineType == SLOT_RGBA) c1 = readColor(input);
                        else if (timelineType == SLOT_RGB) c1 = readColor(input, false);
                        else if (timelineType == SLOT_RGBA2) { c1 = readColor(input); c2 = readColor(input, false); }
                        else if (timelineType == SLOT_RGB2) { c1 = readColor(input, false); c2 = readColor(input, false); }
                        else alpha = readByte(input) / 255.0f;
                    };
                    readValues();
                    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                        TimelineFrame frame;
                        frame.time = time;
                        if (timelineType == SLOT_ALPHA) frame.value1 = alpha;
                        else { frame.color1 = c1; if (timelineType == SLOT_RGBA2 || timelineType == SLOT_RGB2) frame.color2 = c2; }
                        if (frameIndex == frameCount - 1) { timeline.push_back(frame); break; }
                        time = readFloat(input);
                        readValues();
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: frame.curveType = CurveType::CURVE_STEPPED; break;
                            case CURVE_BEZIER: frame.curveType = CurveType::CURVE_BEZIER; readCurve43(input, frame, valueCount); break;
                            default: break;
                        }
                        timeline.push_back(frame);
                    }
                    const char* key = timelineType == SLOT_RGBA ? "rgba" : timelineType == SLOT_RGB ? "rgb" :
                                      timelineType == SLOT_RGBA2 ? "rgba2" : timelineType == SLOT_RGB2 ? "rgb2" : "alpha";
                    slotTimeline[key] = timeline;
                    break;
                }
                default:
                    throw std::runtime_error("Invalid Spine 4.3 binary: unknown slot timeline type");
            }
        }
        animation.slots[slotName] = slotTimeline;
    }

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const std::string boneName = requireString(skeletonData->bones.at(readVarint(input, true)).name, "animation bone name");
        MultiTimeline boneTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const BoneTimelineType timelineType = static_cast<BoneTimelineType>(readByte(input));
            const int frameCount = readVarint(input, true);
            if (timelineType == BONE_INHERIT) {
                Timeline timeline;
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame;
                    frame.time = readFloat(input);
                    frame.inherit = static_cast<Inherit>(readByte(input));
                    timeline.push_back(frame);
                }
                boneTimeline["inherit"] = timeline;
                continue;
            }
            (void) readVarint(input, true); // bezier count
            switch (timelineType) {
                case BONE_ROTATE: boneTimeline["rotate"] = readTimeline43(input, frameCount, 1); break;
                case BONE_TRANSLATE: boneTimeline["translate"] = readTimeline43(input, frameCount, 2); break;
                case BONE_TRANSLATEX: boneTimeline["translatex"] = readTimeline43(input, frameCount, 1); break;
                case BONE_TRANSLATEY: boneTimeline["translatey"] = readTimeline43(input, frameCount, 1); break;
                case BONE_SCALE: boneTimeline["scale"] = readTimeline43(input, frameCount, 2); break;
                case BONE_SCALEX: boneTimeline["scalex"] = readTimeline43(input, frameCount, 1); break;
                case BONE_SCALEY: boneTimeline["scaley"] = readTimeline43(input, frameCount, 1); break;
                case BONE_SHEAR: boneTimeline["shear"] = readTimeline43(input, frameCount, 2); break;
                case BONE_SHEARX: boneTimeline["shearx"] = readTimeline43(input, frameCount, 1); break;
                case BONE_SHEARY: boneTimeline["sheary"] = readTimeline43(input, frameCount, 1); break;
                default: throw std::runtime_error("Invalid Spine 4.3 binary: unknown bone timeline type");
            }
        }
        animation.bones[boneName] = boneTimeline;
    }

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const auto& ref = checkedConstraintRef(constraintRefs, readVarint(input, true), CONSTRAINT_IK_43, "IK timeline");
        const std::string ikName = requireString(skeletonData->ikConstraints.at(ref.localIndex).name, "IK name");
        const int frameCount = readVarint(input, true);
        (void) readVarint(input, true); // bezier count
        Timeline timeline;
        int flags = readByte(input);
        float time = readFloat(input);
        float mix = (flags & 1) != 0 ? ((flags & 2) != 0 ? readFloat(input) : 1.0f) : 0.0f;
        float softness = (flags & 4) != 0 ? readFloat(input) : 0.0f;
        bool bendPositive = (flags & 8) != 0;
        bool compress = (flags & 16) != 0;
        bool stretch = (flags & 32) != 0;
        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            TimelineFrame frame;
            frame.time = time;
            frame.value1 = mix;
            frame.value2 = softness;
            frame.bendPositive = bendPositive;
            frame.compress = compress;
            frame.stretch = stretch;
            if (frameIndex == frameCount - 1) { timeline.push_back(frame); break; }
            flags = readByte(input);
            time = readFloat(input);
            mix = (flags & 1) != 0 ? ((flags & 2) != 0 ? readFloat(input) : 1.0f) : 0.0f;
            softness = (flags & 4) != 0 ? readFloat(input) : 0.0f;
            bendPositive = (flags & 8) != 0;
            compress = (flags & 16) != 0;
            stretch = (flags & 32) != 0;
            if ((flags & 64) != 0) frame.curveType = CurveType::CURVE_STEPPED;
            else if ((flags & 128) != 0) { frame.curveType = CurveType::CURVE_BEZIER; readCurve43(input, frame, 2); }
            timeline.push_back(frame);
        }
        animation.ik[ikName] = timeline;
    }

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const auto& ref = checkedConstraintRef(constraintRefs, readVarint(input, true), CONSTRAINT_TRANSFORM_43, "transform timeline");
        const std::string transformName = requireString(skeletonData->transformConstraints.at(ref.localIndex).name, "transform name");
        const int frameCount = readVarint(input, true);
        (void) readVarint(input, true);
        Timeline timeline;
        if (frameCount > 0) {
            float time = readFloat(input), v1 = readFloat(input), v2 = readFloat(input), v3 = readFloat(input),
                  v4 = readFloat(input), v5 = readFloat(input), v6 = readFloat(input);
            for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                TimelineFrame frame;
                frame.time = time; frame.value1 = v1; frame.value2 = v2; frame.value3 = v3;
                frame.value4 = v4; frame.value5 = v5; frame.value6 = v6;
                if (frameIndex == frameCount - 1) { timeline.push_back(frame); break; }
                time = readFloat(input); v1 = readFloat(input); v2 = readFloat(input); v3 = readFloat(input);
                v4 = readFloat(input); v5 = readFloat(input); v6 = readFloat(input);
                switch (readSByte(input)) {
                    case CURVE_STEPPED: frame.curveType = CurveType::CURVE_STEPPED; break;
                    case CURVE_BEZIER: frame.curveType = CurveType::CURVE_BEZIER; readCurve43(input, frame, 6); break;
                    default: break;
                }
                timeline.push_back(frame);
            }
        }
        animation.transform[transformName] = timeline;
    }

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const auto& ref = checkedConstraintRef(constraintRefs, readVarint(input, true), CONSTRAINT_PATH_43, "path timeline");
        const std::string pathName = requireString(skeletonData->pathConstraints.at(ref.localIndex).name, "path name");
        MultiTimeline pathTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const PathTimelineType timelineType = static_cast<PathTimelineType>(readByte(input));
            const int frameCount = readVarint(input, true);
            (void) readVarint(input, true);
            if (timelineType == PATH_POSITION) pathTimeline["position"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PATH_SPACING) pathTimeline["spacing"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PATH_MIX) pathTimeline["mix"] = readTimeline43(input, frameCount, 3);
            else throw std::runtime_error("Invalid Spine 4.3 binary: unknown path timeline type");
        }
        animation.path[pathName] = pathTimeline;
    }

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const int encodedIndex = readVarint(input, true);
        std::string physicsName;
        if (encodedIndex != 0) {
            const auto& ref = checkedConstraintRef(constraintRefs, encodedIndex - 1, CONSTRAINT_PHYSICS_43, "physics timeline");
            physicsName = requireString(skeletonData->physicsConstraints.at(ref.localIndex).name, "physics name");
        }
        MultiTimeline physicsTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const PhysicsTimelineType timelineType = static_cast<PhysicsTimelineType>(readByte(input));
            const int frameCount = readVarint(input, true);
            if (timelineType == PHYSICS_RESET) {
                Timeline timeline;
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame; frame.time = readFloat(input); timeline.push_back(frame);
                }
                physicsTimeline["reset"] = timeline;
                continue;
            }
            (void) readVarint(input, true);
            if (timelineType == PHYSICS_INERTIA) physicsTimeline["inertia"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_STRENGTH) physicsTimeline["strength"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_DAMPING) physicsTimeline["damping"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_MASS) physicsTimeline["mass"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_WIND) physicsTimeline["wind"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_GRAVITY) physicsTimeline["gravity"] = readTimeline43(input, frameCount, 1);
            else if (timelineType == PHYSICS_MIX) physicsTimeline["mix"] = readTimeline43(input, frameCount, 1);
            else throw std::runtime_error("Invalid Spine 4.3 binary: unknown physics timeline type");
        }
        animation.physics[physicsName] = physicsTimeline;
    }

    const int sliderTimelineCount = readVarint(input, true);
    if (sliderTimelineCount != 0) unsupported("slider animation timelines");

    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const std::string skinName = skeletonData->skins.at(readVarint(input, true)).name;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            const std::string slotName = requireString(skeletonData->slots.at(readVarint(input, true)).name, "attachment timeline slot");
            for (int iii = 0, nnn = readVarint(input, true); iii < nnn; iii++) {
                const std::string attachmentName = requireString(readStringRef(input, skeletonData), "attachment timeline attachment");
                MultiTimeline attachmentTimeline;
                const AttachmentTimelineType timelineType = static_cast<AttachmentTimelineType>(readByte(input));
                const int frameCount = readVarint(input, true);
                if (timelineType == ATTACHMENT_DEFORM) {
                    (void) readVarint(input, true);
                    float time = frameCount > 0 ? readFloat(input) : 0.0f;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        TimelineFrame frame; frame.time = time;
                        size_t end = static_cast<size_t>(readVarint(input, true));
                        if (end != 0) {
                            const size_t start = static_cast<size_t>(readVarint(input, true));
                            frame.int1 = static_cast<int>(start);
                            end += start;
                            for (size_t v = start; v < end; v++) frame.vertices.push_back(readFloat(input));
                        }
                        if (frameIndex == frameCount - 1) { attachmentTimeline["deform"].push_back(frame); break; }
                        time = readFloat(input);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: frame.curveType = CurveType::CURVE_STEPPED; break;
                            case CURVE_BEZIER: frame.curveType = CurveType::CURVE_BEZIER; readCurve43(input, frame, 1); break;
                            default: break;
                        }
                        attachmentTimeline["deform"].push_back(frame);
                    }
                } else if (timelineType == ATTACHMENT_SEQUENCE) {
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        TimelineFrame frame;
                        frame.time = readFloat(input);
                        const int modeAndIndex = readInt(input);
                        frame.sequenceMode = static_cast<SequenceMode>(modeAndIndex & 0xf);
                        frame.int1 = modeAndIndex >> 4;
                        frame.value1 = readFloat(input);
                        attachmentTimeline["sequence"].push_back(frame);
                    }
                } else {
                    throw std::runtime_error("Invalid Spine 4.3 binary: unknown attachment timeline type");
                }
                animation.attachments[skinName][slotName][attachmentName] = attachmentTimeline;
            }
        }
    }

    const size_t drawOrderCount = static_cast<size_t>(readVarint(input, true));
    for (size_t i = 0; i < drawOrderCount; i++) {
        TimelineFrame frame;
        frame.time = readFloat(input);
        const size_t offsetCount = static_cast<size_t>(readVarint(input, true));
        for (size_t ii = 0; ii < offsetCount; ii++) {
            frame.offsets.push_back({requireString(skeletonData->slots.at(readVarint(input, true)).name, "draw order slot"),
                                     readVarint(input, true)});
        }
        animation.drawOrder.push_back(frame);
    }

    const size_t drawOrderFolderCount = static_cast<size_t>(readVarint(input, true));
    if (drawOrderFolderCount != 0) unsupported("draw order folder timelines");

    const int eventCount = readVarint(input, true);
    for (int i = 0; i < eventCount; i++) {
        TimelineFrame frame;
        frame.time = readFloat(input);
        const int eventIndex = readVarint(input, true);
        const EventData& eventData = skeletonData->events.at(eventIndex);
        frame.str1 = eventData.name;
        frame.int1 = readVarint(input, false);
        frame.value1 = readFloat(input);
        const OptStr str = readString(input);
        frame.str2 = str ? str : eventData.stringValue;
        if (eventData.audioPath && !eventData.audioPath->empty()) {
            frame.value2 = readFloat(input);
            frame.value3 = readFloat(input);
        }
        animation.events.push_back(frame);
    }

    if (skeletonData->nonessential) (void) readInt(input); // animation editor color
    return animation;
}

void mapTransformPropertyGraph43(DataInput* input, TransformConstraintData& data, int propertyCount,
                                 bool localSource, bool localTarget, bool additive, bool clamp) {
    // The legacy shared model can represent only the pre-4.3 identity-copy transform
    // constraint. Accept that exact subset and fail loudly for arbitrary property graphs.
    if (localSource || localTarget || additive || clamp)
        unsupported("generalized transform constraints using local/additive/clamp modes");

    bool seen[6] = {false, false, false, false, false, false};
    for (int i = 0; i < propertyCount; ++i) {
        const int fromType = readByte(input);
        require(fromType >= 0 && fromType < 6, "transform from-property type out of range");
        const float fromOffset = readFloat(input);
        const int toCount = readByte(input);
        if (fromOffset != 0.0f || toCount != 1)
            unsupported("generalized transform property mapping");
        const int toType = readByte(input);
        const float toOffset = readFloat(input);
        const float toMax = readFloat(input);
        const float toScale = readFloat(input);
        (void) toMax; // ignored only because clamp=false above.
        if (toType != fromType || toOffset != 0.0f || std::fabs(toScale - 1.0f) > 0.00001f)
            unsupported("generalized transform property mapping");
        seen[fromType] = true;
    }
    (void) seen;

    int flags = readByte(input);
    if ((flags & 1) != 0) data.offsetRotation = readFloat(input);
    if ((flags & 2) != 0) data.offsetX = readFloat(input);
    if ((flags & 4) != 0) data.offsetY = readFloat(input);
    if ((flags & 8) != 0) data.offsetScaleX = readFloat(input);
    if ((flags & 16) != 0) data.offsetScaleY = readFloat(input);
    if ((flags & 32) != 0) data.offsetShearY = readFloat(input);

    // 4.3 setup mix defaults are zero when omitted.
    data.mixRotate = data.mixX = data.mixY = data.mixScaleX = data.mixScaleY = data.mixShearY = 0.0f;
    flags = readByte(input);
    if ((flags & 1) != 0) data.mixRotate = readFloat(input);
    if ((flags & 2) != 0) data.mixX = readFloat(input);
    if ((flags & 4) != 0) data.mixY = readFloat(input);
    if ((flags & 8) != 0) data.mixScaleX = readFloat(input);
    if ((flags & 16) != 0) data.mixScaleY = readFloat(input);
    if ((flags & 32) != 0) data.mixShearY = readFloat(input);
}

} // namespace

SkeletonData readBinaryData(const Binary& binary) {
    SkeletonData skeletonData;
    DataInput input{binary.data(), binary.data() + binary.size()};

    const uint64_t highHash = static_cast<uint32_t>(readInt(&input));
    const uint64_t lowHash = static_cast<uint32_t>(readInt(&input));
    skeletonData.hash = (highHash << 32) | lowHash;
    skeletonData.version = requireString(readString(&input), "version");

    skeletonData.x = readFloat(&input);
    skeletonData.y = readFloat(&input);
    skeletonData.width = readFloat(&input);
    skeletonData.height = readFloat(&input);
    skeletonData.referenceScale = readFloat(&input);
    skeletonData.nonessential = readBoolean(&input);
    if (skeletonData.nonessential) {
        skeletonData.fps = readFloat(&input);
        skeletonData.imagesPath = readString(&input);
        skeletonData.audioPath = readString(&input);
    }

    for (int i = 0, n = readVarint(&input, true); i < n; i++)
        skeletonData.strings.push_back(requireString(readString(&input), "string table entry"));

    const int numBones = readVarint(&input, true);
    for (int i = 0; i < numBones; i++) {
        BoneData bone;
        bone.name = readString(&input);
        if (i != 0) bone.parent = skeletonData.bones.at(readVarint(&input, true)).name;
        bone.rotation = readFloat(&input);
        bone.x = readFloat(&input);
        bone.y = readFloat(&input);
        bone.scaleX = readFloat(&input);
        bone.scaleY = readFloat(&input);
        bone.shearX = readFloat(&input);
        bone.shearY = readFloat(&input);
        bone.inherit = static_cast<Inherit>(readByte(&input));
        bone.length = readFloat(&input);
        bone.skinRequired = readBoolean(&input);
        if (skeletonData.nonessential) {
            Color color = readColor(&input);
            if (color != Color{0x9b, 0x9b, 0x9b, 0xff}) bone.color = color;
            bone.icon = readString(&input);
            (void) readFloat(&input); // iconSize is editor-only in the shared model.
            (void) readFloat(&input); // iconRotation is editor-only in the shared model.
            bone.visible = readBoolean(&input);
        }
        skeletonData.bones.push_back(bone);
    }

    const int slotCount = readVarint(&input, true);
    for (int i = 0; i < slotCount; i++) {
        SlotData slot;
        slot.name = readString(&input);
        slot.bone = skeletonData.bones.at(readVarint(&input, true)).name;
        Color color = readColor(&input);
        if (color != Color{0xff, 0xff, 0xff, 0xff}) slot.color = color;
        const int dark = readInt(&input);
        if (dark != -1) {
            slot.darkColor = Color{static_cast<unsigned char>((dark >> 24) & 0xff),
                                   static_cast<unsigned char>((dark >> 16) & 0xff),
                                   static_cast<unsigned char>((dark >> 8) & 0xff), 0xff};
        }
        slot.attachmentName = readStringRef(&input, &skeletonData);
        slot.blendMode = static_cast<BlendMode>(readVarint(&input, true));
        if (skeletonData.nonessential) slot.visible = readBoolean(&input);
        skeletonData.slots.push_back(slot);
    }

    std::vector<ConstraintRef43> constraintRefs;
    const int constraintCount = readVarint(&input, true);
    constraintRefs.reserve(constraintCount);
    for (int order = 0; order < constraintCount; ++order) {
        const OptStr name = readString(&input);
        const int type = readByte(&input);
        if (type == CONSTRAINT_IK_43) {
            IKConstraintData data;
            data.name = name;
            data.order = static_cast<size_t>(order);
            for (int i = 0, n = readVarint(&input, true); i < n; ++i)
                data.bones.push_back(requireString(skeletonData.bones.at(readVarint(&input, true)).name, "IK bone"));
            data.target = skeletonData.bones.at(readVarint(&input, true)).name;
            const int flags = readByte(&input);
            data.skinRequired = (flags & 1) != 0;
            if ((flags & 2) != 0) {
                const int scaleYMode = readByte(&input);
                if (scaleYMode == 1) data.uniform = true;
                else if (scaleYMode == 2) unsupported("IK scaleY=volume");
            }
            data.bendPositive = (flags & 4) == 0;
            data.compress = (flags & 8) != 0;
            data.stretch = (flags & 16) != 0;
            data.mix = (flags & 32) != 0 ? ((flags & 64) != 0 ? readFloat(&input) : 1.0f) : 0.0f;
            if ((flags & 128) != 0) data.softness = readFloat(&input);
            const size_t local = skeletonData.ikConstraints.size();
            skeletonData.ikConstraints.push_back(data);
            constraintRefs.push_back({type, local});
        } else if (type == CONSTRAINT_TRANSFORM_43) {
            TransformConstraintData data;
            data.name = name;
            data.order = static_cast<size_t>(order);
            for (int i = 0, n = readVarint(&input, true); i < n; ++i)
                data.bones.push_back(requireString(skeletonData.bones.at(readVarint(&input, true)).name, "transform bone"));
            data.target = skeletonData.bones.at(readVarint(&input, true)).name;
            const int flags = readByte(&input);
            data.skinRequired = (flags & 1) != 0;
            const bool localSource = (flags & 2) != 0;
            const bool localTarget = (flags & 4) != 0;
            const bool additive = (flags & 8) != 0;
            const bool clamp = (flags & 16) != 0;
            const int propertyCount = flags >> 5;
            mapTransformPropertyGraph43(&input, data, propertyCount, localSource, localTarget, additive, clamp);
            const size_t local = skeletonData.transformConstraints.size();
            skeletonData.transformConstraints.push_back(data);
            constraintRefs.push_back({type, local});
        } else if (type == CONSTRAINT_PATH_43) {
            PathConstraintData data;
            data.name = name;
            data.order = static_cast<size_t>(order);
            for (int i = 0, n = readVarint(&input, true); i < n; ++i)
                data.bones.push_back(requireString(skeletonData.bones.at(readVarint(&input, true)).name, "path bone"));
            data.target = skeletonData.slots.at(readVarint(&input, true)).name;
            const int flags = readByte(&input);
            data.skinRequired = (flags & 1) != 0;
            data.positionMode = static_cast<PositionMode>((flags >> 1) & 1);
            data.spacingMode = static_cast<SpacingMode>((flags >> 2) & 3);
            data.rotateMode = static_cast<RotateMode>((flags >> 4) & 3);
            if ((flags & 128) != 0) data.offsetRotation = readFloat(&input);
            data.position = readFloat(&input);
            data.spacing = readFloat(&input);
            data.mixRotate = readFloat(&input);
            data.mixX = readFloat(&input);
            data.mixY = readFloat(&input);
            const size_t local = skeletonData.pathConstraints.size();
            skeletonData.pathConstraints.push_back(data);
            constraintRefs.push_back({type, local});
        } else if (type == CONSTRAINT_PHYSICS_43) {
            PhysicsConstraintData data;
            data.name = name;
            data.order = static_cast<size_t>(order);
            data.bone = skeletonData.bones.at(readVarint(&input, true)).name;
            int flags = readByte(&input);
            data.skinRequired = (flags & 1) != 0;
            if ((flags & 2) != 0) data.x = readFloat(&input);
            if ((flags & 4) != 0) data.y = readFloat(&input);
            if ((flags & 8) != 0) data.rotate = readFloat(&input);
            if ((flags & 16) != 0) {
                data.scaleX = readFloat(&input);
                if (data.scaleX < 0.0f) unsupported("physics scaleY mode");
            }
            if ((flags & 32) != 0) data.shearX = readFloat(&input);
            data.limit = (flags & 64) != 0 ? readFloat(&input) : 5000.0f;
            data.fps = static_cast<float>(readByte(&input));
            data.inertia = readFloat(&input);
            data.strength = readFloat(&input);
            data.damping = readFloat(&input);
            if ((flags & 128) != 0) {
                const float massInverse = readFloat(&input);
                data.mass = massInverse == 0.0f ? 0.0f : 1.0f / massInverse;
            }
            data.wind = readFloat(&input);
            data.gravity = readFloat(&input);
            flags = readByte(&input);
            data.inertiaGlobal = (flags & 1) != 0;
            data.strengthGlobal = (flags & 2) != 0;
            data.dampingGlobal = (flags & 4) != 0;
            data.massGlobal = (flags & 8) != 0;
            data.windGlobal = (flags & 16) != 0;
            data.gravityGlobal = (flags & 32) != 0;
            data.mixGlobal = (flags & 64) != 0;
            data.mix = (flags & 128) != 0 ? readFloat(&input) : 1.0f;
            const size_t local = skeletonData.physicsConstraints.size();
            skeletonData.physicsConstraints.push_back(data);
            constraintRefs.push_back({type, local});
        } else if (type == CONSTRAINT_SLIDER_43) {
            unsupported("slider constraints");
        } else {
            throw std::runtime_error("Invalid Spine 4.3 binary: unknown constraint type");
        }
    }

    skeletonData.skins.push_back(readSkin43(&input, true, &skeletonData, constraintRefs));
    const int extraSkinCount = readVarint(&input, true);
    for (int i = 0; i < extraSkinCount; i++)
        skeletonData.skins.push_back(readSkin43(&input, false, &skeletonData, constraintRefs));

    for (auto& skin : skeletonData.skins) {
        for (auto& [slotName, slotMap] : skin.attachments) {
            for (auto& [attachmentName, attachment] : slotMap) {
                if (attachment.type == AttachmentType_Linkedmesh) {
                    auto& linkedMesh = std::get<LinkedmeshAttachment>(attachment.data);
                    if (linkedMesh.skinIndex >= 0 && linkedMesh.skinIndex < static_cast<int>(skeletonData.skins.size()))
                        linkedMesh.skin = skeletonData.skins[linkedMesh.skinIndex].name;
                }
            }
        }
    }

    const int eventCount = readVarint(&input, true);
    for (int i = 0; i < eventCount; i++) {
        EventData event;
        event.name = requireString(readString(&input), "event name");
        event.intValue = readVarint(&input, false);
        event.floatValue = readFloat(&input);
        event.stringValue = readString(&input);
        event.audioPath = readString(&input);
        if (event.audioPath && !event.audioPath->empty()) {
            event.volume = readFloat(&input);
            event.balance = readFloat(&input);
        }
        skeletonData.events.push_back(event);
    }

    const int animationCount = readVarint(&input, true);
    for (int i = 0; i < animationCount; i++)
        skeletonData.animations.push_back(readAnimation43(&input, &skeletonData, constraintRefs));

    // 4.3 stores one animation reference after all animations for every slider
    // constraint. Slider constraints are rejected above, so there is nothing to consume.
    if (input.cursor != input.end) {
        std::ostringstream oss;
        oss << "parser left " << (input.end - input.cursor) << " trailing bytes";
        throw std::runtime_error("Invalid Spine 4.3 binary: " + oss.str());
    }
    return skeletonData;
}

} // namespace spine43
