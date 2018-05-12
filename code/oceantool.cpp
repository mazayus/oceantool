/*
 * Copyright 2017 Milan Izai <milan.izai@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <SDL.h>

#include "common.h"
#include "dft.h"
#include "imgui.h"
#include "math.h"
#include "opengl.h"

#include <chrono>
#include <complex>
#include <random>

#define WINDOW_TITLE    "OceanTool"

#define INITIAL_WINDOW_WIDTH    1366
#define INITIAL_WINDOW_HEIGHT   768

#define MILLISECONDS_PER_FRAME  16

static SDL_Window* sdl_window;
static SDL_GLContext sdl_glcontext;
static int window_width = INITIAL_WINDOW_WIDTH;
static int window_height = INITIAL_WINDOW_HEIGHT;

static bool Init();
static void Shutdown();

static GLProgram imgui_program;
static GLuint imgui_texture;
static GLuint imgui_vao;
static GLuint imgui_vertex_buffer;
static GLuint imgui_index_buffer;

static void InitImGui();
static void SendEventToImGui(const SDL_Event* event);
static void RenderImGui();

struct Camera
{
    Transform       transform;
    float           fovy, aspect, znear, zfar;
};

struct OceanParams
{
    // NOTE: These parameters are described in "Simulating Ocean Water" by Tessendorf.

    int             Nx;
    int             Ny;
    float           Lx;
    float           Ly;
    float           Vx;
    float           Vy;
    float           A;
    float           l;
    float           t;

    uint32_t        seed;
};

#define OCEAN_PARAM_ERROR_INVALID_GRID_SIZE         BIT(0)
#define OCEAN_PARAM_ERROR_INVALID_OCEAN_SIZE        BIT(1)
#define OCEAN_PARAM_ERROR_INVALID_WIND_VELOCITY     BIT(2)

enum DisplayMode
{
    DISPLAY_MODE_SOLID,
    DISPLAY_MODE_WIREFRAME,
    DISPLAY_MODE_HEIGHT_MAP,
    DISPLAY_MODE_NORMAL_MAP,
};

struct OceanTool
{
    Camera camera;

    OceanParams params;
    OceanParams pending_params;

    int ocean_param_errors;

    bool gen_accurate_normal_map;

    DisplayMode display_mode;

    GLuint dummy_vao;

    GLProgram mesh_program;
    GLProgram height_map_program;
    GLProgram normal_map_program;

    GLuint height_map;
    GLuint normal_map;

    float min_value, max_value;
};

static void InitOceanTool(OceanTool* tool);
static void UpdateOceanTool(OceanTool* tool, int buttons, int dwheel, int dx, int dy);

int main(int argc, char* argv[])
{
    (void) argc; (void) argv;

    if (!Init())
        return 1;

    InitImGui();

    OceanTool ocean_tool = {};
    InitOceanTool(&ocean_tool);

    bool should_quit = false;

    uint32_t last_frame_time = SDL_GetTicks();

    int mouse_buttons = 0, mouse_dwheel = 0, mouse_dx = 0, mouse_dy = 0;

    while (!should_quit)
    {
        uint32_t time = SDL_GetTicks();
        uint32_t dt = time - last_frame_time;

        if (dt < MILLISECONDS_PER_FRAME)
        {
            SDL_Delay(MILLISECONDS_PER_FRAME - dt);

            time = SDL_GetTicks();
            dt = time - last_frame_time;
        }

        last_frame_time = time;

        mouse_dwheel = 0; mouse_dx = 0; mouse_dy = 0;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                should_quit = true;
                break;
            case SDL_KEYDOWN:
                break;
            case SDL_KEYUP:
                break;
            case SDL_MOUSEBUTTONDOWN:
                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_buttons |= BIT(0);
                    break;
                case SDL_BUTTON_RIGHT:
                    mouse_buttons |= BIT(1);
                    break;
                case SDL_BUTTON_MIDDLE:
                    mouse_buttons |= BIT(2);
                    break;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    mouse_buttons &= ~BIT(0);
                    break;
                case SDL_BUTTON_RIGHT:
                    mouse_buttons &= ~BIT(1);
                    break;
                case SDL_BUTTON_MIDDLE:
                    mouse_buttons &= ~BIT(2);
                    break;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse_dx += event.motion.xrel;
                mouse_dy += event.motion.yrel;
                break;
            case SDL_MOUSEWHEEL:
                mouse_dwheel += event.wheel.y * ((event.wheel.direction == SDL_MOUSEWHEEL_NORMAL) ? 1 : -1);
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                {
                    window_width = event.window.data1;
                    window_height = event.window.data2;
                }
                break;
            }

            SendEventToImGui(&event);
        }

        ImGui::NewFrame();

        ImGui::GetIO().DeltaTime = dt / 1000.0f;

        UpdateOceanTool(&ocean_tool, mouse_buttons, mouse_dwheel, mouse_dx, mouse_dy);

        RenderImGui();

        SDL_GL_SwapWindow(sdl_window);
    }

    Shutdown();
    return 0;
}

#if DEBUG_OPENGL

static void APIENTRY GLDebugCallback(GLenum source, GLenum type, GLuint /*id*/, GLenum severity,
                                     GLsizei length, const GLchar* message, const void* /*userdata*/)
{
    const char* severity_str = NULL;
    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH_ARB:
        severity_str = "HIGH";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM_ARB:
        severity_str = "MEDIUM";
        break;
    case GL_DEBUG_SEVERITY_LOW_ARB:
        severity_str = "LOW";
        break;
    default:
        severity_str = "UNKNOWN";
        break;
    }

    const char* source_str = NULL;
    switch (source)
    {
    case GL_DEBUG_SOURCE_API_ARB:
        source_str = "API";
        break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:
        source_str = "WINDOW_SYSTEM";
        break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:
        source_str = "SHADER_COMPILER";
        break;
    case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:
        source_str = "THIRD_PARTY";
        break;
    case GL_DEBUG_SOURCE_APPLICATION_ARB:
        source_str = "APPLICATION";
        break;
    case GL_DEBUG_SOURCE_OTHER_ARB:
        source_str = "OTHER";
        break;
    default:
        source_str = "UNKNOWN";
        break;
    }

    const char* type_str = NULL;
    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR_ARB:
        type_str = "ERROR";
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:
        type_str = "DEPRECATED_BEHAVIOR";
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:
        type_str = "UNDEFINED_BEHAVIOR";
        break;
    case GL_DEBUG_TYPE_PORTABILITY_ARB:
        type_str = "PORTABILITY";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE_ARB:
        type_str = "PERFORMANCE";
        break;
    case GL_DEBUG_TYPE_OTHER_ARB:
        type_str = "OTHER";
        break;
    default:
        type_str = "UNKNOWN";
        break;
    }

    printf("[GL][%s][%s][%s]: %s", severity_str, source_str, type_str, message);
    if (length == 0 || message[length-1] != '\n')
        putchar('\n');
}

