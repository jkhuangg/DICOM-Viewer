#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "glad/glad.h"
#include "tinyfiledialogs.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <dcmtk/dcmdata/dctk.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace fs = std::filesystem;

std::vector<GLuint>textures;
int activeView = 1; 
float sliceZs[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
float zoomLevels[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
glm::vec2 zoomCenters[4] = { {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f} };

struct Volume {
    std::vector<uint8_t> data;
    int width, height, depth;
};

const char* vertexShaderSource = R"(
#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform float sliceZ;
uniform sampler3D volumeTex;
uniform float zoom;
uniform vec2 zoomCenter;

void main() {
    vec2 scaledUV = (TexCoord - zoomCenter) / zoom + zoomCenter;
    float val = texture(volumeTex, vec3(scaledUV, sliceZ)).r;
    FragColor = vec4(vec3(val), 1.0);
}
)";

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

float vertices[] =  
{
    -0.5f, -0.5f, 0.0f, 0.0f, 0.0f,     // bottom left
     0.5f, -0.5f, 0.0f, 1.0f, 0.0f,     // bottom right
     0.5f,  0.5f, 0.0f, 1.0f, 1.0f,     // top right
    -0.5f,  0.5f, 0.0f, 0.0f, 1.0f,     // top left
};

glm::vec3 locations[] = 
{
    glm::vec3(-0.5,  0.5f, 0.0f),
    glm::vec3( 0.5,  0.5f, 0.0f),
    glm::vec3(-0.5, -0.5f, 0.0f),
    glm::vec3( 0.5, -0.5f, 0.0f)
};

Volume loadDICOMVolume(const std::string& folderPath) {
    std::vector<std::tuple<int, std::string>> slices;
    for (const auto& entry : fs::directory_iterator(folderPath)) {
        if (entry.path().extension() == ".dcm") {
            DcmFileFormat file;
            if (file.loadFile(entry.path().string().c_str()).good()) {
                DcmDataset* dataset = file.getDataset();
                int instance = 0;
                dataset->findAndGetSint32(DCM_InstanceNumber, instance);
                slices.emplace_back(instance, entry.path().string());
            }
        }
    }

    std::sort(slices.begin(), slices.end());

    std::vector<uint8_t> volumeData;
    int width = 0, height = 0, depth = slices.size();
    Uint16 minVal = 65535, maxVal = 0;
    std::vector<std::vector<Uint16>> rawSlices;

    for (const auto& [_, filepath] : slices) {
        DcmFileFormat file;
        if (file.loadFile(filepath.c_str()).bad()) continue;

        DcmDataset* dataset = file.getDataset();
        const Uint16* pixels = nullptr;
        Uint16 w = 0, h = 0;
        dataset->findAndGetUint16(DCM_Columns, w);
        dataset->findAndGetUint16(DCM_Rows, h);
        dataset->findAndGetUint16Array(DCM_PixelData, pixels);
        if (!pixels) continue;

        if (width == 0) { width = w; height = h; }

        std::vector<Uint16> slice(pixels, pixels + w * h);
        rawSlices.push_back(slice);

        for (auto px : slice) {
            minVal = std::min(minVal, px);
            maxVal = std::max(maxVal, px);
        }
    }

    double scale = (maxVal > minVal) ? 255.0 / (maxVal - minVal) : 1.0;
    for (const auto& slice : rawSlices) {
        for (Uint16 px : slice) {
            uint8_t g = static_cast<uint8_t>((px - minVal) * scale);
            volumeData.push_back(g);
        }
    }

    return { volumeData, width, height, depth };
}

GLuint upload3DTexture(const Volume& vol) {
    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_3D, texID);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RED, vol.width, vol.height, vol.depth, 0,
                 GL_RED, GL_UNSIGNED_BYTE, vol.data.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    return texID;
}

GLuint loadNewVolume(const std::string& folder) {
    Volume volume = loadDICOMVolume(folder);
    if (volume.data.empty()) {
        std::cerr << "Failed to load volume.\n";
        return GLuint(-1);
    }
    return upload3DTexture(volume);
}

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }  

    // Shaders
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    // Error checking shaders
    int success; char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        return -1;
    };

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
        return -1;
    };

    unsigned int shaderProgram;
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    // Delete shader
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // VBO & VAO
    unsigned int VAO, VBO_pos, VBO_tex;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO_pos);
    glGenBuffers(1, &VBO_tex);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_pos);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // VBO for vertices
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*5, (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*5, (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);  

    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {           
        glm::mat4 view          = glm::mat4(1.0f);
        glm::mat4 projection    = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, &view[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        // input
        // -----
        processInput(window);

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // activate shader 
        // ----------------
        glUseProgram(shaderProgram);
        glBindVertexArray(VAO);
 
        for (int i = 0; i < textures.size(); i++)
        {
            glBindTexture(GL_TEXTURE_3D, textures[i]);
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, locations[i]);

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 
                1, GL_FALSE, glm::value_ptr(model));
            glUniform1f(glGetUniformLocation(shaderProgram, "zoom"), zoomLevels[i]);
            glUniform2f(glGetUniformLocation(shaderProgram, "zoomCenter"),zoomCenters[i].x, zoomCenters[i].y);
            glUniform1f(glGetUniformLocation(shaderProgram, "sliceZ"), sliceZs[i]);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO_pos);
    glDeleteBuffers(1, &VBO_tex);
    glDeleteProgram(shaderProgram);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);

    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) activeView = 0;
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) activeView = 1;    
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) activeView = 2;
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) activeView = 3;


    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        sliceZs[activeView] = std::min(sliceZs[activeView] + 0.01f, 1.0f);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        sliceZs[activeView] = std::max(sliceZs[activeView] - 0.01f, 0.0f);

    if (glfwGetKey(window, GLFW_KEY_J) == GLFW_PRESS)
        zoomLevels[activeView] = std::min(zoomLevels[activeView] * 1.05f, 5.0f);
    if (glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS)
        zoomLevels[activeView] = std::max(zoomLevels[activeView] * 0.95f, 0.2f);
    
    if (glfwGetKey(window, GLFW_KEY_L) == GLFW_PRESS)
    {
        const char* picked = tinyfd_selectFolderDialog("Select DICOM Folder", "./");
        if (!picked) return;
        GLuint texID = loadNewVolume(picked);

        if (textures.size() < 4)
        {
            activeView = textures.size();
            textures.push_back(texID);
        }
        else
        {
            textures[activeView] = texID;
        }

        sliceZs[activeView] = 0.5f;
        zoomLevels[activeView] = 1.0f;
    }
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}
