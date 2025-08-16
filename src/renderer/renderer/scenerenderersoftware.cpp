#include <renderer/scenerenderersoftware.h>

#include <stdint.h>
#include <algorithm>
#include <vector>
#include <cassert>
#include <iostream>
#include <array>
#include <execution>
#include <ranges>
#include <utility>

#include "utils.h"

namespace Renderer
{
    static constexpr uint32_t InterpolantsSize = 13;

    struct InterpolationPoint
    {
        float x;
        float y;
        float c;
    };

    struct Interpolant
    {
        Interpolant() {}

        Interpolant(const InterpolationPoint& a, const InterpolationPoint& b, const InterpolationPoint& c)
        {
            float dx = (b.x - c.x) * (a.y - c.y) - (a.x - c.x) * (b.y - c.y);
            float dy = -dx;

            stepX = ((b.c - c.c) * (a.y - c.y) - (a.c - c.c) * (b.y - c.y)) / dx;
            stepY = ((b.c - c.c) * (a.x - c.x) - (a.c - c.c) * (b.x - c.x)) / dy;

            minC = a.c;
            midC = b.c;
        }

        float stepX = 0.0f;
        float stepY = 0.0f;

        float minC = 0.0f;
        float midC = 0.0f;

        float CalculateC(float dx, float dy, bool isMin)
        {
            return (isMin ? minC : midC) + dy * stepY + dx * stepX;
        }
    };

    struct LightS
    {
        Light light;
        Vec position_view;
    };

    struct VertexS
    {
        Vertex v;
        Vec pos_view;

        // todo.pavelza: need to get rid of these, just needed for easier interpolation
        float red = 0.0f;
        float green = 0.0f;
        float blue = 0.0f;
    };

    struct Edge
    {
        Edge()
        {
        }

        Edge(Vec b, Vec e, bool isMin = true) : begin(b), isBeginMin(isMin), pixelX(0)
        {
            pixelYBegin = static_cast<int32_t>(ceil(b.y));
            pixelYEnd = static_cast<int32_t>(ceil(e.y));

            float distanceX = e.x - b.x;
            float distanceY = e.y - b.y;
            stepX = distanceY > 0.0f ? distanceX / distanceY : 0.0f;
        }

        void CalculateC(std::array<Interpolant, InterpolantsSize>& interpolants, int32_t y)
        {
            pixelX = static_cast<int32_t>(ceil(begin.x + (y - begin.y) * stepX));
            for (size_t i = 0; i < InterpolantsSize; i++)
            {
                currentC[i] = interpolants[i].CalculateC(pixelX - begin.x, y - begin.y, isBeginMin);
            }
        }

        void CalculateCForZOnly(std::array<Interpolant, InterpolantsSize>& interpolants, int32_t y)
        {
            pixelX = static_cast<int32_t>(ceil(begin.x + (y - begin.y) * stepX));
            currentC[12] = interpolants[12].CalculateC(pixelX - begin.x, y - begin.y, isBeginMin);
        }

        int32_t pixelYBegin;
        int32_t pixelYEnd;
        int32_t pixelX;

        float stepX;
        bool isBeginMin;
        Vec begin;

        // todo.pavelza: C along the edge. Need to rename the C into interpolated value or something like this.
        std::array<float, InterpolantsSize> currentC;
    };

    struct Triangle
    {
        uint32_t texture = 0;

        std::array<Interpolant, InterpolantsSize> interpolants;
        std::vector<VertexS> vertices;

        Edge minMax;
        Edge minMiddle;
        Edge middleMax;
    };

    struct SceneRendererSoftwareContext
    {
        SceneRendererSoftwareContext(const Scene& scene): scene(scene) {}

        const Scene& scene;

        size_t OutputWidth;
        size_t OutputHeight;

        std::vector<uint32_t> BackBuffer;
        std::vector<float> ZBuffer;
        std::vector<Texture> Textures;
        LightS light;

        std::vector<std::array<float, InterpolantsSize>> GBuffer;
        std::vector<uint32_t> TBuffer;

        float Lerp(float begin, float end, float lerpAmount)
        {
            return begin + (end - begin) * lerpAmount;
        }

