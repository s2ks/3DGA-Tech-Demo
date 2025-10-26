#pragma once
#include <filesystem>

#include <framework/mesh.h>
#include <vector>

struct Triangle {
	Vertex v0;
	Vertex v1;
	Vertex v2;

	Material mat;
};

class Scene {
	public:
		std::vector<Mesh*> meshes;
		std::vector<glm::mat4> meshTransforms;

		std::vector<Triangle> triangles;

		Scene();
		~Scene();

		void addMesh(std::filesystem::path filePath);
}
