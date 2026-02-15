#include "main_window.h"

#include <QStatusBar>
#include <QThread>

#include "bgfx_view_widget.h"

MainWindow::MainWindow(QThread* buildThread, QWidget* parent)
    : QMainWindow(parent)
    , m_viewWidget(new BgfxViewWidget(buildThread, this))
{
    setWindowTitle("Carve");
    resize(1440, 900);
    setCentralWidget(m_viewWidget);
    statusBar()->showMessage("Controls: Left-drag edits cube height (build mesh). Space + left-drag looks. WASD + Q/E move camera.");
}
