/* SPDX-License-Identifier: GPL-3.0-only */
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_gl(const char *operation)
{
    GLenum error = glGetError();
    if (error == GL_NO_ERROR) return 1;
    fprintf(stderr, "Cannot %s: OpenGL error %#x.\n", operation, (unsigned)error);
    return 0;
}

static int load_gl_proc(void *target, size_t size, const char *name)
{
    PROC procedure = wglGetProcAddress(name);
    uintptr_t address;
    _Static_assert(sizeof(procedure) == sizeof(address), "Unexpected Windows procedure representation");
    memcpy(&address, &procedure, sizeof(address));
    if (address <= 3 || address == UINTPTR_MAX || size != sizeof(procedure))
    {
        fprintf(stderr, "The owned graphics provider does not expose %s.\n", name);
        return 0;
    }
    memcpy(target, &procedure, size);
    return 1;
}

static int load_framebuffer_proc(void *target, size_t size, const char *name, int core)
{
    char entry[64];
    snprintf(entry, sizeof(entry), "%s%s", name, core ? "" : "EXT");
    return load_gl_proc(target, size, entry);
}

int main(int argc, char **argv)
{
    static const WCHAR class_name[] = L"XodusOwnedGraphicsCheck";
    const int expected[] = {64, 128, 191, 255};
    const char *operation = "register window class";
    WNDCLASSW window_class = {0};
    PIXELFORMATDESCRIPTOR requested = {0}, actual = {0};
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND window = NULL;
    HDC device = NULL;
    HGLRC context = NULL;
    ATOM registered = 0;
    GLubyte pixel[4] = {0};
    GLuint framebuffer = 0, renderbuffer = 0;
    PFNGLGENFRAMEBUFFERSEXTPROC gen_framebuffers = NULL;
    PFNGLBINDFRAMEBUFFEREXTPROC bind_framebuffer = NULL;
    PFNGLDELETEFRAMEBUFFERSEXTPROC delete_framebuffers = NULL;
    PFNGLGENRENDERBUFFERSEXTPROC gen_renderbuffers = NULL;
    PFNGLBINDRENDERBUFFEREXTPROC bind_renderbuffer = NULL;
    PFNGLDELETERENDERBUFFERSEXTPROC delete_renderbuffers = NULL;
    PFNGLRENDERBUFFERSTORAGEEXTPROC renderbuffer_storage = NULL;
    PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC framebuffer_renderbuffer = NULL;
    PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC check_framebuffer = NULL;
    int format, result = 1, current = 0, core = 0;
    size_t channel;

    if (argc == 2 && !strcmp(argv[1], "--bootstrap"))
    {
        puts("Isolated Windows check started; no credentials requested.");
        return 0;
    }
    core = argc == 2 && !strcmp(argv[1], "--core");
    if (argc != 1 && !core)
    {
        fputs("Usage: xodus-windows-graphics-smoke.exe [--bootstrap|--core]\n", stderr);
        return 2;
    }
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    registered = RegisterClassW(&window_class);
    if (!registered) goto failed;
    operation = "create hidden window";
    window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, class_name,
                             L"Xodus owned hidden graphics check", WS_POPUP,
                             0, 0, 32, 32, NULL, NULL, instance, NULL);
    if (!window) goto failed;
    operation = "obtain owned window DC";
    device = GetDC(window);
    if (!device) goto failed;
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 32;
    requested.cAlphaBits = 8;
    requested.iLayerType = PFD_MAIN_PLANE;
    operation = "choose owned pixel format";
    format = ChoosePixelFormat(device, &requested);
    if (!format) goto failed;
    operation = "describe owned pixel format";
    if (!DescribePixelFormat(device, format, sizeof(actual), &actual)) goto failed;
    operation = "set owned pixel format";
    if (!SetPixelFormat(device, format, &actual)) goto failed;
    if (actual.iPixelType != PFD_TYPE_RGBA || actual.cRedBits < 8 ||
        actual.cGreenBits < 8 || actual.cBlueBits < 8 || actual.cAlphaBits < 8 ||
        (actual.dwFlags & requested.dwFlags) != requested.dwFlags)
    {
        fputs("The owned window did not receive the required double-buffered RGBA8 format.\n", stderr);
        goto done;
    }
    operation = "create owned WGL context";
    context = wglCreateContext(device);
    if (!context) goto failed;
    operation = "activate owned WGL context";
    if (!wglMakeCurrent(device, context)) goto failed;
    current = 1;
    if (!check_gl("initialize the owned current context")) goto done;
    if (core)
    {
        PFNWGLCREATECONTEXTATTRIBSARBPROC create_context = NULL;
        const int attributes[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
                                  WGL_CONTEXT_MINOR_VERSION_ARB, 2,
                                  WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                                  WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, 0};
        GLint profile = 0;
        if (!load_gl_proc(&create_context, sizeof(create_context), "wglCreateContextAttribsARB"))
            goto done;
        operation = "release the owned legacy bootstrap context";
        if (!wglMakeCurrent(NULL, NULL)) goto failed;
        current = 0;
        operation = "delete the owned legacy bootstrap context";
        if (!wglDeleteContext(context)) goto failed;
        context = NULL;
        operation = "create the owned forward-compatible core context";
        context = create_context(device, NULL, attributes);
        if (!context) goto failed;
        operation = "activate the owned core context";
        if (!wglMakeCurrent(device, context)) goto failed;
        current = 1;
        if (!check_gl("initialize the owned core context")) goto done;
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        if (!check_gl("verify the owned core profile")) goto done;
        if (!(profile & GL_CONTEXT_CORE_PROFILE_BIT))
        {
            fputs("The graphics provider did not create the requested core profile.\n", stderr);
            goto done;
        }
    }
    if (!glGetString(GL_VERSION) || !glGetString(GL_RENDERER))
    {
        fputs("The selected graphics provider did not expose a current OpenGL context.\n", stderr);
        goto done;
    }
    if (!check_gl("query the owned graphics provider")) goto done;
    if (!load_framebuffer_proc(&gen_framebuffers, sizeof(gen_framebuffers), "glGenFramebuffers", core) ||
        !load_framebuffer_proc(&bind_framebuffer, sizeof(bind_framebuffer), "glBindFramebuffer", core) ||
        !load_framebuffer_proc(&delete_framebuffers, sizeof(delete_framebuffers), "glDeleteFramebuffers", core) ||
        !load_framebuffer_proc(&gen_renderbuffers, sizeof(gen_renderbuffers), "glGenRenderbuffers", core) ||
        !load_framebuffer_proc(&bind_renderbuffer, sizeof(bind_renderbuffer), "glBindRenderbuffer", core) ||
        !load_framebuffer_proc(&delete_renderbuffers, sizeof(delete_renderbuffers), "glDeleteRenderbuffers", core) ||
        !load_framebuffer_proc(&renderbuffer_storage, sizeof(renderbuffer_storage), "glRenderbufferStorage", core) ||
        !load_framebuffer_proc(&framebuffer_renderbuffer, sizeof(framebuffer_renderbuffer), "glFramebufferRenderbuffer", core) ||
        !load_framebuffer_proc(&check_framebuffer, sizeof(check_framebuffer), "glCheckFramebufferStatus", core))
        goto done;
    gen_framebuffers(1, &framebuffer);
    if (!check_gl("allocate the owned framebuffer")) goto done;
    if (!framebuffer)
    {
        fputs("The graphics provider returned no owned framebuffer object.\n", stderr);
        goto done;
    }
    bind_framebuffer(GL_FRAMEBUFFER_EXT, framebuffer);
    if (!check_gl("bind the owned framebuffer")) goto done;
    gen_renderbuffers(1, &renderbuffer);
    if (!check_gl("allocate the owned color storage")) goto done;
    if (!renderbuffer)
    {
        fputs("The graphics provider returned no owned color-storage object.\n", stderr);
        goto done;
    }
    bind_renderbuffer(GL_RENDERBUFFER_EXT, renderbuffer);
    if (!check_gl("bind the owned color storage")) goto done;
    renderbuffer_storage(GL_RENDERBUFFER_EXT, GL_RGBA8, 32, 32);
    if (!check_gl("initialize the owned RGBA8 storage")) goto done;
    framebuffer_renderbuffer(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                             GL_RENDERBUFFER_EXT, renderbuffer);
    if (!check_gl("attach the owned color storage")) goto done;
    if (check_framebuffer(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
    {
        fputs("The owned offscreen framebuffer is incomplete.\n", stderr);
        goto done;
    }
    if (!check_gl("verify the owned framebuffer")) goto done;
    glViewport(0, 0, 32, 32);
    if (!check_gl("set the owned viewport")) goto done;
    glDisable(GL_DITHER);
    if (!check_gl("disable owned framebuffer dithering")) goto done;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    if (!check_gl("select the owned draw buffer")) goto done;
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    if (!check_gl("select the owned read buffer")) goto done;
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    if (!check_gl("set the owned clear color")) goto done;
    glClear(GL_COLOR_BUFFER_BIT);
    if (!check_gl("clear the owned framebuffer")) goto done;
    glFinish();
    if (!check_gl("finish the owned framebuffer clear")) goto done;
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (!check_gl("read the owned framebuffer pixel")) goto done;
    for (channel = 0; channel < sizeof(pixel); channel++)
    {
        if (abs((int)pixel[channel] - expected[channel]) > 1)
        {
            fputs("The owned framebuffer did not match its known RGBA color within one byte.\n", stderr);
            goto done;
        }
    }
    result = 0;
    goto done;

failed:
    fprintf(stderr, "Cannot %s: Windows error %lu.\n", operation, (unsigned long)GetLastError());
done:
    if (current && framebuffer)
    {
        bind_framebuffer(GL_FRAMEBUFFER_EXT, 0);
        delete_framebuffers(1, &framebuffer);
    }
    if (current && renderbuffer) delete_renderbuffers(1, &renderbuffer);
    if (current && !check_gl("release the owned offscreen framebuffer")) result = 1;
    if (current && !wglMakeCurrent(NULL, NULL))
    {
        fputs("Cannot release the owned current graphics context.\n", stderr);
        result = 1;
    }
    if (context && !wglDeleteContext(context))
    {
        fputs("Cannot delete the owned graphics context.\n", stderr);
        result = 1;
    }
    /* CS_OWNDC belongs to the window and ends with DestroyWindow. */
    if (window && !DestroyWindow(window))
    {
        fputs("Cannot destroy the owned hidden graphics window.\n", stderr);
        result = 1;
    }
    if (registered && !UnregisterClassW(class_name, instance))
    {
        fputs("Cannot unregister the owned graphics window class.\n", stderr);
        result = 1;
    }
    if (!result) puts(core ? "Isolated Windows core graphics outcome passed."
                           : "Isolated Windows legacy graphics outcome passed.");
    return result;
}
