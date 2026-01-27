#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/BoomScrollLayer.hpp>

using namespace geode::prelude;

static CCPoint g_currentVelocity = {0, 0};

static bool g_didStartSchedule = false;
static bool g_fakeScroll = false;
static bool g_isEditor = false;
static bool g_isEditorBlocked = false;
static bool g_enabled = true;

static float g_targetZoom = 1.f;
static float g_editorSensivity = 1.f;
static float g_editorMultiplier = 1.f;
static float g_zoomSensivity = 1.f;
static float g_scrollSensivity = 1.f;
static float g_scrollMultiplier = 1.f;

void updateSettings() {
    g_enabled = Mod::get()->getSettingValue<bool>("enable");
    g_editorSensivity = Mod::get()->getSettingValue<float>("editor-sensivity");
    g_editorMultiplier = Mod::get()->getSettingValue<float>("editor-multiplier");
    g_zoomSensivity = Mod::get()->getSettingValue<float>("zoom-sensivity");
    g_scrollSensivity = Mod::get()->getSettingValue<float>("scroll-sensivity");
    g_scrollMultiplier = Mod::get()->getSettingValue<float>("scroll-multiplier");
}

void updateVelocity(float dt) {
    g_currentVelocity *= pow(0.001f, dt * ((g_isEditor && !g_isEditorBlocked) ? (12.f * g_editorSensivity) : (3.f * g_scrollSensivity)));
        
    if (abs(g_currentVelocity.x) < 0.01f) {
        g_currentVelocity.x = 0.f;
    }

    if (abs(g_currentVelocity.y) < 0.01f) {
        g_currentVelocity.y = 0.f;
    }
}

class $modify(ProEditorUI, EditorUI) {

    struct Fields {
        CCTouch* m_touch1 = nullptr;
        CCTouch* m_touch2 = nullptr;

        CCPoint m_touchStart1;
        CCPoint m_touchStart2;

        float m_zoomStart = 1.f;

        bool m_centerZoom = false;

        ~Fields() {
            g_isEditor = false;
        }
    };

    void proUpdateZoom(float zoom, const CCPoint& center) {
        auto objectLayer = m_editorLayer->m_objectLayer;
        auto prevScale = m_editorLayer->getParent()->getScale();

        m_editorLayer->getParent()->setScale(1.f);

        auto pos = objectLayer->convertToNodeSpace(m_editorLayer->convertToNodeSpace(center));

        objectLayer->setScale(zoom);
        objectLayer->setPosition(
            center - objectLayer->convertToWorldSpace(pos) + objectLayer->getPosition()
        );
        
        constrainGameLayerPosition(-100.f, -100.f);
        
        m_editorLayer->getParent()->setScale(prevScale);

        updateSlider();
    }

    bool init(LevelEditorLayer* p0) {
        if (!EditorUI::init(p0)) {
            return false;
        }

        if (!g_enabled) {
            return true;
        }

        m_fields.self();

        g_isEditor = true;
        g_currentVelocity = CCPoint{0, 0};

        Loader::get()->queueInMainThread([self = Ref(this)] {
            g_targetZoom = self->m_editorLayer->m_objectLayer->getScale();
            self->schedule(schedule_selector(ProEditorUI::updateScroll), 0.f);
        });

        return true;
    }

    void updateScroll(float dt) {
        if (g_isEditorBlocked) {
            return;
        }
        if (m_editorLayer->m_playbackMode == PlaybackMode::Playing) {
            return updateVelocity(dt);
        }

        if (g_currentVelocity != ccp(0, 0)) {
            m_editorLayer->m_objectLayer->setPosition(
                m_editorLayer->m_objectLayer->getPosition() - g_currentVelocity * dt
            );

            constrainGameLayerPosition(-100.f, -100.f);
            updateSlider();
        }

        updateVelocity(dt);

        auto scale = m_editorLayer->m_objectLayer->getScale();

        #ifdef GEODE_IS_WINDOWS

        auto f = m_fields.self();

        if (abs(scale - g_targetZoom) > 0.01f) {
            proUpdateZoom(
                scale + (g_targetZoom - scale) * 0.2f,
                f->m_centerZoom ? CCPoint(getContentSize() / 2.f) : getMousePos()
            );
        } else {
            f->m_centerZoom = false;
        }

        #else

        if (abs(scale - g_targetZoom) > 0.01f) {
            proUpdateZoom(
                scale + (g_targetZoom - scale) * 0.2f,
                CCPoint(getContentSize() / 2.f)
            );
        }

        #endif
    }

