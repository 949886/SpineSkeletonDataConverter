#include "SkeletonData.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

namespace spine43 {

namespace {

enum ConstraintType43 {
    CONSTRAINT_IK_43 = 0,
    CONSTRAINT_PATH_43 = 1,
    CONSTRAINT_TRANSFORM_43 = 2,
    CONSTRAINT_PHYSICS_43 = 3
};

struct ConstraintRef43 {
    int type;
    size_t localIndex;
    size_t order;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("Spine 4.3 writer: " + message);
}

int boneIndex(const SkeletonData& data, const OptStr& name) {
    if (!name) fail("missing bone reference");
    for (size_t i = 0; i < data.bones.size(); ++i)
        if (data.bones[i].name && *data.bones[i].name == *name) return static_cast<int>(i);
    fail("bone not found: " + *name);
}

int boneIndex(const SkeletonData& data, const std::string& name) {
    return boneIndex(data, OptStr{name});
}

int slotIndex(const SkeletonData& data, const OptStr& name) {
    if (!name) fail("missing slot reference");
    for (size_t i = 0; i < data.slots.size(); ++i)
        if (data.slots[i].name && *data.slots[i].name == *name) return static_cast<int>(i);
    fail("slot not found: " + *name);
}

int slotIndex(const SkeletonData& data, const std::string& name) {
    return slotIndex(data, OptStr{name});
}

int skinIndex(const SkeletonData& data, const std::string& name) {
    for (size_t i = 0; i < data.skins.size(); ++i)
        if (data.skins[i].name == name) return static_cast<int>(i);
    fail("skin not found: " + name);
}

int eventIndex(const SkeletonData& data, const OptStr& name) {
    if (!name) fail("missing event reference");
    for (size_t i = 0; i < data.events.size(); ++i)
        if (data.events[i].name == *name) return static_cast<int>(i);
    fail("event not found: " + *name);
}

std::vector<ConstraintRef43> buildConstraintRefs(const SkeletonData& data) {
    std::vector<ConstraintRef43> refs;
    refs.reserve(data.ikConstraints.size() + data.transformConstraints.size() +
                 data.pathConstraints.size() + data.physicsConstraints.size());
    for (size_t i = 0; i < data.ikConstraints.size(); ++i)
        refs.push_back({CONSTRAINT_IK_43, i, data.ikConstraints[i].order});
    for (size_t i = 0; i < data.pathConstraints.size(); ++i)
        refs.push_back({CONSTRAINT_PATH_43, i, data.pathConstraints[i].order});
    for (size_t i = 0; i < data.transformConstraints.size(); ++i)
        refs.push_back({CONSTRAINT_TRANSFORM_43, i, data.transformConstraints[i].order});
    for (size_t i = 0; i < data.physicsConstraints.size(); ++i)
        refs.push_back({CONSTRAINT_PHYSICS_43, i, data.physicsConstraints[i].order});
    std::stable_sort(refs.begin(), refs.end(), [](const ConstraintRef43& a, const ConstraintRef43& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.type != b.type) return a.type < b.type;
        return a.localIndex < b.localIndex;
    });
    return refs;
}

int globalConstraintIndex(const SkeletonData& data, const std::vector<ConstraintRef43>& refs,
                          int type, const std::string& name) {
    for (size_t global = 0; global < refs.size(); ++global) {
        const auto& ref = refs[global];
        if (ref.type != type) continue;
        OptStr candidate;
        if (type == CONSTRAINT_IK_43) candidate = data.ikConstraints.at(ref.localIndex).name;
        else if (type == CONSTRAINT_TRANSFORM_43) candidate = data.transformConstraints.at(ref.localIndex).name;
        else if (type == CONSTRAINT_PATH_43) candidate = data.pathConstraints.at(ref.localIndex).name;
        else if (type == CONSTRAINT_PHYSICS_43) candidate = data.physicsConstraints.at(ref.localIndex).name;
        if (candidate && *candidate == name) return static_cast<int>(global);
    }
    fail("constraint not found: " + name);
}

void writeSequence43(Binary& binary, const Sequence& sequence) {
    writeVarint(binary, sequence.count, true);
    writeVarint(binary, sequence.start, true);
    writeVarint(binary, sequence.digits, true);
    writeVarint(binary, sequence.setupIndex, true);
}

void writeFloatArray43(Binary& binary, const std::vector<float>& values) {
    for (float v : values) writeFloat(binary, v);
}

void writeShortArray43(Binary& binary, const std::vector<unsigned short>& values) {
    for (unsigned short v : values) writeVarint(binary, static_cast<int>(v), true);
}

void writeVertices43(Binary& binary, const std::vector<float>& vertices, bool weighted) {
    if (!weighted) {
        writeVarint(binary, static_cast<int>(vertices.size() >> 1), true);
        writeFloatArray43(binary, vertices);
        return;
    }

    int vertexCount = 0;
    int bonesLength = 0;
    size_t cursor = 0;
    while (cursor < vertices.size()) {
        const int boneCount = static_cast<int>(vertices[cursor++]);
        if (boneCount < 0 || cursor + static_cast<size_t>(boneCount) * 4 > vertices.size())
            fail("malformed weighted vertices");
        ++vertexCount;
        bonesLength += boneCount + 1;
        cursor += static_cast<size_t>(boneCount) * 4;
    }
    writeVarint(binary, vertexCount, true);
    writeVarint(binary, bonesLength, true);

    cursor = 0;
    for (int i = 0; i < vertexCount; ++i) {
        const int boneCount = static_cast<int>(vertices[cursor++]);
        writeVarint(binary, boneCount, true);
        for (int ii = 0; ii < boneCount; ++ii) {
            writeVarint(binary, static_cast<int>(vertices[cursor++]), true);
            writeFloat(binary, vertices[cursor++]);
            writeFloat(binary, vertices[cursor++]);
            writeFloat(binary, vertices[cursor++]);
        }
    }
}

void writeCurve43(Binary& binary, const TimelineFrame& frame) {
    for (float v : frame.curve) writeFloat(binary, v);
}

void writeTimeline43(Binary& binary, const Timeline& timeline, int valueCount) {
    if (timeline.empty()) return;
    writeFloat(binary, timeline[0].time);
    writeFloat(binary, timeline[0].value1);
    if (valueCount > 1) writeFloat(binary, timeline[0].value2);
    if (valueCount > 2) writeFloat(binary, timeline[0].value3);
    for (size_t i = 1; i < timeline.size(); ++i) {
        writeFloat(binary, timeline[i].time);
        writeFloat(binary, timeline[i].value1);
        if (valueCount > 1) writeFloat(binary, timeline[i].value2);
        if (valueCount > 2) writeFloat(binary, timeline[i].value3);
        const CurveType curveType = timeline[i - 1].curveType;
        writeSByte(binary, static_cast<signed char>(curveType));
        if (curveType == CurveType::CURVE_BEZIER) writeCurve43(binary, timeline[i - 1]);
    }
}

void writeTransformTimeline43(Binary& binary, const Timeline& timeline) {
    if (timeline.empty()) return;
    auto writeFrame = [&](const TimelineFrame& frame) {
        writeFloat(binary, frame.time);
        writeFloat(binary, frame.value1);
        writeFloat(binary, frame.value2);
        writeFloat(binary, frame.value3);
        writeFloat(binary, frame.value4);
        writeFloat(binary, frame.value5);
        writeFloat(binary, frame.value6);
    };
    writeFrame(timeline[0]);
    for (size_t i = 1; i < timeline.size(); ++i) {
        writeFrame(timeline[i]);
        const CurveType curveType = timeline[i - 1].curveType;
        writeSByte(binary, static_cast<signed char>(curveType));
        if (curveType == CurveType::CURVE_BEZIER) writeCurve43(binary, timeline[i - 1]);
    }
}

void writeSkin43(Binary& binary, const Skin& skin, const SkeletonData& data,
                 const std::vector<ConstraintRef43>& refs, bool defaultSkin) {
    if (defaultSkin) {
        writeVarint(binary, static_cast<int>(skin.attachments.size()), true);
    } else {
        writeString(binary, OptStr{skin.name});
        if (data.nonessential)
            writeColor(binary, skin.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));

        writeVarint(binary, static_cast<int>(skin.bones.size()), true);
        for (const auto& name : skin.bones) writeVarint(binary, boneIndex(data, name), true);

        std::vector<int> constraints;
        constraints.reserve(skin.ik.size() + skin.transform.size() + skin.path.size() + skin.physics.size());
        for (const auto& name : skin.ik) constraints.push_back(globalConstraintIndex(data, refs, CONSTRAINT_IK_43, name));
        for (const auto& name : skin.transform) constraints.push_back(globalConstraintIndex(data, refs, CONSTRAINT_TRANSFORM_43, name));
        for (const auto& name : skin.path) constraints.push_back(globalConstraintIndex(data, refs, CONSTRAINT_PATH_43, name));
        for (const auto& name : skin.physics) constraints.push_back(globalConstraintIndex(data, refs, CONSTRAINT_PHYSICS_43, name));
        std::sort(constraints.begin(), constraints.end());
        constraints.erase(std::unique(constraints.begin(), constraints.end()), constraints.end());
        writeVarint(binary, static_cast<int>(constraints.size()), true);
        for (int index : constraints) writeVarint(binary, index, true);

        writeVarint(binary, static_cast<int>(skin.attachments.size()), true);
    }

