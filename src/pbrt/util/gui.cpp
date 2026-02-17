// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#include <pbrt/util/gui.h>

#include <pbrt/options.h>
#ifdef PBRT_BUILD_GPU_RENDERER
#include <pbrt/gpu/util.h>
#endif  // PBRT_BUILD_GPU_RENDERER
#include <pbrt/util/error.h>
#include <pbrt/util/image.h>
#include <pbrt/util/parallel.h>
#if !defined(__APPLE__)
#include <ext/imgui/imgui.h>
#include <ext/imgui/imgui_impl_glfw.h>
#include <ext/imgui/imgui_impl_opengl3.h>
#endif
#define GL_CHECK(call)                                                   \
    do {                                                                 \
        call;                                                            \
        if (GLenum err = glGetError(); err != GL_NO_ERROR)               \
            LOG_FATAL("GL error: %s for " #call, getGLErrorString(err)); \
    } while (0)

#define GL_CHECK_ERRORS()                                     \
    do {                                                      \
        if (GLenum err = glGetError(); err != GL_NO_ERROR)    \
            LOG_FATAL("GL error: %s", getGLErrorString(err)); \
    } while (0)

namespace pbrt {

const char *getGLErrorString(GLenum error) {
    switch (error) {
    case GL_NO_ERROR:
        return "No error";
    case GL_INVALID_ENUM:
        return "Invalid enum";
    case GL_INVALID_VALUE:
        return "Invalid value";
    case GL_INVALID_OPERATION:
        return "Invalid operation";
    case GL_OUT_OF_MEMORY:
        return "Out of memory";
    default:
        return "Unknown GL error";
    }
}

static void glfwErrorCallback(int error, const char *desc) {
    LOG_ERROR("GLFW [%d]: %s", error, desc);
}

void GUI::keyboardCallback(GLFWwindow *window, int key, int scan, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    auto doKey = [&](int k, char ch) {
        if (key == k) {
            if (action == GLFW_PRESS)
                keysDown.insert(ch);
            else if (action == GLFW_RELEASE) {
                if (auto iter = keysDown.find(ch); iter != keysDown.end())
                    keysDown.erase(iter);
            }
        }
    };

    doKey(GLFW_KEY_A, 'a');
    doKey(GLFW_KEY_D, 'd');
    doKey(GLFW_KEY_S, 's');
    doKey(GLFW_KEY_W, 'w');
    doKey(GLFW_KEY_Q, 'q');
    doKey(GLFW_KEY_E, 'e');

    doKey(GLFW_KEY_B, (mods & GLFW_MOD_SHIFT) ? 'B' : 'b');
    doKey(GLFW_KEY_C, 'c');
    doKey(GLFW_KEY_EQUAL, '=');
    doKey(GLFW_KEY_MINUS, '-');

    doKey(GLFW_KEY_LEFT, 'L');
    doKey(GLFW_KEY_RIGHT, 'R');
    doKey(GLFW_KEY_UP, 'U');
    doKey(GLFW_KEY_DOWN, 'D');

    if (key == GLFW_KEY_R && action == GLFW_PRESS &&
        glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        recordFrames = !recordFrames;
    else
        doKey(GLFW_KEY_R, 'r');
}

bool GUI::processMouse() {
    bool needsReset = false;
    double amount = 0.5f;
    if (!pressed)
        return false;
    if (xoffset < 0) {
        movingFromCamera = movingFromCamera * Rotate(-amount, Vector3f(0, 1, 0));
        needsReset = true;
        xoffset = 0;
    }
    if (xoffset > 0) {
        movingFromCamera = movingFromCamera * Rotate(amount, Vector3f(0, 1, 0));
        needsReset = true;
        xoffset = 0;
    }
    if (yoffset > 0) {
        movingFromCamera = movingFromCamera * Rotate(-amount, Vector3f(1, 0, 0));
        needsReset = true;
        yoffset = 0;
    }
    if (yoffset < 0) {
        movingFromCamera = movingFromCamera * Rotate(amount, Vector3f(1, 0, 0));
        needsReset = true;
        yoffset = 0;
    }
    return needsReset;
}

bool GUI::process() {
    bool needsReset = false;
    needsReset |= processKeys();
    needsReset |= processMouse();
    return needsReset;
}

bool GUI::processKeys() {
    bool needsReset = false;

    auto handleNeedsReset = [&](char key, std::function<Transform(Transform)> update) {
        if (keysDown.find(key) != keysDown.end()) {
            movingFromCamera = update(movingFromCamera);
            needsReset = true;
        }
    };

    handleNeedsReset(
        'a', [&](Transform t) { return t * Translate(Vector3f(-moveScale, 0, 0)); });
    handleNeedsReset(
        'd', [&](Transform t) { return t * Translate(Vector3f(moveScale, 0, 0)); });
    handleNeedsReset(
        's', [&](Transform t) { return t * Translate(Vector3f(0, 0, -moveScale)); });
    handleNeedsReset(
        'w', [&](Transform t) { return t * Translate(Vector3f(0, 0, moveScale)); });
    handleNeedsReset(
        'q', [&](Transform t) { return t * Translate(Vector3f(0, -moveScale, 0)); });
    handleNeedsReset(
        'e', [&](Transform t) { return t * Translate(Vector3f(0, moveScale, 0)); });
    handleNeedsReset('L',
                     [&](Transform t) { return t * Rotate(-.5f, Vector3f(0, 1, 0)); });
    handleNeedsReset('R',
                     [&](Transform t) { return t * Rotate(.5f, Vector3f(0, 1, 0)); });
    handleNeedsReset('U',
                     [&](Transform t) { return t * Rotate(-.5f, Vector3f(1, 0, 0)); });
    handleNeedsReset('D',
                     [&](Transform t) { return t * Rotate(.5f, Vector3f(1, 0, 0)); });
    handleNeedsReset('r', [&](Transform t) { return Transform(); });

    // No reset needed for these.
    if (keysDown.find('c') != keysDown.end()) {
        keysDown.erase(keysDown.find('c'));
        printCameraTransform = true;
    }
    if (keysDown.find('b') != keysDown.end()) {
        keysDown.erase(keysDown.find('b'));
        exposure *= 1.125f;
    }
    if (keysDown.find('B') != keysDown.end()) {
        keysDown.erase(keysDown.find('B'));
        exposure /= 1.125f;
    }
    if (keysDown.find('=') != keysDown.end()) {
        keysDown.erase(keysDown.find('='));
        moveScale *= 2;
    }
    if (keysDown.find('-') != keysDown.end()) {
        keysDown.erase(keysDown.find('-'));
        moveScale *= 0.5;
    }

    return needsReset;
}

static void glfwKeyCallback(GLFWwindow* window, int key, int scan, int action, int mods) {
    GUI* gui = (GUI*)glfwGetWindowUserPointer(window);
    gui->keyboardCallback(window, key, scan, action, mods);
}

void GUI::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        pressed = true;
        glfwGetCursorPos(window, &lastX, &lastY);
    }
    // Left button is released to use in the menus, without change the render process
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_RELEASE) {
        pressed = false;
    }
}

