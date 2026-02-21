#include "BgfxViewWidget.h"

#include <QApplication>
#include <QAction>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <bx/bx.h>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
constexpr int FRAME_INTERVAL_MS = 16;
constexpr float DEFAULT_DT_SECONDS = 1.0F / 60.0F;
constexpr float MIN_DT_SECONDS = 0.0F;
constexpr float MAX_DT_SECONDS = 0.05F;

/**
 * Converts Qt keyboard input to camera movement directions.
 * @param[in] key Qt key code.
 * @return Camera move state when key is mapped, otherwise std::nullopt.
 */
std::optional<Scene::CameraMove> keyToCameraMove(int key)
{
    switch (key)
    {
    case Qt::Key_W:
        return Scene::CameraMove::Forward;
    case Qt::Key_S:
        return Scene::CameraMove::Backward;
    case Qt::Key_A:
        return Scene::CameraMove::Left;
    case Qt::Key_D:
        return Scene::CameraMove::Right;
    default:
        return std::nullopt;
    }
}
} /* namespace */

/**
 * Configures widget, render timer, and build worker threading.
 * @param[in] buildThread Worker thread used for scene mesh builds.
 * @param[in] parent Optional parent widget.
 */
BgfxViewWidget::BgfxViewWidget(QThread *buildThread, QWidget *parent)
    : QWidget(parent), mFrameTimer(this), mpBuildWorker(new QObject()), mpBuildThread(buildThread)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setupShortcuts();

    mFrameTimer.setInterval(FRAME_INTERVAL_MS);
    mFrameTimer.setTimerType(Qt::PreciseTimer);
    connect(&mFrameTimer, &QTimer::timeout, this, &BgfxViewWidget::renderFrame);

    mpBuildWorker->moveToThread(mpBuildThread);
}

/**
 * Cleans up event filtering, worker object, and renderer state.
 */
BgfxViewWidget::~BgfxViewWidget()
{
    const bool deleteQueued = QMetaObject::invokeMethod(mpBuildWorker, &QObject::deleteLater, Qt::QueuedConnection);
    if (!deleteQueued)
    {
        delete mpBuildWorker;
    }
    mpBuildWorker = nullptr;
    shutdownRenderer();
}

/**
 * Disables Qt paint engine because bgfx owns rendering.
 * @return Always nullptr.
 */
QPaintEngine *BgfxViewWidget::paintEngine() const
{
    return nullptr;
}

/**
 * Initializes renderer and starts frame timer when widget becomes visible.
 * @param[in] event Qt show event.
 */
void BgfxViewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    initializeRenderer();
    mHasFrameTime = false;
    mFrameTimer.start();
}

/**
 * Stops interaction/rendering and clears transient input state on hide.
 * @param[in] event Qt hide event.
 */
void BgfxViewWidget::hideEvent(QHideEvent *event)
{
    mFrameTimer.stop();
    mPendingSelectionClick = false;
    mPendingSelectionCtrlHeld = false;
    mHeldKeys.clear();
    stopLookInteraction();

    stopAllCameraMovement();

    QWidget::hideEvent(event);
}

void BgfxViewWidget::focusOutEvent(QFocusEvent *event)
{
    mHeldKeys.clear();
    stopLookInteraction();
    stopAllCameraMovement();
    QWidget::focusOutEvent(event);
}

/**
 * Resizes bgfx backbuffer to match widget size.
 * @param[in] event Qt resize event.
 */
void BgfxViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!testAttribute(Qt::WA_WState_Created))
    {
        return;
    }

    initializeRenderer();
    const qreal pixelRatio = devicePixelRatioF();
    const uint16_t width = static_cast<uint16_t>(std::max(1L, std::lround(static_cast<double>(event->size().width()) * static_cast<double>(pixelRatio))));
    const uint16_t height = static_cast<uint16_t>(std::max(1L, std::lround(static_cast<double>(event->size().height()) * static_cast<double>(pixelRatio))));
    mRenderBridge.resize(width, height);
}

/**
 * Starts camera-look or selection/component interaction on left click.
 * @param[in] event Qt mouse press event.
 */
void BgfxViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        mPendingSelectionClick = false;
        mPendingSelectionCtrlHeld = false;

        if (mHeldKeys.contains(Qt::Key_Space))
        {
            mLookActive = true;
            mLastMousePosition = event->position();
            if (!mCursorHiddenForLook)
            {
                qApp->setOverrideCursor(Qt::BlankCursor);
                mCursorHiddenForLook = true;
            }
            setCursor(Qt::BlankCursor);
            grabMouse();
        }
        else
        {
            const float mouseX = static_cast<float>(event->position().x());
            const float mouseY = static_cast<float>(event->position().y());
            const bool ctrlHeld = (event->modifiers() & Qt::ControlModifier) != 0;
            const bool componentHit = mSceneCore.selectComponentAtScreen(
                mouseX,
                mouseY,
                static_cast<float>(width()),
                static_cast<float>(height()),
                ctrlHeld);

            if (!componentHit)
            {
                mPendingSelectionClick = true;
                mPendingSelectionCtrlHeld = ctrlHeld;
            }
        }
        setFocus(Qt::MouseFocusReason);
    }

    QWidget::mousePressEvent(event);
}