    #ifndef GEODE_IS_ANDROID

    void zoomIn(CCObject* sender) {
        if (!g_enabled) {
            return EditorUI::zoomIn(sender);
        }

        g_targetZoom += Mod::get()->getSettingValue<float>("zoom-step");

        if (g_targetZoom > 4.f) {
            g_targetZoom = 4.f;
        }

        m_fields->m_centerZoom = true;
    }

    void zoomOut(CCObject* sender) {
        if (!g_enabled) {
            return EditorUI::zoomOut(sender);
        }
        
        g_targetZoom -= Mod::get()->getSettingValue<float>("zoom-step");

        if (g_targetZoom < 0.1f) {
            g_targetZoom = 0.1f;
        }

        m_fields->m_centerZoom = true;
    }

    #else

    void zoomGameLayer(bool in) {
        g_targetZoom += Mod::get()->getSettingValue<float>("zoom-step") * (in ? 1 : -1);
        g_targetZoom = clamp(g_targetZoom, 0.1f, 4.f);
    }

    #endif

    void scrollWheel(float y, float x) {
        if (!g_enabled) {
            EditorUI::scrollWheel(y, x);
        }
    }

    #ifdef GEODE_IS_MOBILE

    bool ccTouchBegan(CCTouch* p0, CCEvent* p1) {
        if (
            !g_enabled
            || !Mod::get()->getSettingValue<bool>("pinch-to-zoom")
            || m_editorLayer->m_playbackMode == PlaybackMode::Playing
        ) {
            return EditorUI::ccTouchBegan(p0, p1);
        }

        auto f = m_fields.self();

        if (f->m_touch1 && f->m_touch2) {
            return true;
        }
        
        if (!f->m_touch1) {
            f->m_touch1 = p0;
        } else if (!f->m_touch2) {
            f->m_touch2 = p0;

            f->m_touchStart1 = f->m_touch1->getLocation();
            f->m_touchStart2 = f->m_touch2->getLocation();

            f->m_zoomStart = g_targetZoom;
        }

        return EditorUI::ccTouchBegan(p0, p1);
    }

    void ccTouchMoved(CCTouch* p0, CCEvent* p1) {
        auto f = m_fields.self();
        auto prevSwipe = m_swipeEnabled;

        if (f->m_touch1 && f->m_touch2) {
            m_swipeEnabled = false;
        }

        EditorUI::ccTouchMoved(p0, p1);

        m_swipeEnabled = prevSwipe;
                
        if (!f->m_touch1 || !f->m_touch2) {
            return;
        }

        auto prevDist = ccpDistance(
            f->m_touch1->getPreviousLocation(),
            f->m_touch2->getPreviousLocation()
        );

        auto currDist = ccpDistance(
            f->m_touch1->getLocation(),
            f->m_touch2->getLocation()
        );

        if (prevDist == 0.f) {
            return;
        }

        g_targetZoom *= currDist / prevDist;
        g_targetZoom = clamp(g_targetZoom, 0.1f, 4.f);

        proUpdateZoom(
            g_targetZoom,
            (f->m_touch1->getLocation() + f->m_touch2->getLocation()) / 2.f
        );
    }

    void ccTouchEnded(CCTouch* p0, CCEvent* p1) {
        EditorUI::ccTouchEnded(p0, p1);

        auto f = m_fields.self();

        if (p0 == f->m_touch1 || p0 == f->m_touch2) {
            f->m_touch1 = nullptr;
            f->m_touch2 = nullptr;
        }
    }

    #endif

};

class $modify(ProCCMouseDispatcher, CCMouseDispatcher) {

    void updateScroll(float dt) {
        if (LevelEditorLayer::get()) {
            auto scene = CCScene::get();

            g_isEditorBlocked = scene->getChildrenCount() > 0
                && scene->getChildByType<FLAlertLayer>(-1);

            if (!g_isEditorBlocked) {
                return;
            }
        }

        g_fakeScroll = true;

        dispatchScrollMSG(-(g_currentVelocity.y + g_currentVelocity.x) * dt * 1.4f, 0.f);

        g_fakeScroll = false;

        updateVelocity(dt);
    }

