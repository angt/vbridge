#include "buffer-static.h"
#include "core-client.h"
#include "option.h"

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>

static const char class_name[] = PROG_NAME;

static struct client_global {
    const char *host;

    core_client_t core;

    struct {
        HDC dc;
        HBITMAP id;
        image_info_t info;
    } image;

    struct {
        int w, h;
        HWND id;
        HDC dc;
    } window;
} global;

static void
image_update(void)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(global.window.id, &ps);
    BitBlt(global.window.dc, 0, 0, global.image.info.w, global.image.info.h, global.image.dc, 0, 0, SRCCOPY);
    EndPaint(global.window.id, &ps);
}

static void
image_cleanup(void)
{
    if (global.image.dc) {
        DeleteDC(global.image.dc);
        global.image.dc = NULL;
    }

    if (global.image.id) {
        DeleteObject(global.image.id);
        global.image.id = NULL;
    }
}

static void
image_resize(int w, int h)
{
    static int first = 1;

    if (!w || !h)
        return;

    if ((w == global.image.info.w) && // XXX
        (h == global.image.info.h) && // XXX
        (!first))                     // XXX
        return;                       // XXX

    image_cleanup();

    BITMAPINFO bmi;
    byte_set(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biSizeImage = w * h * 4;

    global.image.dc = CreateCompatibleDC(global.window.dc);
    global.image.id = CreateDIBSection(global.image.dc, &bmi, DIB_RGB_COLORS,
                                       (void **)&global.image.info.data, NULL, 0);

    if (!global.image.id)
        error("oups\n");

    SelectObject(global.image.dc, global.image.id);

    global.image.info.w = w;
    global.image.info.h = h;
    global.image.info.stride = w;

    global.core.image = global.image.info;

    if (first) {
        SetWindowPos(global.window.id, HWND_TOP, 0, 0, w, h, SWP_NOMOVE | SWP_SHOWWINDOW);
        first = 0;
    }
}

char *
terminal_get(const char *prompt, int hide)
{
    DWORD mode, count;

    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!GetConsoleMode(input, &mode)) {
        warning("no console\n");
        return NULL;
    }

    SetConsoleMode(input, mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));

    WriteConsoleA(output, prompt, str_len(prompt), &count, NULL);

    char *str = safe_calloc(1, 1);
    int len = 0;

    while (str) {
        int n = 1;
        char buf[8];

        if (!ReadConsoleA(input, buf, 1, &count, NULL))
            break;

        if ((buf[0] == '\n') || (buf[0] == '\r'))
            break;

        if ((buf[0] >= ' ') && (buf[0] <= '~')) {
            str = safe_realloc(str, len + n + 1);
            byte_copy(&str[len], buf, n);
            len += n;
            if (hide) {
                WriteConsoleA(output, "*", 1, &count, NULL);
            } else {
                WriteConsoleA(output, buf, n, &count, NULL);
            }
            continue;
        }

        switch (buf[0]) {
        case '\b':
            if (len) {
                WriteConsoleA(output, "\b \b", 3, &count, NULL);
                len--;
            }
            break;
        }
    }

    WriteConsoleA(output, "\r\n", 2, &count, NULL);

    SetConsoleMode(input, mode);

    if (str)
        str[len] = '\0';

    return str;
}

static void
send_auth(void) // fake
{
    char *name = terminal_get("Login: ", 0);
    char *pass = terminal_get("Password: ", 1);

    core_send_auth(&global.core, name, pass);

    safe_free(pass);
    safe_free(name);
}

LRESULT CALLBACK
WndProc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(wnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        running = 0;
        break;
    case WM_LBUTTONUP:
    case WM_LBUTTONDOWN:
        core_send_button(&global.core, 1, msg == WM_LBUTTONDOWN);
        break;
    case WM_MBUTTONUP:
    case WM_MBUTTONDOWN:
        core_send_button(&global.core, 2, msg == WM_MBUTTONDOWN);
        break;
    case WM_RBUTTONUP:
    case WM_RBUTTONDOWN:
        core_send_button(&global.core, 3, msg == WM_RBUTTONDOWN);
        break;
    case WM_MOUSEMOVE:
        core_send_pointer(&global.core, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), 0);
        break;
    case WM_PAINT:
        image_update();
        break;
    case WM_SIZE:
        global.window.w = LOWORD(lparam);
        global.window.h = HIWORD(lparam);
        core_send_resize(&global.core, global.window.w, global.window.h);
        break;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProc(wnd, msg, wparam, lparam);
    }

    return 0;
}

static void
pretty_print(char *str)
{
    while (!str_empty(str)) {
        char *end = str_skip_char_space(str);
        info("%s: %.*s\n", global.core.netio.name, end - str, str);
        str = str_skip_line(end);
    }
}

static int
window_init(HINSTANCE instance)
{
    WNDCLASSEX wc;
    byte_set(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc))
        return 0;

    global.window.id = CreateWindowEx(0, class_name, global.host,
                                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, instance, NULL);

    if (!global.window.id)
        return 0;

    RECT rect;
    GetWindowRect(global.window.id, &rect);

    global.window.w = rect.right - rect.left;
    global.window.h = rect.bottom - rect.top;

    global.window.dc = GetDC(global.window.id);

    return 1;
}

static void
main_init(HINSTANCE instance, int argc, char **argv)
{
    const char *port = CONFIG_PORT;
    const char *ciphers = CONFIG_SSL_CIPHERS;

    option(opt_host, &global.host, NULL, NULL);
    option(opt_port, &port, NULL, NULL);
    option(opt_port, &port, "port", "port to connect on the remote host");

    option(opt_list, &ciphers, "ciphers", "sets the list of ciphers");

    option_run(argc, argv);

    common_init();
    socket_init();
    openssl_init();

    openssl_use_ciphers(ciphers);
    openssl_print_error(NULL);

    if (!core_create(&global.core, global.host, port))
        exit(2);

    if (!window_init(instance))
        exit(3);
}

static void
main_exit()
{
    image_cleanup();
    core_delete(&global.core);
    openssl_exit();
    socket_exit();
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE prev_instance, LPSTR cmdline, int cmdshow)
{
    atexit(main_exit);
    main_init(instance, __argc, __argv);

    while (running) {
        socket_wait(global.core.netio.fd, SOCKET_WAIT_R, 1);

        MSG msg;

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        int ret = core_recv_all(&global.core);

        if (core_received(&global.core, command_start))
            send_auth();

        if (core_received(&global.core, command_access)) {
            if (!global.core.level) {
                send_auth();
            } else if (global.core.access <= 0) {
                pretty_print("access denied");
                break;
            } else {
                core_send(&global.core, command_access);
            }
        }

        image_resize(global.core.size.w, global.core.size.h);

        if (core_received(&global.core, command_pointer_sync)) {
            //  pointer_warp(global.window.id, global.core.pointer.sx, global.core.pointer.sy);
        }

        if (core_received(&global.core, command_image_data))
            image_update();

        char *data;

        if (data = core_recv_clipboard(&global.core), data)
            safe_free(data);

        if (data = core_recv_control(&global.core), data) {
            pretty_print(data);
            safe_free(data);
        }

        if (!ret)
            break;

        core_send_all(&global.core);
    }

    return 0;
}

#else
#endif
