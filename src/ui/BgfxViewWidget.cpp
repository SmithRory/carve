#include "BgfxViewWidget.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>

#include <algorithm>
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
    : QWidget(parent), mFrameTimer(this), mBuildWorker(new QObject()), mBuildThread(buildThread)
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    qApp->installEventFilter(this);

    mFrameTimer.setInterval(FRAME_INTERVAL_MS);
    mFrameTimer.setTimerType(Qt::PreciseTimer);
    connect(&mFrameTimer, &QTimer::timeout, this, &BgfxViewWidget::renderFrame);

    mBuildWorker->moveToThread(mBuildThread);
}

/**
 * Cleans up event filtering, worker object, and renderer state.
 */
BgfxViewWidget::~BgfxViewWidget()
{
    qApp->removeEventFilter(this);
    const bool deleteQueued = QMetaObject::invokeMethod(mBuildWorker, &QObject::deleteLater, Qt::QueuedConnection);
    if (!deleteQueued)
    {
        delete mBuildWorker;
    }
    mBuildWorker = nullptr;
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
 * Tracks Space key state globally while the widget is active.
 * @param[in] watched Event source object.
 * @param[in] event Event to process.
 * @return Result from base event filter.
 */
bool BgfxViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (!keyEvent->isAutoRepeat() && keyEvent->key() == Qt::Key_Space)
        {
            mSpaceHeld = (event->type() == QEvent::KeyPress);
            if (!mSpaceHeld)
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
        }
    }

    return QWidget::eventFilter(watched, event);
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
    mPrimaryEditActive = false;
    mLookActive = false;
    mSpaceHeld = false;
    if (mCursorHiddenForLook)
    {
        qApp->restoreOverrideCursor();
        mCursorHiddenForLook = false;
    }
    mSceneCore.endPrimaryEdit();
    unsetCursor();
    releaseMouse();

    mSceneCore.setCameraMoveState(Scene::CameraMove::Forward, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Backward, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Left, false);
    mSceneCore.setCameraMoveState(Scene::CameraMove::Right, false);

    QWidget::hideEvent(event);
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
    const uint16_t width = static_cast<uint16_t>(event->size().width());
    const uint16_t height = static_cast<uint16_t>(event->size().height());
    mRenderBridge.resize(width, height);
}

/**
 * Starts either camera-look or primary edit interaction on left click.
 * @param[in] event Qt mouse press event.
 */
void BgfxViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (mSpaceHeld)
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
            mPrimaryEditActive = true;
            mSceneCore.beginPrimaryEdit(static_cast<float>(event->position().y()));
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
        mPrimaryEditActive = false;
        mSceneCore.endPrimaryEdit();
        mLookActive = false;
        if (mCursorHiddenForLook)
        {
            qApp->restoreOverrideCursor();
            mCursorHiddenForLook = false;
        }
        unsetCursor();
        releaseMouse();
    }

    QWidget::mouseReleaseEvent(event);
}

/**
 * Routes mouse motion to edit extrusion or camera look updates.
 * @param[in] event Qt mouse move event.
 */
void BgfxViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (mPrimaryEditActive && (event->buttons() & Qt::LeftButton))
    {
        mSceneCore.updatePrimaryEdit(static_cast<float>(event->position().y()));
    }

    if (mLookActive && (event->buttons() & Qt::LeftButton))
    {
        const QPointF delta = event->position() - mLastMousePosition;
        mLastMousePosition = event->position();
        mSceneCore.addCameraLookDelta(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    }

    QWidget::mouseMoveEvent(event);
}

/**
 * Handles movement-key activation and look-mode modifier state.
 * @param[in] event Qt key press event.
 */
void BgfxViewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Space)
    {
        mSpaceHeld = true;
    }

    const std::optional<Scene::CameraMove> move = keyToCameraMove(event->key());
    if (move.has_value())
    {
        mSceneCore.setCameraMoveState(*move, true);
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

    if (event->key() == Qt::Key_Space)
    {
        mSpaceHeld = false;
        mLookActive = false;
        if (mCursorHiddenForLook)
        {
            qApp->restoreOverrideCursor();
            mCursorHiddenForLook = false;
        }
        unsetCursor();
        releaseMouse();
    }

    const std::optional<Scene::CameraMove> move = keyToCameraMove(event->key());
    if (move.has_value())
    {
        mSceneCore.setCameraMoveState(*move, false);
    }

    QWidget::keyReleaseEvent(event);
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

    const uint32_t renderWidth = static_cast<uint32_t>(width());
    const uint32_t renderHeight = static_cast<uint32_t>(height());
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
        mBuildWorker,
        [self, ticket]() mutable
    {
        /* Build on worker thread, then marshal completion back to the UI thread. */
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
        /* Clamp dt to avoid large camera jumps after stalls or tab switches. */
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
