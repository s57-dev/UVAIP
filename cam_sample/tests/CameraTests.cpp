#include <gtest/gtest.h>
#include "camera.h"

TEST(CameraTests, CameraOpensSuccessfully)
{
    MockCamera cam;

    ASSERT_TRUE(cam.open());
    ASSERT_TRUE(cam.isOpen());
}

TEST(CameraTests, CameraClosesSuccessfully)
{
    MockCamera cam;

    ASSERT_TRUE(cam.open());

    cam.close();

    ASSERT_FALSE(cam.isOpen());
}
