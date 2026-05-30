#include "camera.h"
//main application that uses camera class
int main()
{
    Camera camera;

    if (!camera.open())
    {
        //exist if unable to open camera
        return -1;
    }

    camera.run();
    camera.close();

    return 0;
}
