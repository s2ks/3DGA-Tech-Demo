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

struct Triangle {
	Vertex v0;
	Vertex v1;
	Vertex v2;

	Material mat;
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

		void addMesh(std::filesystem::path filePath, bool normalize);
		void buildBVH(int start, int end);
}
