#pragma once

#include "ICamera.h"
#include <iostream>

class MockCamera : public ICamera
{
public:
    bool open() override
    {
        std::cout << "Mock open\n";
        return true;
    }

    void run() override
    {
        std::cout << "Mock run\n";
    }

    void close() override
    {
        std::cout << "Mock close\n";
    }

    bool isOpen() const override
    {
        return true;
    }
};