/**
 * Ends active mouse-driven edit or look interaction.
 * @param[in] event Qt mouse release event.
 */
void BgfxViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (mPendingSelectionClick)
        {
            const float mouseX = static_cast<float>(event->position().x());
            const float mouseY = static_cast<float>(event->position().y());
            mSceneCore.selectObjectAtScreen(mouseX, mouseY, static_cast<float>(width()), static_cast<float>(height()), mPendingSelectionCtrlHeld);
        }
        mPendingSelectionClick = false;
        mPendingSelectionCtrlHeld = false;

        stopLookInteraction();
    }

    QWidget::mouseReleaseEvent(event);
}

/**
 * Routes mouse motion to camera look updates.
 * @param[in] event Qt mouse move event.
 */
void BgfxViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (mLookActive && (event->buttons() & Qt::LeftButton))
    {
        const QPointF delta = event->position() - mLastMousePosition;
        mLastMousePosition = event->position();
        mSceneCore.addCameraLookDelta(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    }

    QWidget::mouseMoveEvent(event);
}

/**
 * Handles editor shortcuts and movement-key activation.
 * @param[in] event Qt key press event.
 */
void BgfxViewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    mHeldKeys.insert(event->key());

    const Qt::KeyboardModifiers disallowed = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier;
    if ((event->modifiers() & disallowed) == Qt::NoModifier)
    {
        const std::optional<Scene::CameraMove> move = keyToCameraMove(event->key());
        if (move.has_value())
        {
            mSceneCore.setCameraMoveState(*move, true);
        }
    }

    QWidget::keyPressEvent(event);
}

/**
 * Handles movement-key deactivation and exits look mode on Space release.
 * @param[in] event Qt key release event.
 */
void BgfxViewWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyReleaseEvent(event);
        return;
    }

    mHeldKeys.remove(event->key());

    const std::optional<Scene::CameraMove> move = keyToCameraMove(event->key());
    if (move.has_value())
    {
        mSceneCore.setCameraMoveState(*move, false);
    }

    QWidget::keyReleaseEvent(event);
}

void BgfxViewWidget::setupShortcuts()
{
    mpUndoAction = new QAction(this);
    mpRedoAction = new QAction(this);
    mpCreateCubeAction = new QAction(this);
    mpDeleteSelectionAction = new QAction(this);
    mpCycleSelectionAction = new QAction(this);
    mpClearSelectionAction = new QAction(this);

    const auto applyShortcutContext = [](QAction *action)
    {
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    };

    applyShortcutContext(mpUndoAction);
    applyShortcutContext(mpRedoAction);
    applyShortcutContext(mpCreateCubeAction);
    applyShortcutContext(mpDeleteSelectionAction);
    applyShortcutContext(mpCycleSelectionAction);
    applyShortcutContext(mpClearSelectionAction);

    mpUndoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    mpRedoAction->setShortcuts({ QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), QKeySequence(Qt::CTRL | Qt::Key_Y) });
    mpCreateCubeAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
    mpDeleteSelectionAction->setShortcuts({ QKeySequence(Qt::Key_X), QKeySequence(Qt::Key_Delete) });
    mpCycleSelectionAction->setShortcut(QKeySequence(Qt::Key_Tab));
    mpClearSelectionAction->setShortcut(QKeySequence(Qt::Key_Escape));

    addAction(mpUndoAction);
    addAction(mpRedoAction);
    addAction(mpCreateCubeAction);
    addAction(mpDeleteSelectionAction);
    addAction(mpCycleSelectionAction);
    addAction(mpClearSelectionAction);

    connect(mpUndoAction, &QAction::triggered, this, [this]() { mSceneCore.undo(); });
    connect(mpRedoAction, &QAction::triggered, this, [this]() { mSceneCore.redo(); });
    connect(mpCreateCubeAction, &QAction::triggered, this, [this]() { mSceneCore.createCubeInFrontOfCamera(); });
    connect(mpDeleteSelectionAction, &QAction::triggered, this, [this]() { mSceneCore.deleteSelectedObject(); });
    connect(mpCycleSelectionAction, &QAction::triggered, this, [this]() { mSceneCore.selectNextObject(); });
    connect(mpClearSelectionAction, &QAction::triggered, this, [this]() { mSceneCore.clearSelection(); });
}

