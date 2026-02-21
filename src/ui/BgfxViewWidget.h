#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <cstdint>

#include "render/RenderBridge.h"
#include "scene/Core.h"

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
    enum class ShortcutCommand : uint8_t
    {
        Undo,
        Redo,
        CreateCube,
        DeleteSelection,
        CycleSelection,
        ClearSelection,
    };

    explicit BgfxViewWidget(QThread *buildThread, QWidget *parent = nullptr);
    ~BgfxViewWidget() override;

    void setShortcutBinding(ShortcutCommand command, const QKeySequence &sequence);
    QKeySequence shortcutBinding(ShortcutCommand command) const;

protected:
    QPaintEngine *paintEngine() const override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    void setupShortcuts();
    QAction *actionForCommand(ShortcutCommand command);
    const QAction *actionForCommand(ShortcutCommand command) const;
    void stopLookInteraction();
    void stopAllCameraMovement();

    void initializeRenderer();
    void shutdownRenderer();
    void scheduleBuildWork();
    void renderFrame();

    QTimer mFrameTimer;
    Scene::Core mSceneCore;
    QObject *mpBuildWorker = nullptr;
    QThread *mpBuildThread = nullptr;
    RenderBridge mRenderBridge;

    std::chrono::steady_clock::time_point mLastFrameTime;
    bool mHasFrameTime = false;
    bool mLookActive = false;
    bool mCursorHiddenForLook = false;
    bool mPendingSelectionClick = false;
    bool mPendingSelectionCtrlHeld = false;
    QSet<int> mHeldKeys;
    QPointF mLastMousePosition;

    QPointer<QAction> mpUndoAction = nullptr;
    QPointer<QAction> mpRedoAction = nullptr;
    QPointer<QAction> mpCreateCubeAction = nullptr;
    QPointer<QAction> mpDeleteSelectionAction = nullptr;
    QPointer<QAction> mpCycleSelectionAction = nullptr;
    QPointer<QAction> mpClearSelectionAction = nullptr;
};