#endif

static bool Init()
{
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

#if DEBUG_OPENGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    sdl_window = SDL_CreateWindow(WINDOW_TITLE, 0, 0, window_width, window_height,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!sdl_window)
    {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        Shutdown();
        return false;
    }

    sdl_glcontext = SDL_GL_CreateContext(sdl_window);
    if (!sdl_glcontext)
    {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        Shutdown();
        return false;
    }

    GL_Init(&SDL_GL_GetProcAddress);

#if DEBUG_OPENGL

    printf("GL_VENDOR = %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER = %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION = %s\n", glGetString(GL_VERSION));
    printf("GL_SHADING_LANGUAGE_VERSION = %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

    if (g_GLInfo.ARB_debug_output)
    {
        glDebugMessageCallbackARB(&GLDebugCallback, NULL);

        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_FALSE);
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_LOW_ARB, 0, NULL, GL_TRUE);
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_MEDIUM_ARB, 0, NULL, GL_TRUE);
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_HIGH_ARB, 0, NULL, GL_TRUE);

        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    }
    else
    {
        fprintf(stderr, "ARB_debug_output not supported\n");
    }

#endif

    // NOTE: This fixes flickering when resizing the window on Linux.
    SDL_GL_SetSwapInterval(0);

    return true;
}

static void Shutdown()
{
    if (sdl_glcontext)
    {
        SDL_GL_DeleteContext(sdl_glcontext);
        sdl_glcontext = NULL;
    }

    if (sdl_window)
    {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }

    SDL_Quit();
}

static void InitImGui()
{
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize.x = window_width;
    io.DisplaySize.y = window_height;

    io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
    io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
    io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
    io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
    io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
    io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
    io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
    io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
    io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
    io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
    io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
    io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
    io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
    io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
    io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
    io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
    io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
    io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
    io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

    imgui_program = {
        "imgui",
        {
            {"shaders/imgui.vert", GL_VERTEX_SHADER},
            {"shaders/imgui.frag", GL_FRAGMENT_SHADER}
        }
    };
    GL_InitShaderProgram(&imgui_program);

    glUseProgram(imgui_program.id);

    glUniform1i(glGetUniformLocation(imgui_program.id, "u_Font"), 0);

    glUseProgram(0);

    glGenVertexArrays(1, &imgui_vao);
    glBindVertexArray(imgui_vao);

    glGenBuffers(1, &imgui_index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, imgui_index_buffer);

    glGenBuffers(1, &imgui_vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, imgui_vertex_buffer);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*) offsetof(ImDrawVert, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (void*) offsetof(ImDrawVert, uv));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (void*) offsetof(ImDrawVert, col));
    glEnableVertexAttribArray(2);

    unsigned char* font_data = NULL;
    int font_width = 0, font_height = 0;
    io.Fonts->GetTexDataAsAlpha8(&font_data, &font_width, &font_height);

    glGenTextures(1, &imgui_texture);
    glBindTexture(GL_TEXTURE_2D, imgui_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, font_width, font_height, 0, GL_RED, GL_UNSIGNED_BYTE, font_data);

    io.Fonts->TexID = (void*)(uintptr_t)imgui_texture;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const GLint font_swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, font_swizzle);
}