    for (const auto& [slotName, slotMap] : skin.attachments) {
        const int sIndex = slotIndex(data, slotName);
        writeVarint(binary, sIndex, true);
        writeVarint(binary, static_cast<int>(slotMap.size()), true);

        for (const auto& [attachmentName, attachment] : slotMap) {
            writeStringRef(binary, OptStr{attachmentName}, data);
            unsigned char flags = static_cast<unsigned char>(attachment.type & 0x7);
            if (attachment.name != attachmentName) flags |= 8;

            switch (attachment.type) {
                case AttachmentType_Region: {
                    const auto& region = std::get<RegionAttachment>(attachment.data);
                    if (attachment.path != attachment.name) flags |= 16;
                    if (region.color) flags |= 32;
                    if (region.sequence) flags |= 64;
                    if (region.rotation != 0.0f) flags |= 128;
                    break;
                }
                case AttachmentType_Boundingbox: {
                    const auto& box = std::get<BoundingboxAttachment>(attachment.data);
                    if (box.vertices.size() != static_cast<size_t>(box.vertexCount) * 2) flags |= 16;
                    break;
                }
                case AttachmentType_Mesh: {
                    const auto& mesh = std::get<MeshAttachment>(attachment.data);
                    if (attachment.path != attachment.name) flags |= 16;
                    if (mesh.color) flags |= 32;
                    if (mesh.sequence) flags |= 64;
                    if (mesh.vertices.size() != mesh.uvs.size()) flags |= 128;
                    break;
                }
                case AttachmentType_Linkedmesh: {
                    const auto& linked = std::get<LinkedmeshAttachment>(attachment.data);
                    if (attachment.path != attachment.name) flags |= 16;
                    if (linked.color) flags |= 32;
                    if (linked.sequence) flags |= 64;
                    if (linked.timelines != 0) flags |= 128;
                    break;
                }
                case AttachmentType_Path: {
                    const auto& path = std::get<PathAttachment>(attachment.data);
                    if (path.closed) flags |= 16;
                    if (path.constantSpeed) flags |= 32;
                    if (path.vertices.size() != static_cast<size_t>(path.vertexCount) * 2) flags |= 64;
                    break;
                }
                case AttachmentType_Clipping: {
                    const auto& clipping = std::get<ClippingAttachment>(attachment.data);
                    if (clipping.vertices.size() != static_cast<size_t>(clipping.vertexCount) * 2) flags |= 16;
                    break;
                }
                case AttachmentType_Point:
                    break;
            }

            writeByte(binary, flags);
            if ((flags & 8) != 0) writeStringRef(binary, OptStr{attachment.name}, data);

            switch (attachment.type) {
                case AttachmentType_Region: {
                    const auto& region = std::get<RegionAttachment>(attachment.data);
                    if ((flags & 16) != 0) writeStringRef(binary, OptStr{attachment.path}, data);
                    if ((flags & 32) != 0) writeColor(binary, *region.color);
                    if ((flags & 64) != 0) writeSequence43(binary, *region.sequence);
                    if ((flags & 128) != 0) writeFloat(binary, region.rotation);
                    writeFloat(binary, region.x);
                    writeFloat(binary, region.y);
                    writeFloat(binary, region.scaleX);
                    writeFloat(binary, region.scaleY);
                    writeFloat(binary, region.width);
                    writeFloat(binary, region.height);
                    break;
                }
                case AttachmentType_Boundingbox: {
                    const auto& box = std::get<BoundingboxAttachment>(attachment.data);
                    writeVertices43(binary, box.vertices, (flags & 16) != 0);
                    if (data.nonessential) writeColor(binary, box.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));
                    break;
                }
                case AttachmentType_Mesh: {
                    const auto& mesh = std::get<MeshAttachment>(attachment.data);
                    if ((flags & 16) != 0) writeStringRef(binary, OptStr{attachment.path}, data);
                    if ((flags & 32) != 0) writeColor(binary, *mesh.color);
                    if ((flags & 64) != 0) writeSequence43(binary, *mesh.sequence);

                    const int actualHullLength = static_cast<int>(mesh.uvs.size()) -
                                                 static_cast<int>(mesh.triangles.size() / 3) - 2;
                    writeVarint(binary, actualHullLength, true);
                    writeVertices43(binary, mesh.vertices, (flags & 128) != 0);
                    writeFloatArray43(binary, mesh.uvs);
                    writeShortArray43(binary, mesh.triangles);
                    writeVarint(binary, 0, true);
                    if (data.nonessential) {
                        writeVarint(binary, static_cast<int>(mesh.edges.size()), true);
                        writeShortArray43(binary, mesh.edges);
                        writeFloat(binary, mesh.width);
                        writeFloat(binary, mesh.height);
                    }
                    break;
                }
                case AttachmentType_Linkedmesh: {
                    const auto& linked = std::get<LinkedmeshAttachment>(attachment.data);
                    if ((flags & 16) != 0) writeStringRef(binary, OptStr{attachment.path}, data);
                    if ((flags & 32) != 0) writeColor(binary, *linked.color);
                    if ((flags & 64) != 0) writeSequence43(binary, *linked.sequence);
                    writeVarint(binary, sIndex, true);
                    int skIndex = 0;
                    if (linked.skin) skIndex = skinIndex(data, *linked.skin);
                    writeVarint(binary, skIndex, true);
                    writeStringRef(binary, OptStr{linked.parentMesh}, data);
                    if (data.nonessential) {
                        writeFloat(binary, linked.width);
                        writeFloat(binary, linked.height);
                    }
                    break;
                }
                case AttachmentType_Path: {
                    const auto& path = std::get<PathAttachment>(attachment.data);
                    writeVertices43(binary, path.vertices, (flags & 64) != 0);
                    writeFloatArray43(binary, path.lengths);
                    if (data.nonessential) writeColor(binary, path.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));
                    break;
                }
                case AttachmentType_Point: {
                    const auto& point = std::get<PointAttachment>(attachment.data);
                    writeFloat(binary, point.rotation);
                    writeFloat(binary, point.x);
                    writeFloat(binary, point.y);
                    if (data.nonessential) writeColor(binary, point.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));
                    break;
                }
                case AttachmentType_Clipping: {
                    const auto& clipping = std::get<ClippingAttachment>(attachment.data);
                    writeVarint(binary, slotIndex(data, clipping.endSlot), true);
                    writeVertices43(binary, clipping.vertices, (flags & 16) != 0);
                    if (data.nonessential) writeColor(binary, clipping.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));
                    break;
                }
            }
        }
    }
}

