#include <gtest/gtest.h>
#include "camera.h"

TEST(CameraTests, CameraOpensSuccessfully)
{
    Camera cam(10);

    ASSERT_TRUE(cam.open());
    ASSERT_TRUE(cam.isOpen());
}

TEST(CameraTests, CameraClosesSuccessfully)
{
    Camera cam(10);

    ASSERT_TRUE(cam.open());

    cam.close();

    ASSERT_FALSE(cam.isOpen());
}