void GUI::Initialize() {
    if (!glfwInit())
        LOG_FATAL("Unable to initialize GLFW");
}

Point2i GUI::GetResolution() {
    auto monitor = glfwGetPrimaryMonitor();
    auto videoMode = glfwGetVideoMode(monitor);
    return Point2i(videoMode->width, videoMode->height);
}

static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action,
                                    int mods) {
    GUI* gui = (GUI*)glfwGetWindowUserPointer(window);
    gui->mouseButtonCallback(window, button, action, mods);
}

static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    GUI* gui = (GUI*)glfwGetWindowUserPointer(window);
    gui->cursorPosCallback(window, xpos, ypos);
}

void GUI::cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    xoffset = xpos - lastX;
    yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;
}

GUI::GUI(std::string title, Vector2i resolution, Float movescale)
    : resolution(resolution), moveScale(movescale) {
    glfwSetErrorCallback(glfwErrorCallback);
    if (Options->fullscreen) {
        window = glfwCreateWindow(resolution.x, resolution.y, "Pbrt-v4",
                                  glfwGetPrimaryMonitor(), NULL);
    } else {
        window = glfwCreateWindow(resolution.x, resolution.y, "Pbrt-v4", NULL, NULL);
    }

    if (!window) {
        glfwTerminate();
        LOG_FATAL("Unable to create GLFW window");
    }
    glfwSetKeyCallback(window, glfwKeyCallback);
    glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
    glfwSetCursorPosCallback(window, glfwCursorPosCallback);

    glfwSetWindowUserPointer(window, this);
    glfwMakeContextCurrent(window);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        LOG_FATAL("gladLoadGLLoader failed");
    // Setup Dear ImGui context -----------------------------------
#if !defined(__APPLE__)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif
    //-------------------------------------------------------------

#ifdef PBRT_BUILD_GPU_RENDERER
    if (Options->useGPU)
        cudaFramebuffer = new CUDAOutputBuffer<RGB>(resolution.x, resolution.y);
    else
#endif  // PBRT_BUILD_GPU_RENDERER
        cpuFramebuffer = new RGB[resolution.x * resolution.y];
}

