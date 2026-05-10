#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

namespace trailer {

class IDocument;

class AnimationBar : public QWidget {
    Q_OBJECT

  public:
    explicit AnimationBar(QWidget *parent = nullptr);

    void setDocument(IDocument *doc);

  private slots:
    void onPlayToggled();
    void onSliderMoved(int value);
    void poll();

  private:
    void refreshPlayIcon();

    IDocument *m_doc = nullptr;
    QToolButton *m_playButton = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_counter = nullptr;
    QTimer m_pollTimer;
};

} // namespace trailer