static void SendEventToImGui(const SDL_Event* event)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (event->type)
    {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        switch (event->key.keysym.scancode)
        {
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL:
            io.KeyCtrl = (event->key.state == SDL_PRESSED);
            break;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT:
            io.KeyShift = (event->key.state == SDL_PRESSED);
            break;
        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT:
            io.KeyAlt = (event->key.state == SDL_PRESSED);
            break;
        case SDL_SCANCODE_LGUI:
        case SDL_SCANCODE_RGUI:
            io.KeySuper = (event->key.state == SDL_PRESSED);
            break;
        default:
            break;
        }
        io.KeysDown[event->key.keysym.scancode] = (event->key.state == SDL_PRESSED);
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        switch (event->button.button)
        {
        case SDL_BUTTON_LEFT:
            io.MouseDown[0] = (event->button.state == SDL_PRESSED);
            break;
        case SDL_BUTTON_RIGHT:
            io.MouseDown[1] = (event->button.state == SDL_PRESSED);
            break;
        case SDL_BUTTON_MIDDLE:
            io.MouseDown[2] = (event->button.state == SDL_PRESSED);
            break;
        default:
            break;
        }
        break;
    case SDL_MOUSEWHEEL:
        if (event->wheel.y > 0)
            io.MouseWheel = 1;
        if (event->wheel.y < 0)
            io.MouseWheel = -1;
        break;
    case SDL_MOUSEMOTION:
        io.MousePos.x = event->motion.x;
        io.MousePos.y = event->motion.y;
        break;
    case SDL_TEXTINPUT:
        io.AddInputCharactersUTF8(event->text.text);
        break;
    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            io.DisplaySize.x = event->window.data1;
            io.DisplaySize.y = event->window.data2;
        }
        break;
    }
}