void writeIKConstraint43(Binary& binary, const IKConstraintData& ik, const SkeletonData& data) {
    writeString(binary, ik.name);
    writeByte(binary, CONSTRAINT_IK_43);
    writeVarint(binary, static_cast<int>(ik.bones.size()), true);
    for (const auto& name : ik.bones) writeVarint(binary, boneIndex(data, name), true);
    writeVarint(binary, boneIndex(data, ik.target), true);

    unsigned char flags = 0;
    if (ik.skinRequired) flags |= 1;
    if (ik.uniform) flags |= 2;
    if (!ik.bendPositive) flags |= 4;
    if (ik.compress) flags |= 8;
    if (ik.stretch) flags |= 16;
    if (ik.mix != 0.0f) {
        flags |= 32;
        if (ik.mix != 1.0f) flags |= 64;
    }
    if (ik.softness != 0.0f) flags |= 128;
    writeByte(binary, flags);
    if ((flags & 2) != 0) writeByte(binary, 1);
    if ((flags & 32) != 0 && (flags & 64) != 0) writeFloat(binary, ik.mix);
    if ((flags & 128) != 0) writeFloat(binary, ik.softness);
}

void writeTransformConstraint43(Binary& binary, const TransformConstraintData& tr, const SkeletonData& data) {
    writeString(binary, tr.name);
    writeByte(binary, CONSTRAINT_TRANSFORM_43);
    writeVarint(binary, static_cast<int>(tr.bones.size()), true);
    for (const auto& name : tr.bones) writeVarint(binary, boneIndex(data, name), true);
    writeVarint(binary, boneIndex(data, tr.target), true);

    unsigned char flags = static_cast<unsigned char>(6 << 5);
    if (tr.skinRequired) flags |= 1;
    if (tr.local) flags |= 2 | 4;
    if (tr.relative) flags |= 8;
    writeByte(binary, flags);
    for (int property = 0; property < 6; ++property) {
        writeByte(binary, static_cast<unsigned char>(property));
        writeFloat(binary, 0.0f);
        writeByte(binary, 1);
        writeByte(binary, static_cast<unsigned char>(property));
        writeFloat(binary, 0.0f);
        writeFloat(binary, 0.0f);
        writeFloat(binary, 1.0f);
    }

    unsigned char offsetFlags = 0;
    if (tr.offsetRotation != 0.0f) offsetFlags |= 1;
    if (tr.offsetX != 0.0f) offsetFlags |= 2;
    if (tr.offsetY != 0.0f) offsetFlags |= 4;
    if (tr.offsetScaleX != 0.0f) offsetFlags |= 8;
    if (tr.offsetScaleY != 0.0f) offsetFlags |= 16;
    if (tr.offsetShearY != 0.0f) offsetFlags |= 32;
    writeByte(binary, offsetFlags);
    if (offsetFlags & 1) writeFloat(binary, tr.offsetRotation);
    if (offsetFlags & 2) writeFloat(binary, tr.offsetX);
    if (offsetFlags & 4) writeFloat(binary, tr.offsetY);
    if (offsetFlags & 8) writeFloat(binary, tr.offsetScaleX);
    if (offsetFlags & 16) writeFloat(binary, tr.offsetScaleY);
    if (offsetFlags & 32) writeFloat(binary, tr.offsetShearY);

    unsigned char mixFlags = 0;
    if (tr.mixRotate != 0.0f) mixFlags |= 1;
    if (tr.mixX != 0.0f) mixFlags |= 2;
    if (tr.mixY != 0.0f) mixFlags |= 4;
    if (tr.mixScaleX != 0.0f) mixFlags |= 8;
    if (tr.mixScaleY != 0.0f) mixFlags |= 16;
    if (tr.mixShearY != 0.0f) mixFlags |= 32;
    writeByte(binary, mixFlags);
    if (mixFlags & 1) writeFloat(binary, tr.mixRotate);
    if (mixFlags & 2) writeFloat(binary, tr.mixX);
    if (mixFlags & 4) writeFloat(binary, tr.mixY);
    if (mixFlags & 8) writeFloat(binary, tr.mixScaleX);
    if (mixFlags & 16) writeFloat(binary, tr.mixScaleY);
    if (mixFlags & 32) writeFloat(binary, tr.mixShearY);
}

