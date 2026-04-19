#pragma once

#include <QDockWidget>

namespace trailer {

class Sidebar : public QDockWidget {
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
};

}  // namespace trailer
