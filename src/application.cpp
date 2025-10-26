//#include "Image.h"
#include "mesh.h"
#include "texture.h"
#include "ui/camera.h"
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
// Can't wait for modules to fix this stuff...
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()
#include <framework/shader.h>
#include <framework/window.h>
#include <functional>
#include <iostream>
#include <vector>
#include "ui/menu.h"


class Application {
public:
    Application()
        : m_window("Final Project", glm::ivec2(1024, 1024), OpenGLVersion::GL41)
        , m_texture(RESOURCE_ROOT "resources/checkerboard.png")
        , m_camera(&m_window, glm::vec3(3, 3, 3), glm::vec3(-3, -3, -3)) // maybe use "utils/constants" for this later
    {
        m_window.registerKeyCallback([this](int key, int scancode, int action, int mods) {
            if (action == GLFW_PRESS)
                onKeyPressed(key, mods);
            else if (action == GLFW_RELEASE)
                onKeyReleased(key, mods);
        });
        m_window.registerMouseMoveCallback(std::bind(&Application::onMouseMove, this, std::placeholders::_1));
        m_window.registerMouseButtonCallback([this](int button, int action, int mods) {
            if (action == GLFW_PRESS)
                onMouseClicked(button, mods);
            else if (action == GLFW_RELEASE)
                onMouseReleased(button, mods);
        });

        m_meshes = GPUMesh::loadMeshGPU(RESOURCE_ROOT "resources/dragon.obj");

        try {
            ShaderBuilder defaultBuilder;
            defaultBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shader_vert.glsl");
            defaultBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shader_frag.glsl");
            m_defaultShader = defaultBuilder.build();

            ShaderBuilder shadowBuilder;
            shadowBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_vert.glsl");
            shadowBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "Shaders/shadow_frag.glsl");
            m_shadowShader = shadowBuilder.build();

            // Any new shaders can be added below in similar fashion.
            // ==> Don't forget to reconfigure CMake when you do!
            //     Visual Studio: PROJECT => Generate Cache for ComputerGraphics
            //     VS Code: ctrl + shift + p => CMake: Configure => enter
            // ....
			
			/* TODO: we could turn this into a compute shader. Is apparently better
			 * parallellizable */
			ShaderBuilder pathTraceBuilder;
			pathTraceBuilder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/path-trace_vert.glsl");
			pathTraceBuilder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/path-trace_frag.glsl");
			m_pathTraceShader = pathTraceBuilder.build();

        } catch (ShaderLoadingException e) {
            std::cerr << e.what() << std::endl;
        }
    }

    void update()
    {
        int dummyInteger = 0; // Initialized to 0
        
        // 1time GL state setup
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

        while (!m_window.shouldClose()) {
            // This is your game loop
            // Put your real-time logic and rendering in here
            m_window.updateInput();

            // Use ImGui for easy input/output of ints, floats, strings, etc...
            ImGui::Begin("Window");
            ImGui::InputInt("This is an integer input", &dummyInteger); // Use ImGui::DragInt or ImGui::DragFloat for larger range of numbers.
            ImGui::Text("Value is: %i", dummyInteger); // Use C printf formatting rules (%i is a signed integer)
            ImGui::Checkbox("Use material if no texture", &m_useMaterial);
            ImGui::Text("Camera / View");
            const char* modes[] = { "First-person", "Third-person", "Top-down" };
            ImGui::Combo("View mode", &m_viewMode, modes, IM_ARRAYSIZE(modes));
            ImGui::Checkbox("Show avatar", &m_showAvatar);
            if (m_viewMode == 1) {
                ImGui::SliderFloat("Follow distance", &m_followDistance, 0.5f, 8.0f, "%.2f");
                ImGui::SliderFloat("Follow height", &m_followHeight, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Look-ahead", &m_lookAhead, 0.0f, 2.5f, "%.2f");
            }
            ImGui::End();


            // camera input
            ImGuiIO& io = ImGui::GetIO();
            if (!io.WantCaptureMouse) {
                m_camera.updateInput(m_viewMode);
            }
            else {
                // potential mouse movement:
                // m_camera.setUserInteraction(false); m_camera.setUserInteraction(true);
            }

            // viewport & aspect
            const glm::ivec2 win = m_window.getWindowSize();
            glViewport(0, 0, win.x, win.y);
            const float aspect = (win.y > 0) ? float(win.x) / float(win.y) : 1.0f;

            // build matrices (proj * view)
            m_projectionMatrix = glm::perspective(glm::radians(80.0f), aspect, 0.1f, 30.0f);

            const glm::vec3 camPos = m_camera.cameraPos();
            const glm::vec3 fwd = m_camera.forward();
            const glm::vec3 up = m_camera.up();

            switch (m_viewMode) {
            case 0: // first
                m_viewMatrix = m_camera.viewMatrix();
                break;

            case 1: { // third
                const glm::vec3 chasePos = camPos - fwd * m_followDistance + up * m_followHeight;
                const glm::vec3 lookAt = camPos + fwd * m_lookAhead;
                m_viewMatrix = glm::lookAt(chasePos, lookAt, up);
                break;
            }

            case 2: { // top
                glm::vec3 topPos = camPos;
                topPos.y = std::max(topPos.y, m_followDistance * 2.0f);
                m_viewMatrix = glm::lookAt(topPos, camPos, glm::vec3(0, 0, -1));
                break;
            }

            default:
                m_viewMatrix = m_camera.viewMatrix();
                break;
            }

            const glm::mat4 viewProjection = m_projectionMatrix * m_viewMatrix;

            // Clear the screen
            glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // ...
            glEnable(GL_DEPTH_TEST);

            const glm::mat4 mvpMatrix = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
            // Normals should be transformed differently than positions (ignoring translations + dealing with scaling):
            // https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
            const glm::mat3 normalModelMatrix = glm::inverseTranspose(glm::mat3(m_modelMatrix));

            for (GPUMesh& mesh : m_meshes) {
                m_defaultShader.bind();
                glUniformMatrix4fv(m_defaultShader.getUniformLocation("mvpMatrix"), 1, GL_FALSE, glm::value_ptr(mvpMatrix));
                //Uncomment this line when you use the modelMatrix (or fragmentPosition)
                //glUniformMatrix4fv(m_defaultShader.getUniformLocation("modelMatrix"), 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
                glUniformMatrix3fv(m_defaultShader.getUniformLocation("normalModelMatrix"), 1, GL_FALSE, glm::value_ptr(normalModelMatrix));
                if (mesh.hasTextureCoords()) {
                    m_texture.bind(GL_TEXTURE0);
                    glUniform1i(m_defaultShader.getUniformLocation("colorMap"), 0);
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_TRUE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), GL_FALSE);
                } else {
                    glUniform1i(m_defaultShader.getUniformLocation("hasTexCoords"), GL_FALSE);
                    glUniform1i(m_defaultShader.getUniformLocation("useMaterial"), m_useMaterial);
                }
                mesh.draw(m_defaultShader);
            }

            // Processes input and swaps the window buffer
            m_window.swapBuffers();
        }
    }

    // In here you can handle key presses
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyPressed(int key, int mods)
    {
        std::cout << "Key pressed: " << key << std::endl;
    }

    // In here you can handle key releases
    // key - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__keys.html
    // mods - Any modifier keys pressed, like shift or control
    void onKeyReleased(int key, int mods)
    {
        std::cout << "Key released: " << key << std::endl;
    }

    // If the mouse is moved this function will be called with the x, y screen-coordinates of the mouse
    void onMouseMove(const glm::dvec2& cursorPos)
    {
        std::cout << "Mouse at position: " << cursorPos.x << " " << cursorPos.y << std::endl;
    }

    // If one of the mouse buttons is pressed this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseClicked(int button, int mods)
    {
        std::cout << "Pressed mouse button: " << button << std::endl;
    }

    // If one of the mouse buttons is released this function will be called
    // button - Integer that corresponds to numbers in https://www.glfw.org/docs/latest/group__buttons.html
    // mods - Any modifier buttons pressed
    void onMouseReleased(int button, int mods)
    {
        std::cout << "Released mouse button: " << button << std::endl;
    }

private:
    Window m_window;
    Camera m_camera;

    // Shader for default rendering and for depth rendering
    Shader m_defaultShader;
    Shader m_shadowShader;
	Shader m_pathTraceShader;

    std::vector<GPUMesh> m_meshes;
    Texture m_texture;
    bool m_useMaterial { true };

    // Projection and view matrices for you to fill in and use
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_modelMatrix { 1.0f };

	// view camera controls
    int   m_viewMode = 0;
    bool  m_showAvatar = true;
    float m_followDistance = 3.0f;
    float m_followHeight = 1.2f;
    float m_lookAhead = 0.8f;
};

int main()
{
    Application app;
    app.update();

    return 0;
}