GUI::~GUI() {
#ifdef PBRT_BUILD_GPU_RENDERER
    delete cudaFramebuffer;
#endif  // PBRT_BUILD_GPU_RENDERER
    delete[] cpuFramebuffer;
    //-------------------------
#if !defined(__APPLE__)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
    //------------------------
    glfwDestroyWindow(window);
    glfwTerminate();
}

DisplayState GUI::RefreshDisplay(int sample, int pixelsamples) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
#if !defined(__APPLE__) // we need to redefine GL_CHECK for MacOS
    GL_CHECK(glViewport(0, 0, width, height));
#else
    glViewport(0, 0, width, height);
#endif
    float pixelScales[2] = {(float)width / (float)windowWidth,
                            (float)height / (float)windowHeight};
    //----------------------------
#if !defined(__APPLE__)
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Animate a simple progress bar
    float progress = (float)sample / (float)pixelsamples;
    ImGui::ProgressBar(progress, ImVec2(0.0f, 0.0f));
    ImGui::Text("Rendered %s of %s samples.", std::to_string(sample).c_str(),
                std::to_string(pixelsamples).c_str());
#endif
    //---------------------------
#ifdef PBRT_BUILD_GPU_RENDERER
    if (Options->useGPU)
        cudaFramebuffer->Draw(width, height);
    else
#endif  // PBRT_BUILD_GPU_RENDERER
    {
#if !defined(__APPLE__)
        GL_CHECK(glEnable(GL_FRAMEBUFFER_SRGB));
        GL_CHECK(glRasterPos2f(-1, 1));
        GL_CHECK(glPixelZoom(pixelScales[0], -pixelScales[1]));
        GL_CHECK(
            glDrawPixels(resolution.x, resolution.y, GL_RGB, GL_FLOAT, cpuFramebuffer));
#else
        glEnable(GL_FRAMEBUFFER_SRGB);
        glRasterPos2f(-1, 1);
        glPixelZoom(pixelScales[0], -pixelScales[1]);
        glDrawPixels(resolution.x, resolution.y, GL_RGB, GL_FLOAT, cpuFramebuffer);

#endif
    }
    //----------------------
#if !defined(__APPLE__)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
    //---------------------
    glfwSwapBuffers(window);
    glfwPollEvents();

    if (recordFrames) {
        const RGB *fb = nullptr;
#ifdef PBRT_BUILD_GPU_RENDERER
        if (cudaFramebuffer)
            fb = cudaFramebuffer->GetReadbackPixels();
        else
#endif
            fb = cpuFramebuffer;

        if (fb) {
            Image image(PixelFormat::Float, {width, height}, {"R", "G", "B"});
            std::memcpy(image.RawPointer({0, 0}), fb, width * height * sizeof(RGB));

            RunAsync(
                [](Image image, int frameNumber) {
                    // TODO: set metadata for e.g. current camera position...
                    ImageMetadata metadata;
                    image.Write(StringPrintf("pbrt-frame%05d.exr", frameNumber),
                                metadata);
                    return 0;  // FIXME: RunAsync() doesn't like lambdas that return
                               // void..
                },
                std::move(image), frameNumber);

            ++frameNumber;
        }
#ifdef PBRT_BUILD_GPU_RENDERER
        if (cudaFramebuffer)
            cudaFramebuffer->StartAsynchronousReadback();
#endif
        recordFrames = false;
    }

    if (glfwWindowShouldClose(window))
        return DisplayState::EXIT;
    else if (process())
        return DisplayState::RESET;
    else
        return DisplayState::NONE;
}

}  // namespace pbrt
