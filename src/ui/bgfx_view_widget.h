#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QTimer>
#include <QThread>
#include <QWidget>

#include <chrono>

#include "scene/scene_core.h"
#include "render/render_bridge.h"

class BgfxViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit BgfxViewWidget(QThread* buildThread, QWidget* parent = nullptr);
    ~BgfxViewWidget() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    QPaintEngine* paintEngine() const override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void initializeRenderer();
    void shutdownRenderer();
    void scheduleBuildWork();
    void renderFrame();

    QTimer m_frameTimer;
    SceneCore m_sceneCore;
    QObject* m_buildWorker;
    QThread* m_buildThread;
    RenderBridge m_renderBridge;

    std::chrono::steady_clock::time_point m_lastFrameTime;
    bool m_hasFrameTime;
    bool m_primaryEditActive;
    bool m_lookActive;
    bool m_spaceHeld;
    bool m_cursorHiddenForLook;
    QPointF m_lastMousePosition;
};