QAction *BgfxViewWidget::actionForCommand(ShortcutCommand command)
{
    switch (command)
    {
    case ShortcutCommand::Undo:
        return mpUndoAction;
    case ShortcutCommand::Redo:
        return mpRedoAction;
    case ShortcutCommand::CreateCube:
        return mpCreateCubeAction;
    case ShortcutCommand::DeleteSelection:
        return mpDeleteSelectionAction;
    case ShortcutCommand::CycleSelection:
        return mpCycleSelectionAction;
    case ShortcutCommand::ClearSelection:
        return mpClearSelectionAction;
    }

    return nullptr;
}

const QAction *BgfxViewWidget::actionForCommand(ShortcutCommand command) const
{
    switch (command)
    {
    case ShortcutCommand::Undo:
        return mpUndoAction;
    case ShortcutCommand::Redo:
        return mpRedoAction;
    case ShortcutCommand::CreateCube:
        return mpCreateCubeAction;
    case ShortcutCommand::DeleteSelection:
        return mpDeleteSelectionAction;
    case ShortcutCommand::CycleSelection:
        return mpCycleSelectionAction;
    case ShortcutCommand::ClearSelection:
        return mpClearSelectionAction;
    }

    return nullptr;
}

void BgfxViewWidget::setShortcutBinding(ShortcutCommand command, const QKeySequence &sequence)
{
    QAction *action = actionForCommand(command);
    if (action == nullptr)
    {
        return;
    }

    action->setShortcut(sequence);
}

QKeySequence BgfxViewWidget::shortcutBinding(ShortcutCommand command) const
{
    const QAction *action = actionForCommand(command);
    if (action == nullptr)
    {
        return {};
    }

    return action->shortcut();
}

void BgfxViewWidget::stopLookInteraction()
{
    mLookActive = false;
    if (mCursorHiddenForLook)
    {
        qApp->restoreOverrideCursor();
        mCursorHiddenForLook = false;
    }

    unsetCursor();
    releaseMouse();
}

void BgfxViewWidget::stopAllCameraMovement()
{
    mSceneCore.setCameraMoveState(Scene::CameraMove::Forward, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Backward, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Left, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Right, false);
}

/**
 * Initializes the renderer using this widget's native window handle.
 */
void BgfxViewWidget::initializeRenderer()
{
    if (!testAttribute(Qt::WA_WState_Created))
    {
        return;
    }

#if defined(_WIN32)
    void *nativeWindowHandle = reinterpret_cast<void *>(static_cast<HWND>(winId()));
#else
    void *nativeWindowHandle = reinterpret_cast<void *>(winId());
#endif

    const qreal pixelRatio = devicePixelRatioF();
    const uint32_t renderWidth = static_cast<uint32_t>(std::max(1L, std::lround(static_cast<double>(width()) * static_cast<double>(pixelRatio))));
    const uint32_t renderHeight = static_cast<uint32_t>(std::max(1L, std::lround(static_cast<double>(height()) * static_cast<double>(pixelRatio))));
    mRenderBridge.initialize(nativeWindowHandle, renderWidth, renderHeight);
}

/**
 * Stops frame timer and shuts renderer down.
 */
void BgfxViewWidget::shutdownRenderer()
{
    mFrameTimer.stop();
    mRenderBridge.shutdown();
}

/**
 * Queues asynchronous mesh build work when the scene needs rebuilding.
 */
void BgfxViewWidget::scheduleBuildWork()
{
    Scene::BuildTicket ticket{};
    if (!mSceneCore.tryStartBuild(ticket))
    {
        return;
    }

    QPointer<BgfxViewWidget> self(this);

    QMetaObject::invokeMethod(
        mpBuildWorker,
        [self, ticket]() mutable
    {
        Scene::BuiltMeshData builtForUiThread = Scene::Core::buildRenderMesh(ticket);

        QMetaObject::invokeMethod(
            self,
            [self, builtMeshData = std::move(builtForUiThread)]() mutable
        {
            if (!self)
            {
                return;
            }

            self->mSceneCore.finishBuild(std::move(builtMeshData));
        },
            Qt::QueuedConnection);
    },
        Qt::QueuedConnection);
}

/**
 * Advances simulation and renders one frame.
 */
void BgfxViewWidget::renderFrame()
{
    if (!isVisible())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float dtSeconds = DEFAULT_DT_SECONDS;
    if (mHasFrameTime)
    {
        dtSeconds = std::chrono::duration<float>(now - mLastFrameTime).count();
        dtSeconds = bx::clamp(dtSeconds, MIN_DT_SECONDS, MAX_DT_SECONDS);
    }
    mLastFrameTime = now;
    mHasFrameTime = true;

    mSceneCore.tickCamera(dtSeconds);

    scheduleBuildWork();

    const Scene::RenderSnapshot snapshot = mSceneCore.snapshot();
    mRenderBridge.render(snapshot);
}