static void RenderImGui()
{
    ImGui::Render();

    glViewport(0, 0, window_width, window_height);

    glUseProgram(imgui_program.id);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, imgui_texture);

    glBindVertexArray(imgui_vao);
    glBindBuffer(GL_ARRAY_BUFFER, imgui_vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, imgui_index_buffer);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    glUniformMatrix4fv(glGetUniformLocation(imgui_program.id, "u_ProjectionMatrix"), 1, GL_FALSE,
                       Matrix4::MakeOrtho(0, window_width, window_height, 0, -1, 1).data);

    ImDrawData* draw_data = ImGui::GetDrawData();
    for (int draw_list_index = 0; draw_list_index < draw_data->CmdListsCount; ++draw_list_index)
    {
        ImDrawList* draw_list = draw_data->CmdLists[draw_list_index];

        glBufferData(GL_ARRAY_BUFFER,
                     draw_list->VtxBuffer.Size * sizeof(ImDrawVert), draw_list->VtxBuffer.Data,
                     GL_STREAM_DRAW);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     draw_list->IdxBuffer.Size * sizeof(ImDrawIdx), draw_list->IdxBuffer.Data,
                     GL_STREAM_DRAW);

        unsigned int first_index = 0;
        for (int draw_cmd_index = 0; draw_cmd_index < draw_list->CmdBuffer.Size; ++draw_cmd_index)
        {
            ImDrawCmd* draw_cmd = &draw_list->CmdBuffer.Data[draw_cmd_index];
            glBindTexture(GL_TEXTURE_2D, (GLuint)(uintptr_t)draw_cmd->TextureId);

            glScissor(draw_cmd->ClipRect.x,
                      window_height - draw_cmd->ClipRect.w,
                      draw_cmd->ClipRect.z - draw_cmd->ClipRect.x,
                      draw_cmd->ClipRect.w - draw_cmd->ClipRect.y);
            glDrawElements(GL_TRIANGLES, draw_cmd->ElemCount, GL_UNSIGNED_SHORT,
                           (void*) (first_index * sizeof(ImDrawIdx)));

            first_index += draw_cmd->ElemCount;
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

static void ResizeTextures(OceanTool* tool)
{
    const int Nx = tool->params.Nx;
    const int Ny = tool->params.Ny;

    tool->min_value = 0;
    tool->max_value = 0;

    glBindTexture(GL_TEXTURE_2D, tool->height_map);
    GLfloat* height_map_data = new GLfloat[Nx * Ny];
    for (int y = 0; y < Ny; ++y)
    {
        for (int x = 0; x < Nx; ++x)
        {
            height_map_data[y * Nx + x] = 0.0f;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, Nx, Ny, 0, GL_RED, GL_FLOAT, height_map_data);
    delete[] height_map_data;

    glBindTexture(GL_TEXTURE_2D, tool->normal_map);
    GLfloat* normal_map_data = new GLfloat[Nx * Ny * 3];
    for (int y = 0; y < Ny; ++y)
    {
        for (int x = 0; x < Nx; ++x)
        {
            normal_map_data[(y * Nx + x) * 3 + 0] = 0.5f;
            normal_map_data[(y * Nx + x) * 3 + 1] = 0.5f;
            normal_map_data[(y * Nx + x) * 3 + 2] = 1.0f;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, Nx, Ny, 0, GL_RGB, GL_FLOAT, normal_map_data);
    delete[] normal_map_data;
}

static void InitOceanTool(OceanTool* tool)
{
    glClearColor(0, 0, 0, 0);

    tool->display_mode = DISPLAY_MODE_WIREFRAME;

    tool->params.Nx = 32;
    tool->params.Ny = 32;
    tool->params.Lx = 1000;
    tool->params.Ly = 1000;
    tool->params.Vx = 31;
    tool->params.Vy = 0;
    tool->params.A = 10;
    tool->params.l = 1;
    tool->params.t = 0;
    tool->params.seed = 0;

    tool->pending_params = tool->params;

    tool->camera.fovy = Math::PI / 3;
    tool->camera.aspect = (float) (window_width * 3 / 4) / (float) window_height;
    tool->camera.znear = 0.1f;
    tool->camera.zfar = 10000.0f;
    tool->camera.transform.translation = Vector3(0, 0, 1000);
    tool->camera.transform.rotation = Quaternion::MakeIdentity();
    tool->camera.transform.scale = 1;

    glGenVertexArrays(1, &tool->dummy_vao);
    glBindVertexArray(tool->dummy_vao);

    tool->mesh_program = {
        "mesh_program",
        {
            {"shaders/mesh.vert", GL_VERTEX_SHADER},
            {"shaders/mesh.frag", GL_FRAGMENT_SHADER}
        }
    };
    GL_InitShaderProgram(&tool->mesh_program);

    tool->height_map_program = {
        "height_map_program",
        {
            {"shaders/image.vert", GL_VERTEX_SHADER},
            {"shaders/image_height_map.frag", GL_FRAGMENT_SHADER}
        }
    };
    GL_InitShaderProgram(&tool->height_map_program);

    tool->normal_map_program = {
        "normal_map_program",
        {
            {"shaders/image.vert", GL_VERTEX_SHADER},
            {"shaders/image_normal_map.frag", GL_FRAGMENT_SHADER}
        }
    };
    GL_InitShaderProgram(&tool->normal_map_program);

    glGenTextures(1, &tool->height_map);
    glBindTexture(GL_TEXTURE_2D, tool->height_map);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glGenTextures(1, &tool->normal_map);
    glBindTexture(GL_TEXTURE_2D, tool->normal_map);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    ResizeTextures(tool);
}

static inline float Ph(float kx, float ky, float Vx, float Vy, float A, float l)
{
    float klen2 = kx*kx + ky*ky;
    float klen = sqrt(klen2);

    kx /= klen;
    ky /= klen;

    float Vlen2 = Vx*Vx + Vy*Vy;
    float Vlen = sqrt(Vlen2);

    Vx /= Vlen;
    Vy /= Vlen;

    float L = Vlen2 / 9.81;

    if (klen == 0)
        return 0;
    if (Vlen == 0)
        return 0;

    float abs_k_dot_V = abs(kx*Vx + ky*Vy);

    return A * exp(-1.0 / (klen2*L*L))/(klen2*klen2) * (abs_k_dot_V * abs_k_dot_V) * exp(-klen2*l*l);
}

static void GenerateOceanSpectrum(complex64* spectrum, uint32_t seed,
                                  int Nx, int Ny, float Lx, float Ly, float Vx, float Vy, float A, float l, float t)
{
    #if 1
    std::mt19937 mt(seed);
    #else
    std::mt19937 mt(0);
    #endif
    std::normal_distribution<float> nd(0, 1);

    // NOTE: This isn't done in Tessendorf's paper, but it makes the A parameter independent of the size of the ocean.
    A /= Lx * Ly;

    const double ONE_OVER_SQRT_2 = 0.7071067811865475;

    for (int y = 0; y < Ny; ++y)
    {
        float ky = 2 * Math::PI * y / Ly;

        for (int x = 0; x < Nx; ++x)
        {
            float kx = 2 * Math::PI * x / Lx;

            float zr_a = nd(mt);
            float zi_a = nd(mt);
            complex64 z_a(zr_a, zi_a);
            complex64 h0a = ONE_OVER_SQRT_2 * std::sqrt(Ph(kx, ky, Vx, Vy, A, l)) * z_a;

            float zr_b = nd(mt);
            float zi_b = nd(mt);
            complex64 z_b(zr_b, zi_b);
            complex64 h0b = std::conj(ONE_OVER_SQRT_2 * std::sqrt(Ph(-kx, -ky, Vx, Vy, A, l)) * z_b);

            float omega = sqrt(9.81 * sqrt(kx*kx+ky*ky));
            complex64 h = h0a * std::exp(complex64(0, omega * t)) + h0b * std::exp(complex64(0, -omega * t));
            spectrum[y * Nx + x] = h;
        }
    }
}

static void GenerateOcean(OceanTool* tool)
{
    const int Nx = tool->params.Nx;
    const int Ny = tool->params.Ny;
    const float Lx = tool->params.Lx;
    const float Ly = tool->params.Ly;
    const float Vx = tool->params.Vx;
    const float Vy = tool->params.Vy;
    const float A = tool->params.A;
    const float l = tool->params.l;
    const float t = tool->params.t;
    const uint32_t seed = tool->params.seed;

    ResizeTextures(tool);

    complex64* spectrum = new complex64[Nx * Ny];

    GenerateOceanSpectrum(spectrum, seed, Nx, Ny, Lx, Ly, Vx, Vy, A, l, t);

    complex64* signal = new complex64[Nx * Ny];

    #if USE_SIMD
    IDFT2D_sse(spectrum, signal, Ny, Nx);
    #else
    IDFT2D_scalar(spectrum, signal, Ny, Nx);
    #endif

    float* height_map_data = new float[Nx * Ny];

    float min_value = INFINITY;
    float max_value = -INFINITY;

    for (int y = 0; y < Ny; ++y)
    {
        for (int x = 0; x < Nx; ++x)
        {
            float h = std::abs(signal[y * Nx + x]);
            if (h < min_value) min_value = h;
            if (h > max_value) max_value = h;
            height_map_data[y * Nx + x] = h;
        }
    }

    tool->min_value = min_value;
    tool->max_value = max_value;

    glBindTexture(GL_TEXTURE_2D, tool->height_map);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, Nx, Ny, 0, GL_RED, GL_FLOAT, height_map_data);

    float* grad_x = new float[Nx * Ny];
    float* grad_y = new float[Nx * Ny];

    if (tool->gen_accurate_normal_map)
    {
        // NOTE: Since our original spectrum results in a signal that is not necessarily real, we construct
        // a real signal equal in magnitude to the existing signal and perform spectral differentiation on it.

        complex64* new_signal = new complex64[Nx * Ny];

        for (int y = 0; y < Ny; ++y)
            for (int x = 0; x < Nx; ++x)
                new_signal[y * Nx + x] = std::abs(signal[y * Nx + x]);

        complex64* new_spectrum = new complex64[Nx * Ny];

        #if USE_SIMD
        DFT2D_sse(new_signal, new_spectrum, Ny, Nx);
        #else
        DFT2D_scalar(new_signal, new_spectrum, Ny, Nx);
        #endif

        for (int y = 0; y < Ny; ++y)
            for (int x = 0; x < Nx; ++x)
                new_spectrum[y * Nx + x] /= Nx * Ny;

        const complex64 I = complex64(0, 1);

        complex64* grad_spectrum_x = new complex64[Nx * Ny];
        complex64* grad_spectrum_y = new complex64[Nx * Ny];

        for (int y = 0; y < Ny; ++y)
        {
            double ky = 0;
            if (y < Ny / 2)
                ky = 2 * Math::PI * y / Ly;
            else if (y > Ny / 2) // NOTE: these are actually the negative frequencies
                ky = 2 * Math::PI * (y-Ny) / Ly;

            for (int x = 0; x < Nx; ++x)
            {
                double kx = 0;
                if (x < Nx / 2)
                    kx = 2 * Math::PI * x / Lx;
                else if (x > Nx / 2) // NOTE: these are actually the negative frequencies
                    kx = 2 * Math::PI * (x-Nx) / Lx;

                grad_spectrum_x[y * Nx + x] = new_spectrum[y * Nx + x] * kx * I;
                grad_spectrum_y[y * Nx + x] = new_spectrum[y * Nx + x] * ky * I;
            }
        }

        complex64* grad_signal_x = new complex64[Nx * Ny];
        complex64* grad_signal_y = new complex64[Nx * Ny];

        #if USE_SIMD
        IDFT2D_sse(grad_spectrum_x, grad_signal_x, Ny, Nx);
        IDFT2D_sse(grad_spectrum_y, grad_signal_y, Ny, Nx);
        #else
        IDFT2D_scalar(grad_spectrum_x, grad_signal_x, Ny, Nx);
        IDFT2D_scalar(grad_spectrum_y, grad_signal_y, Ny, Nx);
        #endif

        for (int y = 0; y < Ny; ++y)
        {
            for (int x = 0; x < Nx; ++x)
            {
                grad_x[y * Nx + x] = grad_signal_x[y * Nx + x].real();
                grad_y[y * Nx + x] = grad_signal_y[y * Nx + x].real();
            }
        }

        delete[] new_signal;
        delete[] new_spectrum;
        delete[] grad_spectrum_x;
        delete[] grad_spectrum_y;
        delete[] grad_signal_x;
        delete[] grad_signal_y;
    }
    else
    {
        // NOTE: Use the finite difference approximation to compute the heightmap gradient.

        for (int y = 0; y < Ny; ++y)
        {
            int yb = (y - 1 + Ny) % Ny;
            int yt = (y + 1) % Ny;

            for (int x = 0; x < Nx; ++x)
            {
                int xl = (x - 1 + Nx) % Nx;
                int xr = (x + 1) % Nx;

                grad_x[y * Nx + x] = (height_map_data[y * Nx + xr] - height_map_data[y * Nx + xl]) / (2 * Lx / Nx);
                grad_y[y * Nx + x] = (height_map_data[yt * Nx + x] - height_map_data[yb * Nx + x]) / (2 * Ly / Ny);
            }
        }
    }

    Vector3* normal_map_data = new Vector3[Nx * Ny];

    for (int y = 0; y < Ny; ++y)
    {
        for (int x = 0; x < Nx; ++x)
        {
            Vector3 tangent = Vector3(1, 0, grad_x[y * Nx + x]);
            Vector3 bitangent = Vector3(0, 1, grad_y[y * Nx + x]);
            Vector3 normal = Math::Normalize(Math::Cross(tangent, bitangent));
            normal_map_data[y * Nx + x] = (normal + Vector3(1, 1, 1)) * 0.5;
        }
    }

    glBindTexture(GL_TEXTURE_2D, tool->normal_map);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, Nx, Ny, 0, GL_RGB, GL_FLOAT, normal_map_data);

    delete[] spectrum;
    delete[] signal;
    delete[] height_map_data;
    delete[] grad_x;
    delete[] grad_y;
    delete[] normal_map_data;
}

static void SaveHeightMap(OceanTool* tool, const char* filename)
{
    FILE* fp = fopen(filename, "wb");
    if (!fp)
    {
        fprintf(stderr, "SaveHeightMap: can't open file '%s'\n", filename);
        return;
    }

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, tool->height_map);

    GLint width = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    GLint height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

    GLfloat* pixels = new GLfloat[width * height];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_FLOAT, pixels);

    {
        const uint8_t id_length = 0;
        const uint8_t color_map_type = 0;
        const uint8_t image_type = 3;
        fwrite(&id_length, 1, 1, fp);
        fwrite(&color_map_type, 1, 1, fp);
        fwrite(&image_type, 1, 1, fp);

        const uint8_t color_map_spec[5] = {0, 0, 0, 0, 0};
        fwrite(color_map_spec, 5, 1, fp);

        const uint16_t x_origin = 0;
        const uint16_t y_origin = 0;
        const uint16_t image_width = width;
        const uint16_t image_height = height;
        const uint8_t pixel_depth = 8;
        const uint8_t image_descriptor = 0;
        fwrite(&x_origin, 2, 1, fp);
        fwrite(&y_origin, 2, 1, fp);
        fwrite(&image_width, 2, 1, fp);
        fwrite(&image_height, 2, 1, fp);
        fwrite(&pixel_depth, 1, 1, fp);
        fwrite(&image_descriptor, 1, 1, fp);

        float min_height = tool->min_value;
        float max_height = tool->max_value;

        float height_range = max_height - min_height;
        if (height_range == 0)
            height_range = 1;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                GLubyte height = ((pixels[y * width + x] - min_height) / height_range) * 255;
                fwrite(&height, 1, 1, fp);
            }
        }
    }

    delete[] pixels;
    fclose(fp);
}

