#include <gtest/gtest.h>
#include "camera.h"

TEST(CameraTests, CanInstantiateCamera)
{
    Camera cam;
    SUCCEED();
}

TEST(CameraTests, OpenCameraDoesNotCrash)
{
    Camera cam;
    cam.open();
    SUCCEED();
}

TEST(CameraTests, CloseDoesNotCrash)
{
    Camera cam;
    cam.open();
    cam.close();
    SUCCEED();
}

TEST(CameraTests, RunDoesNotCrash)
{
    Camera cam;
    cam.open();

    cam.run(); // WARNING: may block depending on implementation

    SUCCEED();
}
