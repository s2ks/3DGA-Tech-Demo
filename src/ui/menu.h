#pragma once

#include "utils/config.h"

class Menu {
public:
    Menu(Config& config);
    void draw();

private:
    Config& m_config;
};
