// The platform's window handle type, as the video and input layers take it.
#pragma once
#ifdef _WIN32
struct HWND__;
typedef HWND__* HWND;
typedef HWND NativeWindow;
#else
struct SDL_Window;
typedef SDL_Window* NativeWindow;
#endif
