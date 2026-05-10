#pragma once

#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace trailer {

class Magnifier : public QWidget {
    Q_OBJECT

  public:
    explicit Magnifier(QWidget *parent = nullptr);

    void setTarget(QWidget *target);
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_factor; }

    void activate();
    void deactivate();
    bool isActive() const { return m_active; }

  protected:
    void paintEvent(QPaintEvent *event) override;

  private slots:
    void tick();

  private:
    QPointer<QWidget> m_target;
    QTimer m_timer;
    QImage m_snapshot;
    double m_factor = 2.0;
    bool m_active = false;
};

} // namespace trailer
