#pragma once

#include "camera/CameraConfiguration.h"

#include <QImage>
#include <QMainWindow>
#include <QString>
#include <vector>

class QComboBox;
class QLabel;
class QSpinBox;
class QPushButton;
class QCloseEvent;

/**
 * Minimal face-detect UI: video view, resolution/FPS controls, Apply.
 */
class FaceDetectWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit FaceDetectWindow(std::vector<CameraConfiguration> modes,
                              int initialModeIndex,
                              int initialFps,
                              QWidget* parent = nullptr);

    void setVideoFrame(const QImage& image);
    void setStatus(const QString& text);

signals:
    void applyRequested(int modeIndex, int fps);
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLabel* videoLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QSpinBox* fpsSpin_ = nullptr;
    QPushButton* applyButton_ = nullptr;
};
