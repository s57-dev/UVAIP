#include "camera.h"

int main()
{
    Camera camera;

    if (!camera.open())
    {
        return -1;
    }

    camera.run();
    camera.close();

    return 0;
}