void writePathConstraint43(Binary& binary, const PathConstraintData& path, const SkeletonData& data) {
    writeString(binary, path.name);
    writeByte(binary, CONSTRAINT_PATH_43);
    writeVarint(binary, static_cast<int>(path.bones.size()), true);
    for (const auto& name : path.bones) writeVarint(binary, boneIndex(data, name), true);
    writeVarint(binary, slotIndex(data, path.target), true);

    unsigned char flags = 0;
    if (path.skinRequired) flags |= 1;
    flags |= (static_cast<unsigned char>(path.positionMode) & 1) << 1;
    flags |= (static_cast<unsigned char>(path.spacingMode) & 3) << 2;
    flags |= (static_cast<unsigned char>(path.rotateMode) & 3) << 4;
    if (path.offsetRotation != 0.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 128) writeFloat(binary, path.offsetRotation);
    writeFloat(binary, path.position);
    writeFloat(binary, path.spacing);
    writeFloat(binary, path.mixRotate);
    writeFloat(binary, path.mixX);
    writeFloat(binary, path.mixY);
}

void writePhysicsConstraint43(Binary& binary, const PhysicsConstraintData& physics, const SkeletonData& data) {
    writeString(binary, physics.name);
    writeByte(binary, CONSTRAINT_PHYSICS_43);
    writeVarint(binary, boneIndex(data, physics.bone), true);

    unsigned char flags = 0;
    if (physics.skinRequired) flags |= 1;
    if (physics.x != 0.0f) flags |= 2;
    if (physics.y != 0.0f) flags |= 4;
    if (physics.rotate != 0.0f) flags |= 8;
    if (physics.scaleX != 0.0f) flags |= 16;
    if (physics.shearX != 0.0f) flags |= 32;
    if (physics.limit != 5000.0f) flags |= 64;
    if (physics.mass != 1.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 2) writeFloat(binary, physics.x);
    if (flags & 4) writeFloat(binary, physics.y);
    if (flags & 8) writeFloat(binary, physics.rotate);
    if (flags & 16) writeFloat(binary, physics.scaleX);
    if (flags & 32) writeFloat(binary, physics.shearX);
    if (flags & 64) writeFloat(binary, physics.limit);
    writeByte(binary, static_cast<unsigned char>(static_cast<int>(physics.fps)));
    writeFloat(binary, physics.inertia);
    writeFloat(binary, physics.strength);
    writeFloat(binary, physics.damping);
    if (flags & 128) writeFloat(binary, physics.mass == 0.0f ? 0.0f : 1.0f / physics.mass);
    writeFloat(binary, physics.wind);
    writeFloat(binary, physics.gravity);

    flags = 0;
    if (physics.inertiaGlobal) flags |= 1;
    if (physics.strengthGlobal) flags |= 2;
    if (physics.dampingGlobal) flags |= 4;
    if (physics.massGlobal) flags |= 8;
    if (physics.windGlobal) flags |= 16;
    if (physics.gravityGlobal) flags |= 32;
    if (physics.mixGlobal) flags |= 64;
    if (physics.mix != 1.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 128) writeFloat(binary, physics.mix);
}

