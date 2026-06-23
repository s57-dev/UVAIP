#pragma once

#include "ICamera.h"
#include <iostream>

class MockCamera : public ICamera 
{
private:
    bool state = false; 
public:
    bool open() override
    {
        std::cout << "Mock open\n";
        state = true;
        return true;
    }

    void run() override
    {
        std::cout << "Mock run\n";
    }

    void close() override
    {
        std::cout << "Mock close\n";
        state = false; 
    }

    bool isOpen() const override
    {
        return state;
    }
};