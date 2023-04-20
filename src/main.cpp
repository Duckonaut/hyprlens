#include "src/debug/Log.hpp"
#include "src/helpers/Vector2D.hpp"
#include "src/plugins/PluginAPI.hpp"
#define WLR_USE_UNSTABLE

#include "globals.hpp"

#include <src/Compositor.hpp>
#include <src/SharedDefs.hpp>
#include <src/config/ConfigManager.hpp>
#include <src/helpers/Color.hpp>
#include <src/helpers/Workspace.hpp>
#include <src/managers/KeybindManager.hpp>
#include <src/render/OpenGL.hpp>

#include <algorithm>
#include <map>
#include <thread>
#include <unistd.h>
#include <vector>

const CColor s_pluginColor = {0x64 / 255.0f, 0x28 / 255.0f, 0xfc / 255.0f, 1.0f};

const std::string k_textureConfigKey = "plugin:hyprlens:background";
const std::string k_nearestConfigKey = "plugin:hyprlens:nearest";
const std::string k_tiledConfigKey = "plugin:hyprlens:tiled";

inline CFunctionHook* g_pPrivatePreBlurForCurrentMonitor = nullptr;

typedef void (*origPreBlurForCurrentMonitor)(void* thisptr);

inline std::unique_ptr<CTexture> g_pTexture = nullptr;

bool g_bTextureLoaded = false;

void hijackBlurFramebuffer();

void loadTexture();

void hkPreBlurForCurrentMonitor(void* thisptr)
{
    hijackBlurFramebuffer();
}

void hijackBlurFramebuffer()
{
    static SConfigValue* tiled = HyprlandAPI::getConfigValue(PHANDLE, k_tiledConfigKey);
    if (!g_bTextureLoaded) {
        loadTexture();

        if (g_pTexture && g_pTexture->m_iTexID != 0) {
            Debug::log(INFO, " [hyprlens] Loaded texture with ID: %d", g_pTexture->m_iTexID);
        }
        else {
            return;
        }

        g_bTextureLoaded = true;
    }


    if (!(g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBShouldRender && g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBDirty)) {
        return;
    }

    const auto SAVEDRENDERMODIF = g_pHyprOpenGL->m_RenderData.renderModif;
    g_pHyprOpenGL->m_RenderData.renderModif = {}; // fix shit

    // make the fake dmg
    pixman_region32_t fakeDamage;
    pixman_region32_init_rect(&fakeDamage, 0, 0,
                              g_pHyprOpenGL->m_RenderData.pMonitor->vecTransformedSize.x,
                              g_pHyprOpenGL->m_RenderData.pMonitor->vecTransformedSize.y);
    wlr_box wholeMonitor = {
        0, 0, static_cast<int>(g_pHyprOpenGL->m_RenderData.pMonitor->vecTransformedSize.x),
        static_cast<int>(g_pHyprOpenGL->m_RenderData.pMonitor->vecTransformedSize.y)};

    // render onto blurFB
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFB.alloc(
        g_pHyprOpenGL->m_RenderData.pMonitor->vecPixelSize.x,
        g_pHyprOpenGL->m_RenderData.pMonitor->vecPixelSize.y);
    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFB.bind();

    g_pHyprOpenGL->clear(CColor(1.0, 0.0, 0.0, 1.0));

    if (g_pTexture) {
        if (g_pTexture->m_iTexID == 0) {
            Debug::log(ERR, " [hyprlens] Null texture...");
        }
        else {
            if (tiled->intValue) {
                Vector2D tileCount =
                    g_pHyprOpenGL->m_RenderData.pMonitor->vecPixelSize / g_pTexture->m_vSize;

                Debug::log(INFO, " [hyprlens] Tile count: %f, %f", tileCount.x, tileCount.y);

                g_pHyprOpenGL->m_RenderData.primarySurfaceUVTopLeft = Vector2D(0, 0);
                g_pHyprOpenGL->m_RenderData.primarySurfaceUVBottomRight = tileCount;

                g_pHyprOpenGL->renderTexture(*g_pTexture, &wholeMonitor, 1.f, 0, false, true);
            }
            else {
                g_pHyprOpenGL->renderTexture(*g_pTexture, &wholeMonitor, 1.f, 0);
            }
        }
    }

    pixman_region32_fini(&fakeDamage);

    g_pHyprOpenGL->m_RenderData.pCurrentMonData->primaryFB.bind();

    g_pHyprOpenGL->m_RenderData.pCurrentMonData->blurFBDirty = false;

    g_pHyprOpenGL->m_RenderData.renderModif = SAVEDRENDERMODIF;
}