    bool dispatchScrollMSG(float y, float x) {
        if (!g_enabled || g_fakeScroll) {
            return CCMouseDispatcher::dispatchScrollMSG(y, x);
        }

        if (!g_didStartSchedule) {
            g_didStartSchedule = true;

            CCScheduler::get()->scheduleSelector(
                schedule_selector(ProCCMouseDispatcher::updateScroll),
                this,
                0.f,
                kCCRepeatForever,
                0.f,
                false
            );
        }

        return false;
    }

};

class $modify(ProBoomScrollLayer, BoomScrollLayer) {

    struct Fields {
        bool m_mustWait = false;
    };

    void updateScroll(float dt) {
        if (g_isEditor || m_touch) {
            return;
        }
        
        auto f = m_fields.self();

        if (
            !f->m_mustWait
            && (g_currentVelocity.x > 400.f || g_currentVelocity.x < -400.f)
        ) {
            quickUpdate();
            moveToPage(m_page + (g_currentVelocity.x > 400.f ? 1 : -1));

            f->m_mustWait = true;
        }
        else if (
            f->m_mustWait
            && g_currentVelocity.x < 100.f
            && g_currentVelocity.x > -100.f
        ) {
            f->m_mustWait = false;
        }
    }

    bool init(CCArray* p0, int p1, bool p2, CCArray* p3, DynamicScrollDelegate* p4) {
        if (!BoomScrollLayer::init(p0, p1, p2, p3, p4)) {
            return false;
        }
        
        Loader::get()->queueInMainThread([self = Ref(this)] {
            if (g_enabled && !g_isEditor) {
                self->schedule(schedule_selector(ProBoomScrollLayer::updateScroll), 0.f);
            }
        });
        
        return true;
    }

};

#ifdef GEODE_IS_WINDOWS

static WNDPROC g_ogWndProc = nullptr;

LRESULT CALLBACK ProWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {    
    switch (msg) {
        case WM_MOUSEWHEEL: {
            auto delta = GET_WHEEL_DELTA_WPARAM(wParam);
            auto steps = delta / static_cast<float>(WHEEL_DELTA);
            auto likelyTouchpad = (abs(delta) < WHEEL_DELTA);
            auto ctrl = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            auto shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
            auto alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

            if (alt) {
                steps *= 4.f * ((g_isEditor && !g_isEditorBlocked) ? g_editorMultiplier : g_scrollMultiplier);
            }

            if (!ctrl) {
                steps *= (g_isEditor && !g_isEditorBlocked) ? g_editorSensivity : g_scrollSensivity;
            }

            if (ctrl) {
                g_targetZoom *= pow(1.1f, steps * 3.4f * g_zoomSensivity);
                g_targetZoom = clamp(g_targetZoom, 0.05f, 2.5f);
            } else if (shift && !likelyTouchpad) {
                g_currentVelocity.x -= steps * ((g_isEditor && !g_isEditorBlocked) ? 600.f : 220.f);
            } else {
                g_currentVelocity.y += steps * ((g_isEditor && !g_isEditorBlocked) ? 600.f : 220.f);
            }

            break;
        }
        case WM_MOUSEHWHEEL: {
            auto delta = GET_WHEEL_DELTA_WPARAM(wParam);
            auto steps = delta / static_cast<float>(WHEEL_DELTA);
            auto alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            
            if (alt) {
                steps *= 4.f * ((g_isEditor && !g_isEditorBlocked) ? g_editorMultiplier : g_scrollMultiplier);
            }

            steps *= (g_isEditor && !g_isEditorBlocked) ? g_editorSensivity : g_scrollSensivity;

            g_currentVelocity.x += steps * ((g_isEditor && !g_isEditorBlocked) ? 600.f : 220.f);

            break;
        }
    }

    return CallWindowProc(g_ogWndProc, hwnd, msg, wParam, lParam);
}

#endif

$on_mod(Loaded) {

    #ifdef GEODE_IS_WINDOWS

    g_ogWndProc = (WNDPROC)SetWindowLongPtr(
        WindowFromDC(wglGetCurrentDC()),
        GWLP_WNDPROC,
        (LONG_PTR)ProWndProc
    );

    #endif

    updateSettings();

    listenForAllSettingChanges([](auto) {
        updateSettings();
    });
}