void writeAnimation43(Binary& binary, const Animation& animation, const SkeletonData& data,
                      const std::vector<ConstraintRef43>& refs) {
    writeString(binary, OptStr{animation.name});
    writeVarint(binary, 0, true);

    writeVarint(binary, static_cast<int>(animation.slots.size()), true);
    for (const auto& [slotName, multi] : animation.slots) {
        writeVarint(binary, slotIndex(data, slotName), true);
        writeVarint(binary, static_cast<int>(multi.size()), true);
        for (const auto& [timelineName, timeline] : multi) {
            const auto it = slotTimelineTypeMap.find(timelineName);
            if (it == slotTimelineTypeMap.end()) fail("unknown slot timeline: " + timelineName);
            const SlotTimelineType type = it->second;
            writeByte(binary, static_cast<unsigned char>(type));
            writeVarint(binary, static_cast<int>(timeline.size()), true);
            if (type == SLOT_ATTACHMENT) {
                for (const auto& frame : timeline) {
                    writeFloat(binary, frame.time);
                    writeStringRef(binary, frame.str1, data);
                }
                continue;
            }

            const int valueCount = type == SLOT_RGBA ? 4 : type == SLOT_RGB ? 3 :
                                   type == SLOT_RGBA2 ? 7 : type == SLOT_RGB2 ? 6 : 1;
            writeVarint(binary, static_cast<int>(timeline.size()) * valueCount, true);
            if (timeline.empty()) continue;
            auto writeValues = [&](const TimelineFrame& frame) {
                if (type == SLOT_RGBA) writeColor(binary, frame.color1.value());
                else if (type == SLOT_RGB) writeColor(binary, frame.color1.value(), false);
                else if (type == SLOT_RGBA2) {
                    writeColor(binary, frame.color1.value());
                    writeColor(binary, frame.color2.value(), false);
                } else if (type == SLOT_RGB2) {
                    writeColor(binary, frame.color1.value(), false);
                    writeColor(binary, frame.color2.value(), false);
                } else {
                    float alpha = std::max(0.0f, std::min(1.0f, frame.value1));
                    writeByte(binary, static_cast<unsigned char>(std::lround(alpha * 255.0f)));
                }
            };
            writeFloat(binary, timeline[0].time);
            writeValues(timeline[0]);
            for (size_t i = 1; i < timeline.size(); ++i) {
                writeFloat(binary, timeline[i].time);
                writeValues(timeline[i]);
                const CurveType curveType = timeline[i - 1].curveType;
                writeSByte(binary, static_cast<signed char>(curveType));
                if (curveType == CURVE_BEZIER) writeCurve43(binary, timeline[i - 1]);
            }
        }
    }

    writeVarint(binary, static_cast<int>(animation.bones.size()), true);
    for (const auto& [boneName, multi] : animation.bones) {
        writeVarint(binary, boneIndex(data, boneName), true);
        writeVarint(binary, static_cast<int>(multi.size()), true);
        for (const auto& [timelineName, timeline] : multi) {
            const auto it = boneTimelineTypeMap.find(timelineName);
            if (it == boneTimelineTypeMap.end()) fail("unknown bone timeline: " + timelineName);
            const BoneTimelineType type = it->second;
            writeByte(binary, static_cast<unsigned char>(type));
            writeVarint(binary, static_cast<int>(timeline.size()), true);
            if (type == BONE_INHERIT) {
                for (const auto& frame : timeline) {
                    writeFloat(binary, frame.time);
                    writeByte(binary, static_cast<unsigned char>(frame.inherit));
                }
                continue;
            }
            const int valueCount = (type == BONE_TRANSLATE || type == BONE_SCALE || type == BONE_SHEAR) ? 2 : 1;
            writeVarint(binary, static_cast<int>(timeline.size()) * valueCount, true);
            writeTimeline43(binary, timeline, valueCount);
        }
    }

    writeVarint(binary, static_cast<int>(animation.ik.size()), true);
    for (const auto& [name, timeline] : animation.ik) {
        writeVarint(binary, globalConstraintIndex(data, refs, CONSTRAINT_IK_43, name), true);
        writeVarint(binary, static_cast<int>(timeline.size()), true);
        writeVarint(binary, static_cast<int>(timeline.size()) * 2, true);
        if (timeline.empty()) continue;
        for (size_t i = 0; i < timeline.size(); ++i) {
            unsigned char flags = 0;
            if (timeline[i].value1 != 0.0f) {
                flags |= 1;
                if (timeline[i].value1 != 1.0f) flags |= 2;
            }
            if (timeline[i].value2 != 0.0f) flags |= 4;
            if (timeline[i].bendPositive) flags |= 8;
            if (timeline[i].compress) flags |= 16;
            if (timeline[i].stretch) flags |= 32;
            if (i > 0) {
                if (timeline[i - 1].curveType == CURVE_STEPPED) flags |= 64;
                else if (timeline[i - 1].curveType == CURVE_BEZIER) flags |= 128;
            }
            writeByte(binary, flags);
            writeFloat(binary, timeline[i].time);
            if ((flags & 1) && (flags & 2)) writeFloat(binary, timeline[i].value1);
            if (flags & 4) writeFloat(binary, timeline[i].value2);
            if (i > 0 && (flags & 128)) writeCurve43(binary, timeline[i - 1]);
        }
    }

    writeVarint(binary, static_cast<int>(animation.transform.size()), true);
    for (const auto& [name, timeline] : animation.transform) {
        writeVarint(binary, globalConstraintIndex(data, refs, CONSTRAINT_TRANSFORM_43, name), true);
        writeVarint(binary, static_cast<int>(timeline.size()), true);
        writeVarint(binary, static_cast<int>(timeline.size()) * 6, true);
        writeTransformTimeline43(binary, timeline);
    }

    writeVarint(binary, static_cast<int>(animation.path.size()), true);
    for (const auto& [name, multi] : animation.path) {
        writeVarint(binary, globalConstraintIndex(data, refs, CONSTRAINT_PATH_43, name), true);
        writeVarint(binary, static_cast<int>(multi.size()), true);
        for (const auto& [timelineName, timeline] : multi) {
            const auto it = pathTimelineTypeMap.find(timelineName);
            if (it == pathTimelineTypeMap.end()) fail("unknown path timeline: " + timelineName);
            const PathTimelineType type = it->second;
            writeByte(binary, static_cast<unsigned char>(type));
            writeVarint(binary, static_cast<int>(timeline.size()), true);
            const int valueCount = type == PATH_MIX ? 3 : 1;
            writeVarint(binary, static_cast<int>(timeline.size()) * valueCount, true);
            writeTimeline43(binary, timeline, valueCount);
        }
    }

    writeVarint(binary, static_cast<int>(animation.physics.size()), true);
    for (const auto& [name, multi] : animation.physics) {
        const int encoded = name.empty() ? 0 :
            globalConstraintIndex(data, refs, CONSTRAINT_PHYSICS_43, name) + 1;
        writeVarint(binary, encoded, true);
        writeVarint(binary, static_cast<int>(multi.size()), true);
        for (const auto& [timelineName, timeline] : multi) {
            const auto it = physicsTimelineTypeMap.find(timelineName);
            if (it == physicsTimelineTypeMap.end()) fail("unknown physics timeline: " + timelineName);
            const PhysicsTimelineType type = it->second;
            writeByte(binary, static_cast<unsigned char>(type));
            writeVarint(binary, static_cast<int>(timeline.size()), true);
            if (type == PHYSICS_RESET) {
                for (const auto& frame : timeline) writeFloat(binary, frame.time);
            } else {
                writeVarint(binary, static_cast<int>(timeline.size()), true);
                writeTimeline43(binary, timeline, 1);
            }
        }
    }

    writeVarint(binary, 0, true);

    writeVarint(binary, static_cast<int>(animation.attachments.size()), true);
    for (const auto& [skinName, skinMap] : animation.attachments) {
        writeVarint(binary, skinIndex(data, skinName), true);
        writeVarint(binary, static_cast<int>(skinMap.size()), true);
        for (const auto& [slotName, slotMap] : skinMap) {
            writeVarint(binary, slotIndex(data, slotName), true);
            writeVarint(binary, static_cast<int>(slotMap.size()), true);
            for (const auto& [attachmentName, multi] : slotMap) {
                writeStringRef(binary, OptStr{attachmentName}, data);
                if (multi.size() != 1) fail("attachment timeline entry must contain one timeline type");
                const auto& [timelineName, timeline] = *multi.begin();
                const auto it = attachmentTimelineTypeMap.find(timelineName);
                if (it == attachmentTimelineTypeMap.end()) fail("unknown attachment timeline: " + timelineName);
                const AttachmentTimelineType type = it->second;
                writeByte(binary, static_cast<unsigned char>(type));
                writeVarint(binary, static_cast<int>(timeline.size()), true);
                if (type == ATTACHMENT_DEFORM) {
                    writeVarint(binary, static_cast<int>(timeline.size()), true);
                    if (timeline.empty()) continue;
                    writeFloat(binary, timeline[0].time);
                    for (size_t i = 0; i < timeline.size(); ++i) {
                        writeVarint(binary, static_cast<int>(timeline[i].vertices.size()), true);
                        if (!timeline[i].vertices.empty()) {
                            writeVarint(binary, timeline[i].int1, true);
                            for (float v : timeline[i].vertices) writeFloat(binary, v);
                        }
                        if (i + 1 == timeline.size()) break;
                        writeFloat(binary, timeline[i + 1].time);
                        const CurveType curveType = timeline[i].curveType;
                        writeSByte(binary, static_cast<signed char>(curveType));
                        if (curveType == CURVE_BEZIER) writeCurve43(binary, timeline[i]);
                    }
                } else {
                    for (const auto& frame : timeline) {
                        writeFloat(binary, frame.time);
                        writeInt(binary, (frame.int1 << 4) | (static_cast<int>(frame.sequenceMode) & 0xf));
                        writeFloat(binary, frame.value1);
                    }
                }
            }
        }
    }

    writeVarint(binary, static_cast<int>(animation.drawOrder.size()), true);
    for (const auto& frame : animation.drawOrder) {
        writeFloat(binary, frame.time);
        writeVarint(binary, static_cast<int>(frame.offsets.size()), true);
        for (const auto& [name, offset] : frame.offsets) {
            writeVarint(binary, slotIndex(data, name), true);
            writeVarint(binary, offset, true);
        }
    }

    writeVarint(binary, 0, true);

    writeVarint(binary, static_cast<int>(animation.events.size()), true);
    for (const auto& frame : animation.events) {
        const int eIndex = eventIndex(data, frame.str1);
        const EventData& event = data.events.at(eIndex);
        writeFloat(binary, frame.time);
        writeVarint(binary, eIndex, true);
        writeVarint(binary, frame.int1, false);
        writeFloat(binary, frame.value1);
        if (frame.str2 != event.stringValue) writeString(binary, frame.str2);
        else writeString(binary, std::nullopt);
        if (event.audioPath && !event.audioPath->empty()) {
            writeFloat(binary, frame.value2);
            writeFloat(binary, frame.value3);
        }
    }

    if (data.nonessential) writeInt(binary, -1);
}