        void FillZBuffer(Triangle& tr)
        {
            for (int32_t y = tr.minMax.pixelYBegin; y < tr.minMax.pixelYEnd; y++)
            {
                Edge* left = &tr.minMax;
                Edge* right = y >= tr.middleMax.pixelYBegin ? &tr.middleMax : &tr.minMiddle;

                left->CalculateCForZOnly(tr.interpolants, y);
                right->CalculateCForZOnly(tr.interpolants, y);

                if (left->pixelX > right->pixelX)
                {
                    Edge* temp = left;
                    left = right;
                    right = temp;
                }

                for (int32_t x = left->pixelX; x < right->pixelX; x++)
                {
                    float percent = static_cast<float>(x - left->pixelX) / static_cast<float>(right->pixelX - left->pixelX);
                    float z = Lerp(left->currentC[12], right->currentC[12], percent);
                    if (z < ZBuffer[y * OutputWidth + x])
                    {
                        ZBuffer[y * OutputWidth + x] = z;
                    }
                }
            }
        }

        void FillGBuffer(Triangle& tr)
        {
            for (int32_t y = tr.minMax.pixelYBegin; y < tr.minMax.pixelYEnd; y++)
            {
                Edge* left = &tr.minMax;
                Edge* right = y >= tr.middleMax.pixelYBegin ? &tr.middleMax : &tr.minMiddle;

                left->CalculateC(tr.interpolants, y);
                right->CalculateC(tr.interpolants, y);

                if (left->pixelX > right->pixelX)
                {
                    Edge* temp = left;
                    left = right;
                    right = temp;
                }

                for (int32_t x = left->pixelX; x < right->pixelX; x++)
                {
                    float percent = static_cast<float>(x - left->pixelX) / static_cast<float>(right->pixelX - left->pixelX);
                    float z = Lerp(left->currentC[12], right->currentC[12], percent);
                    if (z == ZBuffer[y * OutputWidth + x])
                    {
                        for (uint32_t i = 0; i < InterpolantsSize - 1; i++)
                        {
                            if (GBuffer[y * OutputWidth + x][i] != 0.0f)
                            {
                                // todo.pavelza: this might trigger if there is same z for two triangles, should we do something about it?
                                // assert(false);
                            }
                            GBuffer[y * OutputWidth + x][i] = Lerp(left->currentC[i], right->currentC[i], percent);
                        }
                        TBuffer[y * OutputWidth + x] = tr.texture;
                        GBuffer[y * OutputWidth + x][12] = z;
                    }
                }
            }
        }

