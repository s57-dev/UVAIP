#include "camera.h"
//main application that uses camera class
int main()
{
    Camera camera(10);

    if (!camera.open())
    {
        return -1;
    }

    camera.run();
    camera.close();

    return 0;
}
