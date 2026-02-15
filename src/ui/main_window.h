#pragma once

#include <QMainWindow>

class BgfxViewWidget;
class QThread;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QThread* buildThread, QWidget* parent = nullptr);
    ~MainWindow() override = default;

private:
    BgfxViewWidget* m_viewWidget;
};