void collectStrings43(SkeletonData& data) {
    std::set<std::string> strings;
    for (const auto& slot : data.slots)
        if (slot.attachmentName) strings.insert(*slot.attachmentName);

    for (const auto& skin : data.skins) {
        for (const auto& [slotName, slotMap] : skin.attachments) {
            (void)slotName;
            for (const auto& [attachmentName, attachment] : slotMap) {
                strings.insert(attachmentName);
                if (attachment.name != attachmentName) strings.insert(attachment.name);
                if (!attachment.path.empty() && attachment.path != attachment.name) strings.insert(attachment.path);
                if (attachment.type == AttachmentType_Linkedmesh)
                    strings.insert(std::get<LinkedmeshAttachment>(attachment.data).parentMesh);
            }
        }
    }

    for (const auto& animation : data.animations) {
        for (const auto& [slotName, multi] : animation.slots) {
            (void)slotName;
            auto it = multi.find("attachment");
            if (it != multi.end())
                for (const auto& frame : it->second) if (frame.str1) strings.insert(*frame.str1);
        }
        for (const auto& [skinName, skinMap] : animation.attachments) {
            (void)skinName;
            for (const auto& [slotName, slotMap] : skinMap) {
                (void)slotName;
                for (const auto& [attachmentName, multi] : slotMap) {
                    (void)multi;
                    strings.insert(attachmentName);
                }
            }
        }
    }

    data.strings.assign(strings.begin(), strings.end());
}

} // namespace