static void SaveNormalMap(OceanTool* tool, const char* filename)
{
    FILE* fp = fopen(filename, "wb");
    if (!fp)
    {
        fprintf(stderr, "SaveNormalMap: can't open file '%s'\n", filename);
        return;
    }

    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, tool->normal_map);

    GLint width = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    GLint height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);

    GLubyte* pixels = new GLubyte[width * height * 4];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    {
        const uint8_t id_length = 0;
        const uint8_t color_map_type = 0;
        const uint8_t image_type = 2;
        fwrite(&id_length, 1, 1, fp);
        fwrite(&color_map_type, 1, 1, fp);
        fwrite(&image_type, 1, 1, fp);

        const uint8_t color_map_spec[5] = {0, 0, 0, 0, 0};
        fwrite(color_map_spec, 5, 1, fp);

        const uint16_t x_origin = 0;
        const uint16_t y_origin = 0;
        const uint16_t image_width = width;
        const uint16_t image_height = height;
        const uint8_t pixel_depth = 24;
        const uint8_t image_descriptor = 0;
        fwrite(&x_origin, 2, 1, fp);
        fwrite(&y_origin, 2, 1, fp);
        fwrite(&image_width, 2, 1, fp);
        fwrite(&image_height, 2, 1, fp);
        fwrite(&pixel_depth, 1, 1, fp);
        fwrite(&image_descriptor, 1, 1, fp);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                fwrite(pixels + (y * width + x) * 4 + 2, 1, 1, fp);
                fwrite(pixels + (y * width + x) * 4 + 1, 1, 1, fp);
                fwrite(pixels + (y * width + x) * 4 + 0, 1, 1, fp);
            }
        }
    }

    delete[] pixels;

    fclose(fp);
}

