#pragma once

class ICamera
{
public:
    virtual ~ICamera() = default;

    virtual bool open() = 0;
    virtual void run() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};