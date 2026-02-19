#pragma once

#include <QMainWindow>

class BgfxViewWidget;
class QThread;

/**
 * Top-level application window that hosts the viewport experience.
 *
 * MainWindow is intentionally thin: it wires shared application services (such as the build
 * thread) into UI components and owns the primary BgfxViewWidget instance. New developers can
 * treat this class as the entry point for composing high-level editor UI.
 */
class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QThread *buildThread, QWidget *parent = nullptr);
    ~MainWindow() override = default;

private:
    BgfxViewWidget *mViewWidget;
};
