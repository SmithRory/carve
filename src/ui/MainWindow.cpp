#include "MainWindow.h"

#include <QStatusBar>
#include <QThread>

#include "BgfxViewWidget.h"

namespace
{
constexpr int INITIAL_WINDOW_WIDTH = 1440;
constexpr int INITIAL_WINDOW_HEIGHT = 900;
} // namespace

/**
 * Creates the main application window and embeds the bgfx view widget.
 * @param[in] buildThread Worker thread used for asynchronous mesh builds.
 * @param[in] parent Optional Qt parent widget.
 */
MainWindow::MainWindow(QThread* buildThread, QWidget* parent)
    : QMainWindow(parent)
    , mViewWidget(new BgfxViewWidget(buildThread, this))
{
    setWindowTitle("Carve");
    resize(INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT);
    setCentralWidget(mViewWidget);
    statusBar()->showMessage("Controls: Left-drag edits cube height (build mesh). Space + left-drag looks. WASD + Q/E move camera.");
}
