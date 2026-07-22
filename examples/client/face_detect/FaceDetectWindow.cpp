#include "FaceDetectWindow.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

FaceDetectWindow::FaceDetectWindow(std::vector<CameraConfiguration> modes,
                                   int initialModeIndex,
                                   int initialFps,
                                   QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Face Detect"));

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    videoLabel_ = new QLabel(central);
    videoLabel_->setMinimumSize(640, 480);
    videoLabel_->setAlignment(Qt::AlignCenter);
    videoLabel_->setStyleSheet(QStringLiteral("background: #202020; color: #aaaaaa;"));
    videoLabel_->setText(QStringLiteral("Waiting for frames…"));
    root->addWidget(videoLabel_, 1);

    statusLabel_ = new QLabel(central);
    root->addWidget(statusLabel_);

    auto* controls = new QHBoxLayout();
    modeCombo_ = new QComboBox(central);
    for (const CameraConfiguration& mode : modes)
    {
        modeCombo_->addItem(
            QStringLiteral("%1×%2").arg(mode.width).arg(mode.height));
    }
    if (initialModeIndex >= 0 && initialModeIndex < modeCombo_->count())
    {
        modeCombo_->setCurrentIndex(initialModeIndex);
    }

    fpsSpin_ = new QSpinBox(central);
    fpsSpin_->setRange(1, 60);
    fpsSpin_->setValue(initialFps > 0 ? initialFps : 30);
    fpsSpin_->setSuffix(QStringLiteral(" fps"));

    applyButton_ = new QPushButton(QStringLiteral("Apply"), central);

    controls->addWidget(new QLabel(QStringLiteral("Resolution"), central));
    controls->addWidget(modeCombo_, 1);
    controls->addWidget(new QLabel(QStringLiteral("FPS"), central));
    controls->addWidget(fpsSpin_);
    controls->addWidget(applyButton_);
    root->addLayout(controls);

    setCentralWidget(central);
    resize(960, 720);

    connect(applyButton_, &QPushButton::clicked, this, [this]() {
        emit applyRequested(modeCombo_->currentIndex(), fpsSpin_->value());
    });
}

void FaceDetectWindow::setVideoFrame(const QImage& image)
{
    if (image.isNull())
    {
        return;
    }
    videoLabel_->setPixmap(QPixmap::fromImage(image).scaled(
        videoLabel_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void FaceDetectWindow::setStatus(const QString& text)
{
    statusLabel_->setText(text);
}

void FaceDetectWindow::closeEvent(QCloseEvent* event)
{
    emit quitRequested();
    QMainWindow::closeEvent(event);
}