Binary writeBinaryData(SkeletonData& data) {
    Binary binary;

    writeInt(binary, static_cast<int>((data.hash >> 32) & 0xffffffffu));
    writeInt(binary, static_cast<int>(data.hash & 0xffffffffu));
    writeString(binary, data.version);
    writeFloat(binary, data.x);
    writeFloat(binary, data.y);
    writeFloat(binary, data.width);
    writeFloat(binary, data.height);
    writeFloat(binary, data.referenceScale);
    writeBoolean(binary, data.nonessential);
    if (data.nonessential) {
        writeFloat(binary, data.fps);
        writeString(binary, data.imagesPath);
        writeString(binary, data.audioPath);
    }

    collectStrings43(data);
    writeVarint(binary, static_cast<int>(data.strings.size()), true);
    for (const auto& str : data.strings) writeString(binary, OptStr{str});

    writeVarint(binary, static_cast<int>(data.bones.size()), true);
    for (size_t i = 0; i < data.bones.size(); ++i) {
        const BoneData& bone = data.bones[i];
        writeString(binary, bone.name);
        if (i != 0) writeVarint(binary, boneIndex(data, bone.parent), true);
        writeFloat(binary, bone.rotation);
        writeFloat(binary, bone.x);
        writeFloat(binary, bone.y);
        writeFloat(binary, bone.scaleX);
        writeFloat(binary, bone.scaleY);
        writeFloat(binary, bone.shearX);
        writeFloat(binary, bone.shearY);
        writeByte(binary, static_cast<unsigned char>(bone.inherit));
        writeFloat(binary, bone.length);
        writeBoolean(binary, bone.skinRequired);
        if (data.nonessential) {
            writeColor(binary, bone.color.value_or(Color{0x9b, 0x9b, 0x9b, 0xff}));
            writeString(binary, bone.icon);
            writeFloat(binary, 1.0f);
            writeFloat(binary, 0.0f);
            writeBoolean(binary, bone.visible);
        }
    }

    writeVarint(binary, static_cast<int>(data.slots.size()), true);
    for (const auto& slot : data.slots) {
        writeString(binary, slot.name);
        writeVarint(binary, boneIndex(data, slot.bone), true);
        writeColor(binary, slot.color.value_or(Color{0xff, 0xff, 0xff, 0xff}));
        if (slot.darkColor) {
            const Color& c = *slot.darkColor;
            const uint32_t rgb = (static_cast<uint32_t>(c.r) << 24) |
                                 (static_cast<uint32_t>(c.g) << 16) |
                                 (static_cast<uint32_t>(c.b) << 8);
            writeInt(binary, static_cast<int>(rgb));
        } else {
            writeInt(binary, -1);
        }
        writeStringRef(binary, slot.attachmentName, data);
        writeVarint(binary, static_cast<int>(slot.blendMode), true);
        if (data.nonessential) writeBoolean(binary, slot.visible);
    }

    const auto refs = buildConstraintRefs(data);
    writeVarint(binary, static_cast<int>(refs.size()), true);
    for (const auto& ref : refs) {
        if (ref.type == CONSTRAINT_IK_43) writeIKConstraint43(binary, data.ikConstraints.at(ref.localIndex), data);
        else if (ref.type == CONSTRAINT_TRANSFORM_43) writeTransformConstraint43(binary, data.transformConstraints.at(ref.localIndex), data);
        else if (ref.type == CONSTRAINT_PATH_43) writePathConstraint43(binary, data.pathConstraints.at(ref.localIndex), data);
        else if (ref.type == CONSTRAINT_PHYSICS_43) writePhysicsConstraint43(binary, data.physicsConstraints.at(ref.localIndex), data);
    }

    const Skin* defaultSkin = nullptr;
    int extraSkinCount = 0;
    for (const auto& skin : data.skins) {
        if (skin.name == "default" && !defaultSkin) defaultSkin = &skin;
        else ++extraSkinCount;
    }
    if (defaultSkin) writeSkin43(binary, *defaultSkin, data, refs, true);
    else writeVarint(binary, 0, true);

    writeVarint(binary, extraSkinCount, true);
    for (const auto& skin : data.skins)
        if (&skin != defaultSkin) writeSkin43(binary, skin, data, refs, false);

    writeVarint(binary, static_cast<int>(data.events.size()), true);
    for (const auto& event : data.events) {
        writeString(binary, OptStr{event.name});
        writeVarint(binary, event.intValue, false);
        writeFloat(binary, event.floatValue);
        writeString(binary, event.stringValue);
        writeString(binary, event.audioPath);
        if (event.audioPath && !event.audioPath->empty()) {
            writeFloat(binary, event.volume);
            writeFloat(binary, event.balance);
        }
    }

    writeVarint(binary, static_cast<int>(data.animations.size()), true);
    for (const auto& animation : data.animations) writeAnimation43(binary, animation, data, refs);

    return binary;
}

} // namespace spine43
