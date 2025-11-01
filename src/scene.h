#pragma once
#include <framework/mesh.h>
#include <framework/shader.h>
#include <glm/common.hpp>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

#include <exception>
#include <filesystem>
#include <vector>
#include <framework/opengl_includes.h>

struct GPUCam {
	alignas(16) glm::vec3 pos;

	alignas(16) glm::vec3 p0;
	alignas(16) glm::vec3 p1;
	alignas(16) glm::vec3 p2;

	bool is_moving;
}

struct TriVertex {
	alignas(16) glm::vec3 position;
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec2 uv;

	constexpr TriVertex operator=(const Vertex &other) {
		this->position = other.position;
		this->normal = other.normal;
		this->uv = other.texCoord;
		return *this;
	}
};

struct MeshMat {
	float specular;
	float diffuse;
	float refract;
	alignas(16) glm::vec3 kd; // Diffuse color
	alignas(16) glm::vec3 ks { 0.0f }; // Specular color
	alignas(16) glm::vec3 ke { 0.0f }; // Emissive color

	GLuint kdTexture{ 0 };

	constexpr MeshMat operator=(const Material &other) {
		this->kd = other.kd;
		//this->ks = other.ks;
		this->specular = other.shininess;
		this->diffuse = 1.0f - other.shininess;
		this->refract = other.transparency;
		
		return *this;
	}
};

struct Triangle {
	TriVertex v0;
	TriVertex v1;
	TriVertex v2;

	MeshMat mat;
};

struct BVHNode {
	alignas(16) glm::vec3 min;
	alignas(16) glm::vec3 max;
	int left;
	int right;
	int first;
	int count;
};

class Scene {
	public:
		std::vector<Mesh> meshes;
		std::vector<glm::mat4> meshTransforms;
		std::vector<BVHNode> bvh;

		std::vector<Triangle> triangles;

		Scene();
		~Scene();

		std::vector<int> addMesh(
				std::filesystem::path filePath,
				const glm::mat4 &transform = glm::mat4(1.0f),
				bool normalize = false);

		//void addLight(const glm::vec3 &position, const glm::vec3 &color, float size);
		//void setCamera(const glm::vec3 &position, const glm::vec3 &lookAt, const glm::vec3 &up);
		void draw(const Shader &shader);

		void buildBVH(int maxTris = 4);

	private:
		GLuint tri_ubo;
		GLuint bvh_ubo;

		GLuint vao;
		GLuint vbo;
		GLuint ibo;

		Mesh quadMesh;
		GLsizei quadNumIndices;

		size_t buildBVH(int start, int end, int maxTris);

		void updateTriUBO();
		void updateBVHUBO();
};