        void ShadePixels()
        {
            auto r = std::ranges::iota_view<int32_t, int32_t>{ 0, static_cast<int32_t>(OutputWidth * OutputHeight) };
            std::for_each(std::execution::par, r.begin(), r.end(), [this](int32_t i) {
                std::array<float, InterpolantsSize>& interpolants_raw = GBuffer[i];

                if (interpolants_raw[12] != 0.0f)
                {
                    float tintRed = interpolants_raw[0] / interpolants_raw[11];
                    float tintGreen = interpolants_raw[1] / interpolants_raw[11];
                    float tintBlue = interpolants_raw[2] / interpolants_raw[11];

                    float texX = interpolants_raw[3] / interpolants_raw[11];
                    float texY = interpolants_raw[4] / interpolants_raw[11];

                    float normalX = interpolants_raw[5] / interpolants_raw[11];
                    float normalY = interpolants_raw[6] / interpolants_raw[11];
                    float normalZ = interpolants_raw[7] / interpolants_raw[11];

                    float viewX = interpolants_raw[8] / interpolants_raw[11];
                    float viewY = interpolants_raw[9] / interpolants_raw[11];
                    float viewZ = interpolants_raw[10] / interpolants_raw[11];

                    Vec pos_view{ viewX, viewY, viewZ, 1.0f };
                    Vec normal_vec = normalize({ normalX, normalY, normalZ, 0.0f });
                    Vec light_vec = normalize(light.position_view - pos_view);

                    Vec diffuse = light.light.color.GetVec() * static_cast<float>(std::max<float>(dot(normal_vec, light_vec), 0.0f));
                    Vec ambient = light.light.color.GetVec() * light.light.ambientStrength;

                    float specAmount = static_cast<float>(std::max<float>(dot(normalize(pos_view), reflect(normal_vec, light_vec * -1.0f)), 0.0f));
                    Vec specular = light.light.color.GetVec() * pow(specAmount, light.light.specularShininess) * light.light.specularStrength;

                    Vec final_color{ tintRed, tintGreen, tintBlue, 1.0f };
                    if (Textures.size() > 0)
                    {
                        uint32_t materialId = TBuffer[i];
                        assert(Textures[materialId].GetHeight() > 0 && Textures[materialId].GetWidth() > 0);

                        // From 0 to TextureWidth - 1 (TextureWidth pixels in total)
                        size_t textureX = static_cast<size_t>(texX * (Textures[materialId].GetWidth() - 1));
                        // From 0 to TextureHeight - 1 (TextureHeight pixels in total)
                        size_t textureY = static_cast<size_t>(texY * (Textures[materialId].GetHeight() - 1));

                        textureY = (Textures[materialId].GetHeight() - 1) - textureY; // invert texture coords

                        assert(textureY < Textures[materialId].GetHeight() && textureX < Textures[materialId].GetWidth());
                        size_t texelBase = textureY * Textures[materialId].GetWidth() + textureX;

                        final_color = Textures[materialId].GetColor(texelBase).GetVec();
                    }

                    final_color = (diffuse + ambient + specular) * final_color;
                    final_color.x = std::clamp(final_color.x, 0.0f, 1.0f);
                    final_color.y = std::clamp(final_color.y, 0.0f, 1.0f);
                    final_color.z = std::clamp(final_color.z, 0.0f, 1.0f);
                    final_color.w = 1.0f; // fix the alpha being affected by light, noticable only in tests, because in application we correct alpha manually when copying to backbuffer

                    assert(OutputWidth > 0);
                    assert(OutputHeight > 0);
                    size_t coefficient1 = OutputWidth * OutputHeight - OutputWidth;
                    size_t coefficient2 = 2 * OutputWidth;
                    BackBuffer[coefficient1 - (i / OutputWidth) * coefficient2 + i] = Color(final_color).rgba;
                }
            });
        }

        VertexS Lerp(const VertexS& begin, const VertexS& end, float lerpAmount)
        {
            VertexS result;
            result.v.position.x = Lerp(begin.v.position.x, end.v.position.x, lerpAmount);
            result.v.position.y = Lerp(begin.v.position.y, end.v.position.y, lerpAmount);
            result.v.position.z = Lerp(begin.v.position.z, end.v.position.z, lerpAmount);
            result.v.position.w = Lerp(begin.v.position.w, end.v.position.w, lerpAmount);
            result.pos_view.x = Lerp(begin.pos_view.x, end.pos_view.x, lerpAmount);
            result.pos_view.y = Lerp(begin.pos_view.y, end.pos_view.y, lerpAmount);
            result.pos_view.z = Lerp(begin.pos_view.z, end.pos_view.z, lerpAmount);
            result.pos_view.w = Lerp(begin.pos_view.w, end.pos_view.w, lerpAmount);
            result.v.textureCoord.x = Lerp(begin.v.textureCoord.x, end.v.textureCoord.x, lerpAmount);
            result.v.textureCoord.y = Lerp(begin.v.textureCoord.y, end.v.textureCoord.y, lerpAmount);
            result.red = Lerp(begin.red, end.red, lerpAmount);
            result.green = Lerp(begin.green, end.green, lerpAmount);
            result.blue = Lerp(begin.blue, end.blue, lerpAmount);
            result.v.normal.x = Lerp(begin.v.normal.x, end.v.normal.x, lerpAmount);
            result.v.normal.y = Lerp(begin.v.normal.y, end.v.normal.y, lerpAmount);
            result.v.normal.z = Lerp(begin.v.normal.z, end.v.normal.z, lerpAmount);
            result.v.normal.w = Lerp(begin.v.normal.w, end.v.normal.w, lerpAmount);
            result.v.materialId = begin.v.materialId;
            return result;
        }

        Interpolant GetInterpolant(std::vector<VertexS>& vertices, const std::function<float(const VertexS& v)>& c)
        {
            return Interpolant(
                { vertices[0].v.position.x, vertices[0].v.position.y, c(vertices[0]) / vertices[0].v.position.w },
                { vertices[1].v.position.x, vertices[1].v.position.y, c(vertices[1]) / vertices[1].v.position.w },
                { vertices[2].v.position.x, vertices[2].v.position.y, c(vertices[2]) / vertices[2].v.position.w }
            );
        }

