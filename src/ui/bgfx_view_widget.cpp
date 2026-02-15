#include "bgfx_view_widget.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
std::optional<SceneCore::CameraMove> keyToCameraMove(int key)
{
    switch (key)
    {
    case Qt::Key_W:
        return SceneCore::CameraMove::Forward;
    case Qt::Key_S:
        return SceneCore::CameraMove::Backward;
    case Qt::Key_A:
        return SceneCore::CameraMove::Left;
    case Qt::Key_D:
        return SceneCore::CameraMove::Right;
    default:
        return std::nullopt;
    }
}
} // namespace

BgfxViewWidget::BgfxViewWidget(QThread *buildThread, QWidget *parent)
    : QWidget(parent), m_frameTimer(this), m_buildWorker(new QObject()), m_buildThread(buildThread), m_lastFrameTime(), m_hasFrameTime(false), m_primaryEditActive(false), m_lookActive(false), m_spaceHeld(false), m_cursorHiddenForLook(false), m_lastMousePosition()
{
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    qApp->installEventFilter(this);

    m_frameTimer.setInterval(16);
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_frameTimer, &QTimer::timeout, this, &BgfxViewWidget::renderFrame);

    m_buildWorker->moveToThread(m_buildThread);
}

BgfxViewWidget::~BgfxViewWidget()
{
    qApp->removeEventFilter(this);
    const bool deleteQueued = QMetaObject::invokeMethod(m_buildWorker, &QObject::deleteLater, Qt::QueuedConnection);
    if (!deleteQueued)
    {
        delete m_buildWorker;
    }
    m_buildWorker = nullptr;
    shutdownRenderer();
}

QPaintEngine *BgfxViewWidget::paintEngine() const
{
    return nullptr;
}

bool BgfxViewWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (!keyEvent->isAutoRepeat() && keyEvent->key() == Qt::Key_Space)
        {
            m_spaceHeld = (event->type() == QEvent::KeyPress);
            if (!m_spaceHeld)
            {
                m_lookActive = false;
                if (m_cursorHiddenForLook)
                {
                    qApp->restoreOverrideCursor();
                    m_cursorHiddenForLook = false;
                }
                unsetCursor();
                releaseMouse();
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void BgfxViewWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    initializeRenderer();
    m_hasFrameTime = false;
    if (m_renderBridge.initialized())
    {
        m_frameTimer.start();
    }
}

void BgfxViewWidget::hideEvent(QHideEvent *event)
{
    m_frameTimer.stop();
    m_primaryEditActive = false;
    m_lookActive = false;
    m_spaceHeld = false;
    if (m_cursorHiddenForLook)
    {
        qApp->restoreOverrideCursor();
        m_cursorHiddenForLook = false;
    }
    m_sceneCore.endPrimaryEdit();
    unsetCursor();
    releaseMouse();

    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Forward, false);
    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Backward, false);
    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Left, false);
    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Right, false);
    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Up, false);
    m_sceneCore.setCameraMoveState(SceneCore::CameraMove::Down, false);

    QWidget::hideEvent(event);
}

void BgfxViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_renderBridge.initialized())
    {
        return;
    }

    const std::uint16_t width = static_cast<std::uint16_t>(event->size().width());
    const std::uint16_t height = static_cast<std::uint16_t>(event->size().height());
    m_renderBridge.resize(width, height);
}

void BgfxViewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_spaceHeld)
        {
            m_lookActive = true;
            m_lastMousePosition = event->position();
            if (!m_cursorHiddenForLook)
            {
                qApp->setOverrideCursor(Qt::BlankCursor);
                m_cursorHiddenForLook = true;
            }
            setCursor(Qt::BlankCursor);
            grabMouse();
        }
        else
        {
            m_primaryEditActive = true;
            m_sceneCore.beginPrimaryEdit(event->position().y());
        }
        setFocus(Qt::MouseFocusReason);
    }

    QWidget::mousePressEvent(event);
}

void BgfxViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_primaryEditActive = false;
        m_sceneCore.endPrimaryEdit();
        m_lookActive = false;
        if (m_cursorHiddenForLook)
        {
            qApp->restoreOverrideCursor();
            m_cursorHiddenForLook = false;
        }
        unsetCursor();
        releaseMouse();
    }

    QWidget::mouseReleaseEvent(event);
}

void BgfxViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_primaryEditActive && (event->buttons() & Qt::LeftButton))
    {
        m_sceneCore.updatePrimaryEdit(event->position().y());
    }

    if (m_lookActive && (event->buttons() & Qt::LeftButton))
    {
        const QPointF delta = event->position() - m_lastMousePosition;
        m_lastMousePosition = event->position();
        m_sceneCore.addCameraLookDelta(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
    }

    QWidget::mouseMoveEvent(event);
}

void BgfxViewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyPressEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Space)
    {
        m_spaceHeld = true;
    }

    const std::optional<SceneCore::CameraMove> move = keyToCameraMove(event->key());
    if (move.has_value())
    {
        m_sceneCore.setCameraMoveState(*move, true);
    }

    QWidget::keyPressEvent(event);
}

void BgfxViewWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyReleaseEvent(event);
        return;
    }

    if (event->key() == Qt::Key_Space)
    {
        m_spaceHeld = false;
        m_lookActive = false;
        if (m_cursorHiddenForLook)
        {
            qApp->restoreOverrideCursor();
            m_cursorHiddenForLook = false;
        }
        unsetCursor();
        releaseMouse();
    }

    const std::optional<SceneCore::CameraMove> move = keyToCameraMove(event->key());
    if (move.has_value())
    {
        m_sceneCore.setCameraMoveState(*move, false);
    }

    QWidget::keyReleaseEvent(event);
}

void BgfxViewWidget::initializeRenderer()
{
    if (m_renderBridge.initialized() || !testAttribute(Qt::WA_WState_Created))
    {
        return;
    }

#if defined(_WIN32)
    void *nativeWindowHandle = reinterpret_cast<void *>(static_cast<HWND>(winId()));
#else
    void *nativeWindowHandle = reinterpret_cast<void *>(winId());
#endif

    const std::uint32_t renderWidth = static_cast<std::uint32_t>(width());
    const std::uint32_t renderHeight = static_cast<std::uint32_t>(height());
    m_renderBridge.initialize(nativeWindowHandle, renderWidth, renderHeight);
}

void BgfxViewWidget::shutdownRenderer()
{
    m_frameTimer.stop();
    m_renderBridge.shutdown();
}

void BgfxViewWidget::scheduleBuildWork()
{
    SceneCore::BuildTicket ticket{};
    if (!m_sceneCore.tryStartBuild(ticket))
    {
        return;
    }

    QPointer<BgfxViewWidget> self(this);

    QMetaObject::invokeMethod(
        m_buildWorker,
        [self, ticket]() mutable
    {
        SceneCore::BuiltMeshData built = SceneCore::buildRenderMesh(ticket);

        QMetaObject::invokeMethod(
            self,
            [self, built = std::move(built)]() mutable
        {
            if (!self)
            {
                return;
            }

            self->m_sceneCore.finishBuild(std::move(built));
        },
            Qt::QueuedConnection);
    },
        Qt::QueuedConnection);
}

void BgfxViewWidget::renderFrame()
{
    if (!m_renderBridge.initialized() || !isVisible())
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    float dtSeconds = 1.0F / 60.0F;
    if (m_hasFrameTime)
    {
        dtSeconds = std::chrono::duration<float>(now - m_lastFrameTime).count();
        dtSeconds = std::clamp(dtSeconds, 0.0F, 0.05F);
    }
    m_lastFrameTime = now;
    m_hasFrameTime = true;

    m_sceneCore.tickCamera(dtSeconds);

    scheduleBuildWork();

    const SceneCore::RenderSnapshot snapshot = m_sceneCore.snapshot();
    m_renderBridge.render(snapshot);
}
