#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <chrono>

#include "render/RenderBridge.h"
#include "scene/SceneCore.h"

/**
 * Interactive viewport widget that coordinates input, scene updates, build work, and rendering.
 *
 * BgfxViewWidget connects Qt events and frame timing to the core editor pipeline. It translates
 * user input into scene core edits/camera motion, schedules mesh build tasks on the provided build
 * thread, and drives RenderBridge each frame. This class is the glue between UI interaction and
 * the scene/render subsystems.
 */
class BgfxViewWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit BgfxViewWidget(QThread *buildThread, QWidget *parent = nullptr);
    ~BgfxViewWidget() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    QPaintEngine *paintEngine() const override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    void initializeRenderer();
    void shutdownRenderer();
    void scheduleBuildWork();
    void renderFrame();

    QTimer mFrameTimer;
    Scene::Core mSceneCore;
    QObject *mBuildWorker = nullptr;
    QThread *mBuildThread = nullptr;
    RenderBridge mRenderBridge;

    std::chrono::steady_clock::time_point mLastFrameTime;
    bool mHasFrameTime = false;
    bool mPrimaryEditActive = false;
    bool mLookActive = false;
    bool mSpaceHeld = false;
    bool mCursorHiddenForLook = false;
    QPointF mLastMousePosition;
};
