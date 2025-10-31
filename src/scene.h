#pragma once
#include <framework/mesh.h>
#include <framework/shader.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

#include <exception>
#include <filesystem>
#include <vector>
#include <framework/opengl_includes.h>

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
	//alignas(16) glm::vec3 ks{ 0.0f };
	alignas(16) glm::vec3 ke { 0.0f };

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

		std::vector<int> addMesh(std::filesystem::path filePath, bool normalize);
		void buildBVH(int maxTris);

	private:
		GLuint triSSBO;
		GLuint bvhSSBO;

		size_t buildBVH(int start, int end, int maxTris);

		void updateTriSSBO();
		void updateBVHSSBO();
};