        void AddRawTriangle(Triangle& tr)
        {
            assert(tr.vertices.size() == 3);

            for (VertexS& v : tr.vertices)
            {
                v.v.position.x /= v.v.position.w;
                v.v.position.y /= v.v.position.w;
                v.v.position.z /= v.v.position.w;
            }

            std::sort(std::begin(tr.vertices), std::end(tr.vertices), [](const VertexS& lhs, const VertexS& rhs) { return lhs.v.position.y < rhs.v.position.y; });

            for (VertexS& v : tr.vertices)
            {
                // todo.pavelza: Clipping might result in some vertices being slightly outside of -1 to 1 range, so we clamp. Will need to think how to avoid this.
                v.v.position.x = std::clamp(v.v.position.x, -1.0f, 1.0f);
                v.v.position.y = std::clamp(v.v.position.y, -1.0f, 1.0f);
                v.v.position.z = std::clamp(v.v.position.z, -1.0f, 1.0f);

                v.v.position.x = (OutputWidth - 1) * ((v.v.position.x + 1) / 2.0f);
                v.v.position.y = (OutputHeight - 1) * ((v.v.position.y + 1) / 2.0f);
            }

            tr.interpolants[0] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.red; });
            tr.interpolants[1] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.green; });
            tr.interpolants[2] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.blue; });

            tr.interpolants[3] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.v.textureCoord.x; });
            tr.interpolants[4] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.v.textureCoord.y; });

            tr.interpolants[5] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.v.normal.x; });
            tr.interpolants[6] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.v.normal.y; });
            tr.interpolants[7] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.v.normal.z; });

            tr.interpolants[8] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.pos_view.x; });
            tr.interpolants[9] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.pos_view.y; });
            tr.interpolants[10] = GetInterpolant(tr.vertices, [](const VertexS& v) { return v.pos_view.z; });

            tr.interpolants[11] = GetInterpolant(tr.vertices, [](const VertexS& v) { return 1.0f; });

            // No division by w, because it is already divided by w.
            tr.interpolants[12] = Interpolant(
                { tr.vertices[0].v.position.x, tr.vertices[0].v.position.y, tr.vertices[0].v.position.z },
                { tr.vertices[1].v.position.x, tr.vertices[1].v.position.y, tr.vertices[1].v.position.z },
                { tr.vertices[2].v.position.x, tr.vertices[2].v.position.y, tr.vertices[2].v.position.z });

            tr.texture = tr.vertices[0].v.materialId;

            tr.minMax = Edge(tr.vertices[0].v.position, tr.vertices[2].v.position, true);
            tr.minMiddle = Edge(tr.vertices[0].v.position, tr.vertices[1].v.position, true);
            tr.middleMax = Edge(tr.vertices[1].v.position, tr.vertices[2].v.position, false);
        }

        static bool IsVertexInside(const VertexS& point, int32_t axis, int32_t plane)
        {
            return point.v.position.Get(axis) * plane <= point.v.position.w;
        }

        // todo.pavelza: There is an issue somewhere - we render black 1px line on the border sometimes when the triangle is outside of frustum (compared to dx12 renderer).
        // And after introducing the backface culling we can sometimes see pixels from some triangles on the back in the first left 1px line.
        // Looks like front triangle is clipped not precisely by frustum.
        void ClipTrianglePlane(std::vector<VertexS>& vertices, int32_t axis, int32_t plane)
        {
            std::vector<VertexS> result;
            size_t previousElement = vertices.size() - 1;

            for (size_t currentElement = 0; currentElement < vertices.size(); currentElement++)
            {
                bool isPreviousInside = IsVertexInside(vertices[previousElement], axis, plane);
                bool isCurrentInside = IsVertexInside(vertices[currentElement], axis, plane);

                if (isPreviousInside != isCurrentInside)
                {
                    float k = (vertices[previousElement].v.position.w - vertices[previousElement].v.position.Get(axis) * plane);
                    float lerpAmount = k / (k - vertices[currentElement].v.position.w + vertices[currentElement].v.position.Get(axis) * plane);
                    result.push_back(Lerp(vertices[previousElement], vertices[currentElement], lerpAmount));
                }

                if (isCurrentInside)
                {
                    result.push_back(vertices[currentElement]);
                }

                previousElement = currentElement;
            }

            swap(vertices, result);
        }

        bool ClipTriangleAxis(std::vector<VertexS>& vertices, int32_t axis)
        {
            ClipTrianglePlane(vertices, axis, 1);

            if (vertices.size() == 0)
            {
                return false;
            }

            ClipTrianglePlane(vertices, axis, -1);

            return vertices.size() != 0;
        }

        void AddTriangle(Triangle& tr, const Matrix& transform, std::vector<Triangle>& trianglesCache)
        {
            for (VertexS& v : tr.vertices)
            {
                v.v.position = PerspectiveTransform(scene.camera, static_cast<float>(OutputWidth), static_cast<float>(OutputHeight)) * transform * v.v.position;
                v.pos_view = transform * v.pos_view;
                // This is possible because we do not do non-uniform scale in transform. If we are about to do non-uniform scale, we should calculate the normal matrix.
                v.v.normal = transform * v.v.normal;
            }

            // Backface culling produces very rough results in view space (maybe need to figure out why some day). So we do it in clip space. And it seems to be the right (identical to hardware) way.
            // Doing clipspace culling before we split triangles that penetrate camera frustum gives us additional ~10ms gain for a frame in reference scene.
            // Front is counter clockwise.
            Vec v0 { tr.vertices[0].v.position.x / tr.vertices[0].v.position.w, tr.vertices[0].v.position.y / tr.vertices[0].v.position.w, tr.vertices[0].v.position.z / tr.vertices[0].v.position.w, 1.0f };
            Vec v1 { tr.vertices[1].v.position.x / tr.vertices[1].v.position.w, tr.vertices[1].v.position.y / tr.vertices[1].v.position.w, tr.vertices[1].v.position.z / tr.vertices[1].v.position.w, 1.0f };
            Vec v2 { tr.vertices[2].v.position.x / tr.vertices[2].v.position.w, tr.vertices[2].v.position.y / tr.vertices[2].v.position.w, tr.vertices[2].v.position.z / tr.vertices[2].v.position.w, 1.0f };
            if (cross(v2 - v0, v1 - v0).z > 0)
            {
                return;
            }

            // We must check that all triangle lies on outside of one of the planes,
            // since if we check that some vertices lie on the outside of one plane and others on outside of the other,
            // then part of the triangle might still be visible.
            std::array<bool, 6> planesToOutsideness { false };
            for (const VertexS& v : tr.vertices)
            {
                planesToOutsideness[0] |= IsVertexInside(v, 0, 1);
                planesToOutsideness[1] |= IsVertexInside(v, 1, 1);
                planesToOutsideness[2] |= IsVertexInside(v, 2, 1);
                planesToOutsideness[3] |= IsVertexInside(v, 0, -1);
                planesToOutsideness[4] |= IsVertexInside(v, 1, -1);
                planesToOutsideness[5] |= IsVertexInside(v, 2, -1);
            }

            // This check doesn't give us a big performance boost in a general case,
            // but it doesn't seem to hit performance in general case too much to remove it either.
            if (std::any_of(planesToOutsideness.begin(), planesToOutsideness.end(), [](bool val) { return !val; }))
            {
                return;
            }

            if (std::all_of(tr.vertices.begin(), tr.vertices.end(), [](const VertexS& v){ return IsVertexInside(v, 0, 1) && IsVertexInside(v, 1, 1) && IsVertexInside(v, 2, 1) && IsVertexInside(v, 0, -1) && IsVertexInside(v, 1, -1) && IsVertexInside(v, 2, -1); }))
            {
                AddRawTriangle(tr);
                trianglesCache.push_back(std::move(tr));
                return;
            }

            std::vector<VertexS> vertices = std::move(tr.vertices);
            if (ClipTriangleAxis(vertices, 0) && ClipTriangleAxis(vertices, 1) && ClipTriangleAxis(vertices, 2))
            {
                assert(vertices.size() >= 3);

                for (size_t i = 2; i < vertices.size(); i++)
                {
                    Triangle newTr;
                    newTr.vertices.resize(3);

                    newTr.vertices[0] = vertices[0];
                    newTr.vertices[1] = vertices[i - 1];
                    newTr.vertices[2] = vertices[i];

                    AddRawTriangle(newTr);
                    trianglesCache.push_back(std::move(newTr));
                }
            }
        }

        void GetTriangleFromModel(uint32_t index, const Model& model, Triangle& triangle)
        {
            triangle.vertices.resize(3);

            const Vertex& a = model.vertices[model.indices[index * 3 + 0]];
            const Vertex& b = model.vertices[model.indices[index * 3 + 1]];
            const Vertex& c = model.vertices[model.indices[index * 3 + 2]];

            triangle.vertices[0] = VertexS{
                a,
                a.position,
                a.color.GetVec().x,
                a.color.GetVec().y,
                a.color.GetVec().z
            };

            triangle.vertices[1] = VertexS{
                b,
                b.position,
                b.color.GetVec().x,
                b.color.GetVec().y,
                b.color.GetVec().z
            };

            triangle.vertices[2] = VertexS{
                c,
                c.position,
                c.color.GetVec().x,
                c.color.GetVec().y,
                c.color.GetVec().z
            };
        }
    };

    bool SceneRendererSoftware::Render(const Scene& scene, Texture& texture)
    {
        if (texture.GetWidth() == 0 || texture.GetHeight() == 0)
        {
            return false;
        }

        if (context == nullptr || context->scene.name != scene.name)
        {
            context = std::make_shared<SceneRendererSoftwareContext>(scene);
        }

        context->OutputWidth = texture.GetWidth();
        context->OutputHeight = texture.GetHeight();

        PERF_START("Clean buffers");
        context->BackBuffer.resize(context->OutputWidth * context->OutputHeight);
        context->ZBuffer.resize(context->OutputWidth * context->OutputHeight);
        context->GBuffer.resize(context->OutputWidth * context->OutputHeight);
        context->TBuffer.resize(context->OutputWidth * context->OutputHeight);

        std::fill(context->BackBuffer.begin(), context->BackBuffer.end(), Color::Black.rgba);
        std::fill(context->ZBuffer.begin(), context->ZBuffer.end(), 2.0f);
        std::fill(context->TBuffer.begin(), context->TBuffer.end(), 0u);
        PERF_END();

        PERF_START("Clean G buffers");
        auto r = std::ranges::iota_view<size_t, size_t>{ 0, context->GBuffer.size() };
        std::for_each(std::execution::par, r.begin(), r.end(), [this](size_t i) { std::fill(context->GBuffer[i].begin(), context->GBuffer[i].end(), 0.0f); });
        PERF_END();

        PERF_START("Light transform");
        // calculate light's position in view space
        context->light.position_view = ViewTransform(scene.camera) * scene.light.position;
        context->light.light = scene.light;
        PERF_END();

        PERF_START("Materials");
        if (context->Textures.size() == 0)
        {
            context->Textures.resize(scene.models[0].materials.size());
            for (size_t i = 0; i < context->Textures.size(); i++)
            {
                Load(scene.models[0].materials[i].textureName, context->Textures[i]);
            }
        }
        PERF_END();

        PERF_START("Triangle cache");
        const Model& model = scene.models[0];

        static std::vector<Triangle> trianglesCache;
        trianglesCache.reserve(model.indices.size() / 3 * 2);
        trianglesCache.clear();
        PERF_END();

        PERF_START("Add triangles");
        for (uint32_t i = 0; i < model.indices.size() / 3; i++)
        {
            Triangle triangle;
            context->GetTriangleFromModel(i, model, triangle);
            context->AddTriangle(triangle, ViewTransform(scene.camera), trianglesCache);
        }
        PERF_END();

        PERF_START("ZBuffer");
        for (Triangle& tr : trianglesCache)
        {
            context->FillZBuffer(tr);
        }
        PERF_END();

        PERF_START("GBuffer");
        std::for_each(std::execution::par, trianglesCache.begin(), trianglesCache.end(), [this](Triangle& tr) { context->FillGBuffer(tr); });
        PERF_END();

        PERF_START("Shading");
        context->ShadePixels();
        PERF_END();

        PERF_START("Buffer to texture");
        for (size_t i = 0; i < context->BackBuffer.size(); i++)
        {
            texture.SetColor(i, Color(context->BackBuffer[i]));
        }
        PERF_END();

        return true;
    }
}