void loadTexture()
{
    static SConfigValue* texturePath = HyprlandAPI::getConfigValue(PHANDLE, k_textureConfigKey);
    static SConfigValue* nearest = HyprlandAPI::getConfigValue(PHANDLE, k_nearestConfigKey);
    static SConfigValue* tiled = HyprlandAPI::getConfigValue(PHANDLE, k_tiledConfigKey);

    if (!texturePath) {
        Debug::log(ERR, " [hyprlens] Failed to get texture config value");
        return;
    }

    if (texturePath->strValue.empty() || texturePath->strValue == STRVAL_EMPTY) {
        Debug::log(INFO, " [hyprlens] Texture path is empty, clearing texture");
        return;
    }

    if (g_pTexture) {
        g_pTexture->destroyTexture();
    }

    g_pTexture = std::make_unique<CTexture>();

    g_pTexture->allocate();

    std::string path = texturePath->strValue;

    if (!std::filesystem::exists(path)) {
        Debug::log(ERR, " [hyprlens] Texture path does not exist: %s", path.c_str());
        return; // the texture will be empty, oh well. We'll clear with a solid color anyways.
    }

    Debug::log(INFO, " [hyprlens] Loading texture from path: %s", path.c_str());

    // create a new one with cairo
    const auto CAIROSURFACE = cairo_image_surface_create_from_png(path.c_str());
    const auto CAIRO = cairo_create(CAIROSURFACE);

    const Vector2D textureSize = {
        static_cast<double>(cairo_image_surface_get_width(CAIROSURFACE)),
        static_cast<double>(cairo_image_surface_get_height(CAIROSURFACE))};

    g_pTexture->m_vSize = textureSize;

    Debug::log(INFO, " [hyprlens] Texture size: %f, %f", textureSize.x, textureSize.y);

    const auto DATA = cairo_image_surface_get_data(CAIROSURFACE);
    glBindTexture(GL_TEXTURE_2D, g_pTexture->m_iTexID);

    Debug::log(INFO, " [hyprlens] Nearest: %d", nearest->intValue);
    if (nearest->intValue) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    }
    else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    Debug::log(INFO, " [hyprlens] Tiled: %d", tiled->intValue);
    if (tiled->intValue) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
#ifndef GLES2
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureSize.x, textureSize.y, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, DATA);

    cairo_surface_destroy(CAIROSURFACE);
    cairo_destroy(CAIRO);
}

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    HyprlandAPI::addConfigValue(PHANDLE, k_textureConfigKey,
                                SConfigValue{.strValue = STRVAL_EMPTY});

    HyprlandAPI::addConfigValue(PHANDLE, k_nearestConfigKey, SConfigValue{.intValue = 0});

    HyprlandAPI::addConfigValue(PHANDLE, k_tiledConfigKey, SConfigValue{.intValue = 0});

    const auto reloadConfigCandidates =
        HyprlandAPI::findFunctionsByName(PHANDLE, "loadConfigLoadVars");

    // REAALLY hacky way to force the config to reload
    for (const auto& candidate : reloadConfigCandidates) {
        ((void (*)(void*))candidate.address)(g_pConfigManager.get());
    }

    const auto blurCandidates =
        HyprlandAPI::findFunctionsByName(PHANDLE, "preBlurForCurrentMonitor");

    if (blurCandidates.size() != 1) {
        Debug::log(ERR, " [hyprlens] Found %d preBlurForCurrentMonitor candidates, expected 1",
                   blurCandidates.size());
    }
    else {
        g_pPrivatePreBlurForCurrentMonitor = HyprlandAPI::createFunctionHook(
            PHANDLE, blurCandidates[0].address,
            (void*)&hkPreBlurForCurrentMonitor);

        g_pPrivatePreBlurForCurrentMonitor->hook();

        Debug::log(INFO, " [hyprlens] Hooked preBlurForCurrentMonitor");
    }

    HyprlandAPI::addNotification(PHANDLE, "[hyprlens] Initialized successfully!", s_pluginColor,
                                 5000);

    return {"hyprlens",
            "Customizable background for transparent windows,"
            " separate from the desktop background.",
            "Duckonaut", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    HyprlandAPI::addNotification(PHANDLE, "[hyprlens] Unloaded successfully!", s_pluginColor,
                                 5000);
}
