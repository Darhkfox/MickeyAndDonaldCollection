#include "app.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    static App app;
    return app.run(inst, show);
}