static inline bool IsPowerOf2(unsigned int n)
{
    return (n != 0) && !(n & (n - 1));
}

static int ValidateOceanParams(const OceanParams* params)
{
    int ocean_param_errors = 0;

    if (!IsPowerOf2(params->Nx) || !IsPowerOf2(params->Ny) || params->Nx <= 1 || params->Ny <= 1)
        ocean_param_errors |= OCEAN_PARAM_ERROR_INVALID_GRID_SIZE;

    if (params->Lx <= 0 || params->Ly <= 0)
        ocean_param_errors |= OCEAN_PARAM_ERROR_INVALID_OCEAN_SIZE;

    if (params->Vx == 0 && params->Vy == 0)
        ocean_param_errors |= OCEAN_PARAM_ERROR_INVALID_WIND_VELOCITY;

    return ocean_param_errors;
}

static void UpdateOceanTool(OceanTool* tool, int buttons, int dwheel, int dx, int dy)
{
    // draw main panel

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(window_width / 4, window_height));
    unsigned long window_flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("OceanTool", NULL, window_flags))
    {
        if (ImGui::CollapsingHeader("Ocean", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextWrapped("NOTE: These parameters are described in \"Simulating Ocean Water\" by Tessendorf.");

            if (ImGui::InputInt2("N", &tool->pending_params.Nx))
                tool->ocean_param_errors &= ~OCEAN_PARAM_ERROR_INVALID_GRID_SIZE;

            if (tool->ocean_param_errors & OCEAN_PARAM_ERROR_INVALID_GRID_SIZE)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 0, 0, 255));
                ImGui::TextWrapped("Grid size (N) should be a positive power of two.");
                ImGui::PopStyleColor();
            }

            if (ImGui::InputFloat2("L", &tool->pending_params.Lx))
                tool->ocean_param_errors &= ~OCEAN_PARAM_ERROR_INVALID_OCEAN_SIZE;

            if (tool->ocean_param_errors & OCEAN_PARAM_ERROR_INVALID_OCEAN_SIZE)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 0, 0, 255));
                ImGui::TextWrapped("Ocean size (L) should be positive.");
                ImGui::PopStyleColor();
            }

            if (ImGui::InputFloat2("V", &tool->pending_params.Vx))
                tool->ocean_param_errors &= ~OCEAN_PARAM_ERROR_INVALID_WIND_VELOCITY;

            if (tool->ocean_param_errors & OCEAN_PARAM_ERROR_INVALID_WIND_VELOCITY)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 0, 0, 255));
                ImGui::TextWrapped("Wind velocity (V) should be non-zero.");
                ImGui::PopStyleColor();
            }

            ImGui::InputFloat("A", &tool->pending_params.A);
            ImGui::InputFloat("l", &tool->pending_params.l);
            ImGui::InputFloat("t", &tool->pending_params.t);

            ImGui::Checkbox("Accurate normal map", &tool->gen_accurate_normal_map);
            ImGui::SameLine(); ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Performs spectral differentiation to compute the heightmap gradient. Requires 3 extra DFTs.");

            if (ImGui::Button("Generate with new seed"))
            {
                tool->ocean_param_errors = ValidateOceanParams(&tool->pending_params);
                if (!tool->ocean_param_errors)
                {
                    std::random_device rd;
                    tool->pending_params.seed = rd();
                    tool->params = tool->pending_params;
                    GenerateOcean(tool);
                }
            }

            if (ImGui::Button("Regenerate with current seed"))
            {
                tool->ocean_param_errors = ValidateOceanParams(&tool->pending_params);
                if (!tool->ocean_param_errors)
                {
                    tool->params = tool->pending_params;
                    GenerateOcean(tool);
                }
            }
        }

        if (ImGui::CollapsingHeader("Export", ImGuiTreeNodeFlags_DefaultOpen))
        {
            static char filename[256] = {};
            ImGui::InputText("filename##export", filename, sizeof(filename));

            if (ImGui::Button("Save height map (*.tga)"))
            {
                SaveHeightMap(tool, filename);
            }

            if (ImGui::Button("Save normal map (*.tga)"))
            {
                SaveNormalMap(tool, filename);
            }
        }

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int display_mode = tool->display_mode;
            ImGui::RadioButton("Solid", &display_mode, DISPLAY_MODE_SOLID);
            ImGui::RadioButton("Wireframe", &display_mode, DISPLAY_MODE_WIREFRAME);
            ImGui::RadioButton("Height map", &display_mode, DISPLAY_MODE_HEIGHT_MAP);
            ImGui::RadioButton("Normal map", &display_mode, DISPLAY_MODE_NORMAL_MAP);
            tool->display_mode = (DisplayMode) display_mode;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar();

    // update camera

    {
        const float DESIRED_FOV = Math::PI / 3;

        tool->camera.aspect = (float) (window_width * 3 / 4) / (float) window_height;

        if (tool->camera.aspect >= 1)
        {
            tool->camera.fovy = DESIRED_FOV;
        }
        else
        {
            // compute vertical fov given horizontal fov
            tool->camera.fovy = 2 * atan(1 / tool->camera.aspect * tan(DESIRED_FOV / 2));
        }
    }

    if (!ImGui::GetIO().WantCaptureMouse)
    {
        Matrix4 matrix = tool->camera.transform.GetMatrix();
        Vector3 local_x_axis = Vector3(matrix._11, matrix._21, matrix._31);
        // Vector3 local_y_axis = Vector3(matrix._12, matrix._22, matrix._32);
        // Vector3 local_z_axis = Vector3(matrix._13, matrix._23, matrix._33);

        if (buttons & BIT(0))
        {
            Transform tmp = Transform::MakeIdentity();
            tmp.rotation = AxisAngleToQuaternion(local_x_axis, -dy/256.0f);
            tool->camera.transform = tmp * tool->camera.transform;
            tmp.rotation = AxisAngleToQuaternion(Vector3(0, 0, 1), -dx/256.0f);
            tool->camera.transform = tmp * tool->camera.transform;
        }

        tool->camera.transform.translation = tool->camera.transform.translation * exp(-dwheel/10.0f);

        const float min_dist = Math::Min(tool->params.Lx, tool->params.Ly) / 5;
        const float max_dist = Math::Max(tool->params.Lx, tool->params.Ly) * 2;
        float dist_to_origin = Math::Length(tool->camera.transform.translation);
        float new_dist_to_origin = Math::Clamp(dist_to_origin, min_dist, max_dist);
        tool->camera.transform.translation = Math::Normalize(tool->camera.transform.translation) * new_dist_to_origin;
    }

    // draw ocean / height map / normal map

    glViewport(window_width / 4, 0, window_width * 3 / 4, window_height);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    switch (tool->display_mode)
    {
    case DISPLAY_MODE_SOLID:
    case DISPLAY_MODE_WIREFRAME:
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glDisable(GL_BLEND);

        glUseProgram(tool->mesh_program.id);

        Matrix4 projection_matrix =
            Matrix4::MakePerspective(tool->camera.fovy, tool->camera.aspect, tool->camera.znear, tool->camera.zfar);
        Matrix4 view_matrix =
            MakeWorldToLocalMatrix(tool->camera.transform.translation, tool->camera.transform.rotation, 1);

        glUniformMatrix4fv(glGetUniformLocation(tool->mesh_program.id, "u_WorldToClipMatrix"), 1, GL_FALSE,
                           (projection_matrix * view_matrix).data);
        glUniformMatrix4fv(glGetUniformLocation(tool->mesh_program.id, "u_ObjectToWorldMatrix"), 1, GL_FALSE,
                           Matrix4::MakeIdentity().data);

        glUniform2f(glGetUniformLocation(tool->mesh_program.id, "u_GridSize"),
                    tool->params.Nx, tool->params.Ny);
        glUniform2f(glGetUniformLocation(tool->mesh_program.id, "u_OceanSize"),
                    tool->params.Lx, tool->params.Ly);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tool->height_map);
        glUniform1i(glGetUniformLocation(tool->mesh_program.id, "u_HeightMap"), 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tool->normal_map);
        glUniform1i(glGetUniformLocation(tool->mesh_program.id, "u_NormalMap"), 1);

        glBindVertexArray(tool->dummy_vao);

        if (tool->display_mode == DISPLAY_MODE_WIREFRAME)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        glDrawArrays(GL_TRIANGLES, 0, (tool->params.Nx - 1) * (tool->params.Ny - 1) * 2 * 3);

        if (tool->display_mode == DISPLAY_MODE_WIREFRAME)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        break;
    }
    case DISPLAY_MODE_HEIGHT_MAP:
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glUseProgram(tool->height_map_program.id);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tool->height_map);
        glUniform1i(glGetUniformLocation(tool->height_map_program.id, "u_HeightMap"), 0);

        glUniform2f(glGetUniformLocation(tool->height_map_program.id, "u_HeightRange"),
                    tool->min_value, tool->max_value);

        glBindVertexArray(tool->dummy_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        break;
    }
    case DISPLAY_MODE_NORMAL_MAP:
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        glUseProgram(tool->normal_map_program.id);

        glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(tool->normal_map_program.id, "u_NormalMap"), 0);
        glBindTexture(GL_TEXTURE_2D, tool->normal_map);

        glBindVertexArray(tool->dummy_vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        break;
    }
    default:
        INVALID_CODE_PATH;
    }
}
